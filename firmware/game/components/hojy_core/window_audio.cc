/*
 * SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Overlay of HeroesOfJinYong src/scene/window_audio.cc for ESP32-P4.
 *
 * A BGM track in this data set is a ~3.97 MB 22 kHz stereo WAV. Upstream loads
 * and decodes it inside Mixer::play(), on the game thread, holding the mixer
 * mutex: every scene change (enter/exit submap, end of battle) froze the game
 * for about a second and starved the audio task at the same time.
 *
 * Here the game thread only (a) starts the fade-out of the current track and
 * (b) hands the filename to a low-priority loader task. The loader does the SD
 * read + decode outside the mixer lock and installs the finished Channel when
 * it is ready. Sound effects stay synchronous: they are <= 32 KB and are
 * already served from the in-process cache.
 */

#include "window.hh"

#include "tab5_audio_sync.hh"

#include "audio/channelwav.hh"
#include "channelwavstream.hh"
#include "audio/mixer.hh"
#include "core/config.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <exception>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <fmt/format.h>

#include <cstdio>
#include <string>

namespace hojy::scene {

namespace {

bool fileReadable(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    std::fclose(f);
    return true;
}

constexpr std::uint32_t kMusicFadeOutMs = 500;
constexpr std::uint32_t kMusicFadeInMs = 1000;

SemaphoreHandle_t gMusicLock;
SemaphoreHandle_t gMusicSignal;
TaskHandle_t gMusicTask;
std::string gMusicWanted;     /* latest request; empty means "stop" */
std::uint64_t gMusicRequestId; /* bumped on every request; stale loads are dropped */
int gMusicVolume;
bool gMusicBusy;              /* loader is holding a decoded track right now */

void musicLoaderTask(void *)
{
    for (;;) {
        xSemaphoreTake(gMusicSignal, portMAX_DELAY);

        std::string path;
        std::uint64_t requestId;
        int volume;
        xSemaphoreTake(gMusicLock, portMAX_DELAY);
        path = gMusicWanted;
        requestId = gMusicRequestId;
        volume = gMusicVolume;
        xSemaphoreGive(gMusicLock);

        if (path.empty()) {
            continue;
        }

        xSemaphoreTake(gMusicLock, portMAX_DELAY);
        gMusicBusy = true;
        xSemaphoreGive(gMusicLock);

        const auto t0 = tab5_clock_us();
        audio::Channel *channel = nullptr;
        try {
            /* Stream from the card first: the in-memory channel would hold the
             * whole ~4 MB track, which is about what GlobalMap needs to exist. */
            auto *stream = new audio::ChannelWavStream(&audio::gMixer, path);
            if (stream->ok()) {
                stream->setRepeat(true);
                channel = stream;
            } else {
                delete stream;
                auto *wav = new audio::ChannelWav(&audio::gMixer, path);
                if (wav->ok()) {
                    ESP_LOGW(TAB5_TAG, "bgm not streamable, loading whole file: %s",
                             path.c_str());
                    wav->setRepeat(true);
                    channel = wav;
                } else {
                    delete wav;
                    ESP_LOGE(TAB5_TAG, "bgm load failed %s", path.c_str());
                }
            }
        } catch (const std::bad_alloc &) {
            ESP_LOGE(TAB5_TAG, "bgm bad_alloc %s", path.c_str());
        } catch (const std::exception &e) {
            /* Nothing may leave a FreeRTOS task entry: std::terminate on this
             * chip is abort(), which is a reboot with no message on the panel. */
            ESP_LOGE(TAB5_TAG, "bgm exception %s: %s", path.c_str(), e.what());
        } catch (...) {
            ESP_LOGE(TAB5_TAG, "bgm unknown exception %s", path.c_str());
        }
        /* A newer request arrived while we were reading the card: drop this. */
        bool stale = false;
        xSemaphoreTake(gMusicLock, portMAX_DELAY);
        stale = (requestId != gMusicRequestId);
        xSemaphoreGive(gMusicLock);
        if (channel == nullptr || stale) {
            delete channel;
            xSemaphoreTake(gMusicLock, portMAX_DELAY);
            gMusicBusy = false;
            xSemaphoreGive(gMusicLock);
            continue;
        }

        /* fadeOutMs = 0: the old track was already faded out by the caller, so
         * this takes the branch that calls start() and fills the ring itself. */
        audio::gMixer.play(0, channel, volume, 0, kMusicFadeInMs);
        xSemaphoreTake(gMusicLock, portMAX_DELAY);
        gMusicBusy = false;
        xSemaphoreGive(gMusicLock);
        ESP_LOGI(TAB5_TAG, "bgm ready %s in %ums", path.c_str(),
                 (unsigned)((tab5_clock_us() - t0) / 1000ULL));
    }
}

bool ensureMusicLoader()
{
    if (gMusicTask != nullptr) {
        return true;
    }
    if (gMusicLock == nullptr) {
        gMusicLock = xSemaphoreCreateMutex();
    }
    if (gMusicSignal == nullptr) {
        gMusicSignal = xSemaphoreCreateBinary();
    }
    if (gMusicLock == nullptr || gMusicSignal == nullptr) {
        return false;
    }
    if (xTaskCreate(musicLoaderTask, "hojy_bgm", 8192, nullptr, 3, &gMusicTask) != pdPASS) {
        gMusicTask = nullptr;
        ESP_LOGE(TAB5_TAG, "bgm loader task create failed");
        return false;
    }
    return true;
}

void requestMusic(const std::string &path, int volume)
{
    xSemaphoreTake(gMusicLock, portMAX_DELAY);
    gMusicWanted = path;
    ++gMusicRequestId;
    gMusicVolume = volume;
    xSemaphoreGive(gMusicLock);
    xSemaphoreGive(gMusicSignal);
}

}

/*
 * prepareNewGame() stops the music specifically to free the ~3.7 MB contiguous
 * hole the map layers need (see playbook P-04), and ensureGlobalMap() needs an
 * even bigger one. An in-flight async load is still holding ~4 MB, so both have
 * to wait for the loader to drop it.
 */
void waitMusicLoaderIdle(std::uint32_t timeoutMs)
{
    if (gMusicLock == nullptr) {
        return;
    }
    for (std::uint32_t waited = 0; waited < timeoutMs; waited += 10) {
        bool busy;
        xSemaphoreTake(gMusicLock, portMAX_DELAY);
        busy = gMusicBusy;
        xSemaphoreGive(gMusicLock);
        if (!busy) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAB5_TAG, "bgm loader still busy after %ums", (unsigned)timeoutMs);
}

void Window::playMusic(int idx)
{
    if (idx < 0) {
        if (gMusicLock != nullptr) {
            requestMusic(std::string(), 0);
        }
        audio::gMixer.play(0, static_cast<audio::Channel *>(nullptr), 0, 0, 0);
        waitMusicLoaderIdle(3000);
        playingMusic_ = -1;
        return;
    }
    ++idx;
    if (playingMusic_ == idx) {
        return;
    }
    const int volume = 16 * core::config.musicVolume();
    if (volume <= 0) {
        /* A BGM track is ~4 MB of PSRAM. At volume 0 it would be decoded, held
         * and mixed at zero gain -- 4 MB for silence. Turning music off is the
         * cheapest memory lever this device has; make it actually free it. */
        audio::gMixer.play(0, static_cast<audio::Channel *>(nullptr), 0, 0, 0);
        if (gMusicLock != nullptr) { requestMusic(std::string(), 0); }
        waitMusicLoaderIdle(3000);
        playingMusic_ = idx;
        return;
    }
    const auto wav = core::config.musicFilePath(fmt::format("GAME{:02}.WAV", idx));

    if (fileReadable(wav) && ensureMusicLoader()) {
        /* Start the cross-fade now; the new track installs itself when the
         * card read finishes. Nothing blocks the game loop. */
        audio::gMixer.play(0, static_cast<audio::Channel *>(nullptr), 0,
                           kMusicFadeOutMs, 0);
        requestMusic(wav, volume);
    } else {
        /* No WAV (or no loader task): keep the upstream synchronous path so
         * the MIDI/stub channel selection is unchanged. */
        const auto xmi = core::config.musicFilePath(fmt::format("GAME{:02}.XMI", idx));
        audio::gMixer.play(0, xmi, true, volume, kMusicFadeOutMs, kMusicFadeInMs);
    }
    playingMusic_ = idx;
}

void Window::playAtkSound(int idx)
{
    if (idx >= 24) {
        playEffectSound(idx - 24);
        return;
    }
    audio::gMixer.play(1,
                       core::config.soundFilePath(fmt::format("ATK{:02}.WAV", idx)),
                       false,
                       16 * core::config.soundVolume());
}

void Window::playEffectSound(int idx)
{
    audio::gMixer.play(2,
                       core::config.soundFilePath(fmt::format("E{:02}.WAV", idx)),
                       false,
                       16 * core::config.soundVolume());
}

}
