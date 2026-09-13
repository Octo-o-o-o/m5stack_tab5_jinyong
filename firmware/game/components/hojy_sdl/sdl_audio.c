/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sdl_internal.h"

#include "tab5_platform.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>

typedef struct {
    SDL_AudioSpec spec;
    SDL_AudioCallback callback;
    void *userdata;
    volatile int paused;
    volatile int running;
    TaskHandle_t task;
} AudioDev;

static AudioDev s_dev;

static int fmt_bytes(SDL_AudioFormat fmt)
{
    switch (fmt) {
    case AUDIO_U8:
    case AUDIO_S8:
        return 1;
    case AUDIO_S16LSB:
    case AUDIO_S16MSB:
        return 2;
    case AUDIO_S32LSB:
    case AUDIO_F32LSB:
        return 4;
    default:
        return 0;
    }
}

static void f32_to_s16(const float *in, int16_t *out, int samples)
{
    for (int i = 0; i < samples; ++i) {
        float s = in[i];
        if (s > 1.f) {
            s = 1.f;
        } else if (s < -1.f) {
            s = -1.f;
        }
        const int v = (int)lrintf(s * 32767.f);
        out[i] = (int16_t)v;
    }
}

static void audio_task(void *arg)
{
    AudioDev *dev = (AudioDev *)arg;
    const int frames = dev->spec.samples;
    const int ch = dev->spec.channels ? dev->spec.channels : 2;
    const int f32_bytes = frames * ch * (int)sizeof(float);
    const int s16_bytes = frames * ch * (int)sizeof(int16_t);
    float *mix = (float *)SDL_malloc((size_t)f32_bytes);
    int16_t *pcm = (int16_t *)SDL_malloc((size_t)s16_bytes);
    if (mix == NULL || pcm == NULL) {
        ESP_LOGE(TAB5_TAG, "audio task buffers failed");
        vTaskDelete(NULL);
        return;
    }

    while (dev->running) {
        if (dev->paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        memset(mix, 0, (size_t)f32_bytes);
        if (dev->callback) {
            dev->callback(dev->userdata, (Uint8 *)mix, f32_bytes);
        }
        f32_to_s16(mix, pcm, frames * ch);
        if (tab5_audio_write(pcm, (size_t)s16_bytes) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    SDL_free(mix);
    SDL_free(pcm);
    vTaskDelete(NULL);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained, int allowed_changes)
{
    (void)device;
    (void)allowed_changes;
    if (iscapture || desired == NULL) {
        sdl_set_error("OpenAudioDevice unsupported");
        return 0;
    }
    if (!tab5_audio_ready()) {
        sdl_set_error("no speaker codec");
        return 0;
    }
    if (s_dev.running) {
        SDL_CloseAudioDevice(1);
    }

    int freq = desired->freq;
    if (freq <= 0) {
        freq = 22050;
    }
    SDL_AudioSpec spec = *desired;
    spec.freq = freq;
    spec.format = AUDIO_F32;
    spec.channels = 2;
    if (spec.samples == 0) {
        spec.samples = 1024;
    }
    spec.size = (Uint32)spec.samples * spec.channels * 4u;
    spec.silence = 0;

    if (tab5_audio_open(spec.freq, spec.channels) != ESP_OK) {
        sdl_set_error("codec open failed");
        return 0;
    }

    s_dev.spec = spec;
    s_dev.callback = desired->callback;
    s_dev.userdata = desired->userdata;
    s_dev.paused = 1;
    s_dev.running = 1;
    if (xTaskCreate(audio_task, "hojy_audio", 8192, &s_dev, 5, &s_dev.task) != pdPASS) {
        s_dev.running = 0;
        sdl_set_error("audio task failed");
        return 0;
    }
    if (obtained) {
        *obtained = spec;
    }
    ESP_LOGI(TAB5_TAG, "SDL_OpenAudioDevice %d Hz F32 stereo samples=%u",
             spec.freq, (unsigned)spec.samples);
    return 1;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    s_dev.running = 0;
    s_dev.paused = 1;
    vTaskDelay(pdMS_TO_TICKS(20));
    s_dev.task = NULL;
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
    (void)dev;
    s_dev.paused = pause_on ? 1 : 0;
}

void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src, SDL_AudioFormat format, Uint32 len, int volume)
{
    if (dst == NULL || src == NULL || len == 0) {
        return;
    }
    if (volume <= 0) {
        return;
    }
    if (volume > 128) {
        volume = 128;
    }
    if (format == AUDIO_F32) {
        float *d = (float *)dst;
        const float *s = (const float *)src;
        const unsigned n = len / 4u;
        const float v = (float)volume / 128.f;
        for (unsigned i = 0; i < n; ++i) {
            float x = d[i] + s[i] * v;
            if (x > 1.f) {
                x = 1.f;
            } else if (x < -1.f) {
                x = -1.f;
            }
            d[i] = x;
        }
        return;
    }
    if (format == AUDIO_S16) {
        int16_t *d = (int16_t *)dst;
        const int16_t *s = (const int16_t *)src;
        const unsigned n = len / 2u;
        for (unsigned i = 0; i < n; ++i) {
            int x = d[i] + (s[i] * volume) / 128;
            if (x > 32767) {
                x = 32767;
            } else if (x < -32768) {
                x = -32768;
            }
            d[i] = (int16_t)x;
        }
    }
}

static float sample_as_f32(const Uint8 *p, SDL_AudioFormat fmt, int index)
{
    switch (fmt) {
    case AUDIO_U8:
        return ((float)p[index] - 128.f) / 128.f;
    case AUDIO_S8:
        return ((float)((int8_t)p[index])) / 127.f;
    case AUDIO_S16LSB: {
        const int16_t v = (int16_t)(p[index * 2] | (p[index * 2 + 1] << 8));
        return (float)v / 32768.f;
    }
    case AUDIO_S32LSB: {
        const int32_t v = (int32_t)(p[index * 4] | (p[index * 4 + 1] << 8) |
                                    (p[index * 4 + 2] << 16) | (p[index * 4 + 3] << 24));
        return (float)v / 2147483648.f;
    }
    case AUDIO_F32LSB: {
        float v;
        memcpy(&v, p + index * 4, 4);
        return v;
    }
    default:
        return 0.f;
    }
}

static void store_f32_as(Uint8 *p, SDL_AudioFormat fmt, int index, float s)
{
    if (s > 1.f) {
        s = 1.f;
    } else if (s < -1.f) {
        s = -1.f;
    }
    switch (fmt) {
    case AUDIO_U8:
        p[index] = (Uint8)(s * 127.f + 128.f);
        break;
    case AUDIO_S16LSB: {
        const int16_t v = (int16_t)lrintf(s * 32767.f);
        p[index * 2] = (Uint8)(v & 0xff);
        p[index * 2 + 1] = (Uint8)((v >> 8) & 0xff);
        break;
    }
    case AUDIO_S32LSB: {
        const int32_t v = (int32_t)lrintf(s * 2147483647.f);
        p[index * 4] = (Uint8)(v & 0xff);
        p[index * 4 + 1] = (Uint8)((v >> 8) & 0xff);
        p[index * 4 + 2] = (Uint8)((v >> 16) & 0xff);
        p[index * 4 + 3] = (Uint8)((v >> 24) & 0xff);
        break;
    }
    case AUDIO_F32LSB:
        memcpy(p + index * 4, &s, 4);
        break;
    default:
        break;
    }
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_fmt, Uint8 src_channels, int src_rate,
                      SDL_AudioFormat dst_fmt, Uint8 dst_channels, int dst_rate)
{
    if (cvt == NULL || src_channels == 0 || dst_channels == 0 || src_rate <= 0 || dst_rate <= 0) {
        return -1;
    }
    memset(cvt, 0, sizeof(*cvt));
    cvt->src_format = src_fmt;
    cvt->dst_format = dst_fmt;
    cvt->src_channels = src_channels;
    cvt->dst_channels = dst_channels;
    cvt->src_rate = src_rate;
    cvt->dst_rate = dst_rate;
    if (src_fmt == dst_fmt && src_channels == dst_channels && src_rate == dst_rate) {
        cvt->needed = 0;
        cvt->len_mult = 1;
        cvt->len_ratio = 1.0;
        return 0;
    }
    const int sb = fmt_bytes(src_fmt);
    const int db = fmt_bytes(dst_fmt);
    if (sb == 0 || db == 0) {
        return -1;
    }
    double ratio = ((double)dst_rate / (double)src_rate) * ((double)dst_channels / (double)src_channels)
        * ((double)db / (double)sb);
    cvt->needed = 1;
    cvt->len_ratio = ratio;
    cvt->len_mult = (int)ceil(ratio) + 2;
    if (cvt->len_mult < 2) {
        cvt->len_mult = 2;
    }
    return 1;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    if (cvt == NULL || cvt->buf == NULL || cvt->len <= 0) {
        return -1;
    }
    if (!cvt->needed) {
        cvt->len_cvt = cvt->len;
        return 0;
    }
    const int sb = fmt_bytes(cvt->src_format);
    const int db = fmt_bytes(cvt->dst_format);
    if (sb == 0 || db == 0) {
        return -1;
    }
    const int src_frames = cvt->len / (sb * cvt->src_channels);
    if (src_frames <= 0) {
        return -1;
    }
    const int dst_frames = (int)((int64_t)src_frames * cvt->dst_rate / cvt->src_rate);
    Uint8 *tmp = (Uint8 *)SDL_malloc((size_t)dst_frames * (size_t)cvt->dst_channels * (size_t)db);
    if (tmp == NULL) {
        return -1;
    }
    for (int df = 0; df < dst_frames; ++df) {
        const int sf = (int)((int64_t)df * cvt->src_rate / cvt->dst_rate);
        const int src_i = (sf < src_frames ? sf : src_frames - 1) * cvt->src_channels;
        float left = sample_as_f32(cvt->buf, cvt->src_format, src_i);
        float right = (cvt->src_channels > 1)
            ? sample_as_f32(cvt->buf, cvt->src_format, src_i + 1)
            : left;
        if (cvt->dst_channels == 1) {
            store_f32_as(tmp, cvt->dst_format, df, (left + right) * 0.5f);
        } else {
            store_f32_as(tmp, cvt->dst_format, df * 2, left);
            store_f32_as(tmp, cvt->dst_format, df * 2 + 1, right);
        }
    }
    const int out_bytes = dst_frames * cvt->dst_channels * db;
    memcpy(cvt->buf, tmp, (size_t)out_bytes);
    SDL_free(tmp);
    cvt->len_cvt = out_bytes;
    return 0;
}

static uint16_t ru16(const Uint8 *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t ru32(const Uint8 *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec,
                              Uint8 **audio_buf, Uint32 *audio_len)
{
    if (src == NULL || spec == NULL || audio_buf == NULL || audio_len == NULL) {
        return NULL;
    }
    const Uint8 *p = src->base;
    const size_t n = (size_t)(src->stop - src->base);
    if (n < 44 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
        sdl_set_error("not a WAV");
        if (freesrc) {
            SDL_free(src);
        }
        return NULL;
    }
    size_t off = 12;
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const Uint8 *data = NULL;
    uint32_t data_len = 0;
    while (off + 8 <= n) {
        const uint32_t sz = ru32(p + off + 4);
        if (memcmp(p + off, "fmt ", 4) == 0 && off + 8 + sz <= n && sz >= 16) {
            fmt = ru16(p + off + 8);
            ch = ru16(p + off + 10);
            rate = (int)ru32(p + off + 12);
            bits = ru16(p + off + 22);
        } else if (memcmp(p + off, "data", 4) == 0 && off + 8 + sz <= n) {
            data = p + off + 8;
            data_len = sz;
        }
        off += 8 + ((sz + 1u) & ~1u);
    }
    if (data == NULL || ch == 0 || rate <= 0) {
        sdl_set_error("WAV missing chunks");
        if (freesrc) {
            SDL_free(src);
        }
        return NULL;
    }
    SDL_AudioFormat af = AUDIO_U8;
    if (fmt == 1 && bits == 8) {
        af = AUDIO_U8;
    } else if (fmt == 1 && bits == 16) {
        af = AUDIO_S16LSB;
    } else if (fmt == 1 && bits == 32) {
        af = AUDIO_S32LSB;
    } else if (fmt == 3 && bits == 32) {
        af = AUDIO_F32LSB;
    } else {
        sdl_set_error("unsupported WAV fmt=%d bits=%d", fmt, bits);
        if (freesrc) {
            SDL_free(src);
        }
        return NULL;
    }
    Uint8 *copy = (Uint8 *)SDL_malloc(data_len);
    if (copy == NULL) {
        if (freesrc) {
            SDL_free(src);
        }
        return NULL;
    }
    memcpy(copy, data, data_len);
    memset(spec, 0, sizeof(*spec));
    spec->freq = rate;
    spec->format = af;
    spec->channels = (Uint8)ch;
    spec->samples = 4096;
    spec->size = data_len;
    *audio_buf = copy;
    *audio_len = data_len;
    if (freesrc) {
        SDL_free(src);
    }
    return spec;
}

void SDL_FreeWAV(Uint8 *audio_buf)
{
    SDL_free(audio_buf);
}
