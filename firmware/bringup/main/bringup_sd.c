#include "bringup.h"

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

esp_err_t bringup_sd_start(void)
{
    const esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "SD mount failed: %s (ok if no card)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(BRINGUP_TAG, "SD mounted at %s", BSP_SD_MOUNT_POINT);

    const char *path = BSP_SD_MOUNT_POINT "/jinyong/config/bringup.txt";
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGW(BRINGUP_TAG, "missing %s — copy prepared data as /jinyong/ on the card", path);
        return ESP_ERR_NOT_FOUND;
    }

    char line[128];
    if (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = 0;
        ESP_LOGI(BRINGUP_TAG, "read %s: %s", path, line);
    } else {
        ESP_LOGI(BRINGUP_TAG, "opened %s (empty)", path);
    }
    fclose(f);
    return ESP_OK;
}
