/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tab5_platform.h"

#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

#include <string.h>

static esp_codec_dev_handle_t s_spk;
static bool s_open;

esp_err_t tab5_audio_start(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_feature_enable(BSP_FEATURE_SPEAKER, true));
    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        ESP_LOGW(TAB5_TAG, "audio: speaker codec init returned null");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(s_spk, 70));
    ESP_LOGI(TAB5_TAG, "ES8388 speaker codec handle %p", (void *)s_spk);
    return ESP_OK;
}

bool tab5_audio_ready(void)
{
    return s_spk != NULL;
}

esp_err_t tab5_audio_open(int sample_rate, int channels)
{
    if (s_spk == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_open) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(s_spk));
        s_open = false;
    }
    esp_codec_dev_sample_info_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.bits_per_sample = 16;
    fs.channel = (uint8_t)((channels <= 1) ? 1 : 2);
    fs.sample_rate = (uint32_t)sample_rate;
    const esp_err_t err = esp_codec_dev_open(s_spk, &fs);
    if (err != ESP_OK) {
        ESP_LOGW(TAB5_TAG, "audio open %d Hz / %d ch failed: %s",
                 sample_rate, channels, esp_err_to_name(err));
        return err;
    }
    s_open = true;
    ESP_LOGI(TAB5_TAG, "audio open %d Hz / %u ch S16", sample_rate, (unsigned)fs.channel);
    return ESP_OK;
}

esp_err_t tab5_audio_write(const int16_t *pcm, size_t bytes)
{
    if (!s_open || s_spk == NULL || pcm == NULL || bytes == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_write(s_spk, (void *)pcm, (int)bytes);
}
