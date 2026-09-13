/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/*
 * Streaming WAV channel for background music.
 *
 * audio::ChannelWav decodes a whole track into PSRAM. In this data set a track
 * is 22050 Hz stereo S16 x 45 s = 3.97 MB, held for as long as it plays, which
 * is most of the session. That is the single largest avoidable resident block
 * on this device -- it is roughly what GlobalMap needs to exist at all.
 *
 * This reads the PCM from the card in ~8 KB pieces instead. Only uncompressed
 * PCM is handled (U8 / S16 / S32 / F32); anything else reports !ok() so the
 * caller can fall back to the in-memory channel.
 */

#include "audio/channel.hh"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hojy::audio {

class ChannelWavStream final : public Channel {
public:
    ChannelWavStream(Mixer *mixer, const std::string &filename);
    ~ChannelWavStream() override;

    void load(const std::string &filename) override;
    void reset() override { pos_ = 0; }

protected:
    size_t readPCMData(const void **data, size_t size, bool convType) override;

private:
    bool open(const std::string &filename);
    void close();
    /* Returns bytes actually produced; wraps at the end when repeating. */
    size_t readRaw(std::uint8_t *dst, size_t bytes);

private:
    std::FILE *fp_ = nullptr;
    std::uint32_t dataOffset_ = 0;
    std::uint32_t dataLength_ = 0;
    std::uint32_t pos_ = 0;
    std::uint32_t filePos_ = 0;          /* tracked so we can skip fseek */
    int channels_ = 2;
    int bytesPerSample_ = 2;
    std::vector<std::uint8_t> raw_;
    std::vector<std::uint8_t> cache_;
};

}
