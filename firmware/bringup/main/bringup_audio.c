#include "bringup.h"

#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static esp_codec_dev_handle_t s_spk;

esp_err_t bringup_audio_start(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_feature_enable(BSP_FEATURE_SPEAKER, true));

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        ESP_LOGW(BRINGUP_TAG, "audio stub: speaker codec init returned null");
        return ESP_FAIL;
    }

    ESP_LOGI(BRINGUP_TAG, "ES8388 speaker codec handle %p", (void *)s_spk);
    return ESP_OK;
}

void bringup_audio_beep_or_stub(void)
{
    if (s_spk == NULL) {
        ESP_LOGW(BRINGUP_TAG, "audio stub: no codec, skip beep");
        return;
    }

    const int rate = 16000;
    const int ms = 120;
    const int frames = rate * ms / 1000;
    const size_t bytes = (size_t)frames * 2U * sizeof(int16_t);
    int16_t *pcm = calloc(frames * 2, sizeof(int16_t));
    if (pcm == NULL) {
        ESP_LOGW(BRINGUP_TAG, "audio stub: no heap for beep");
        return;
    }

    for (int i = 0; i < frames; ++i) {
        const int16_t s = ((i / 18) & 1) ? 2500 : -2500;
        pcm[i * 2] = s;
        pcm[i * 2 + 1] = s;
    }

    esp_codec_dev_sample_info_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.bits_per_sample = 16;
    fs.channel = 2;
    fs.sample_rate = (uint32_t)rate;

    esp_err_t err = esp_codec_dev_set_out_vol(s_spk, 60);
    if (err != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "audio stub: set volume failed: %s", esp_err_to_name(err));
    }
    err = esp_codec_dev_open(s_spk, &fs);
    if (err != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "audio stub: codec open failed: %s", esp_err_to_name(err));
        free(pcm);
        return;
    }
    err = esp_codec_dev_write(s_spk, pcm, (int)bytes);
    ESP_LOGI(BRINGUP_TAG, "audio beep write %u bytes -> %s", (unsigned)bytes, esp_err_to_name(err));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(s_spk));
    free(pcm);
}
