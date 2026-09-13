/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "channelwavstream.hh"

#include "audio/mixer.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <algorithm>
#include <cstring>

namespace hojy::audio {

namespace {

std::uint16_t ru16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t ru32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

float sampleToFloat(const std::uint8_t *p, Mixer::DataType type) {
    switch (type) {
    case Mixer::I16: {
        const std::int16_t v = static_cast<std::int16_t>(p[0] | (p[1] << 8));
        return static_cast<float>(v) / 32768.f;
    }
    case Mixer::I32: {
        const std::int32_t v = static_cast<std::int32_t>(ru32(p));
        return static_cast<float>(v) / 2147483648.f;
    }
    default: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    }
}

}

ChannelWavStream::ChannelWavStream(Mixer *mixer, const std::string &filename)
    : Channel(mixer, nullptr, 0) {
    ok_ = open(filename);
}

ChannelWavStream::~ChannelWavStream() {
    close();
}

void ChannelWavStream::load(const std::string &filename) {
    close();
    resampler_.reset();
    ok_ = open(filename);
}

void ChannelWavStream::close() {
    if (fp_ != nullptr) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    dataOffset_ = dataLength_ = pos_ = filePos_ = 0;
}

bool ChannelWavStream::open(const std::string &filename) {
    close();
    fp_ = std::fopen(filename.c_str(), "rb");
    if (fp_ == nullptr) {
        return false;
    }
    /* Chunk headers only; the PCM itself is never pulled in as a whole. */
    std::uint8_t head[256];
    const size_t got = std::fread(head, 1, sizeof(head), fp_);
    if (got < 44 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) {
        close();
        return false;
    }
    int format = 0, bits = 0, rate = 0, channels = 0;
    std::uint32_t dataOff = 0, dataLen = 0;
    size_t off = 12;
    while (off + 8 <= got) {
        const std::uint32_t sz = ru32(head + off + 4);
        if (std::memcmp(head + off, "fmt ", 4) == 0 && sz >= 16 && off + 8 + 16 <= got) {
            format = ru16(head + off + 8);
            channels = ru16(head + off + 10);
            rate = static_cast<int>(ru32(head + off + 12));
            bits = ru16(head + off + 22);
        } else if (std::memcmp(head + off, "data", 4) == 0) {
            dataOff = static_cast<std::uint32_t>(off + 8);
            dataLen = sz;
            break;
        }
        off += 8 + ((sz + 1u) & ~1u);
    }
    if (dataOff == 0 || dataLen == 0 || channels != 2 || rate <= 0) {
        /* Mono or an exotic layout: let the caller use the in-memory channel,
         * which knows how to convert channel counts. */
        close();
        return false;
    }
    if (format == 1 && bits == 16) {
        typeIn_ = Mixer::I16; bytesPerSample_ = 2;
    } else if (format == 1 && bits == 32) {
        typeIn_ = Mixer::I32; bytesPerSample_ = 4;
    } else if (format == 3 && bits == 32) {
        typeIn_ = Mixer::F32; bytesPerSample_ = 4;
    } else {
        /* Mixer::DataType has no 8-bit form, and anything compressed is out of
         * scope here; the in-memory channel handles those. */
        close();
        return false;
    }
    channels_ = channels;
    sampleRateIn_ = rate;
    dataOffset_ = dataOff;
    dataLength_ = dataLen;
    pos_ = 0;
    filePos_ = static_cast<std::uint32_t>(got);
    ESP_LOGI(TAB5_TAG, "bgm stream %s %d Hz %d bit, %u KB on card (not in RAM)",
             filename.c_str(), rate, bits, (unsigned)(dataLen / 1024u));
    return true;
}

size_t ChannelWavStream::readRaw(std::uint8_t *dst, size_t bytes) {
    if (fp_ == nullptr || dataLength_ == 0) {
        return 0;
    }
    size_t done = 0;
    while (done < bytes) {
        if (pos_ >= dataLength_) {
            if (!repeat_) { break; }
            pos_ = 0;
        }
        const std::uint32_t want = static_cast<std::uint32_t>(
            std::min<size_t>(bytes - done, dataLength_ - pos_));
        const std::uint32_t at = dataOffset_ + pos_;
        if (filePos_ != at) {
            if (std::fseek(fp_, static_cast<long>(at), SEEK_SET) != 0) { break; }
            filePos_ = at;
        }
        const size_t got = std::fread(dst + done, 1, want, fp_);
        if (got == 0) { break; }
        filePos_ += static_cast<std::uint32_t>(got);
        pos_ += static_cast<std::uint32_t>(got);
        done += got;
    }
    return done;
}

size_t ChannelWavStream::readPCMData(const void **data, size_t size, bool convType) {
    if (data == nullptr || size == 0) {
        return 0;
    }
    const bool needConv = convType && typeIn_ != typeOut_;
    const size_t inSample = Mixer::dataTypeToSize(typeIn_);
    const size_t outSample = Mixer::dataTypeToSize(typeOut_);
    if (inSample == 0 || outSample == 0) {
        return 0;
    }

    size_t inBytes = size;
    if (needConv) {
        const size_t outFrame = outSample * 2;
        if (outFrame == 0) { return 0; }
        inBytes = size / outFrame * inSample * 2;
    }
    if (inBytes == 0) {
        return 0;
    }
    if (raw_.size() < inBytes) {
        raw_.resize(inBytes);
    }
    const size_t got = readRaw(raw_.data(), inBytes);
    if (got == 0) {
        return 0;
    }
    if (!needConv) {
        *data = raw_.data();
        return got;
    }

    const size_t samples = got / inSample;
    const size_t outBytes = samples * outSample;
    if (cache_.size() < outBytes) {
        cache_.resize(outBytes);
    }
    /* The mixer on this device always asks for F32. */
    if (typeOut_ == Mixer::F32) {
        auto *out = reinterpret_cast<float *>(cache_.data());
        for (size_t i = 0; i < samples; ++i) {
            out[i] = sampleToFloat(raw_.data() + i * inSample, typeIn_);
        }
    } else if (typeOut_ == Mixer::I16) {
        auto *out = reinterpret_cast<std::int16_t *>(cache_.data());
        for (size_t i = 0; i < samples; ++i) {
            float v = sampleToFloat(raw_.data() + i * inSample, typeIn_);
            v = std::min(1.f, std::max(-1.f, v));
            out[i] = static_cast<std::int16_t>(v * 32767.f);
        }
    } else {
        return 0;
    }
    *data = cache_.data();
    return outBytes;
}

}
