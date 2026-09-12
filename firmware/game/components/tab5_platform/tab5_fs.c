#include "tab5_platform.h"

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static bool s_mounted;

esp_err_t tab5_fs_start(void)
{
    /*
     * The BSP default is max_files = 5. The streaming BGM channel holds one
     * FILE* for as long as a track plays (two during a cross-fade), GRP loads
     * take two at a time, and a save takes more; five is close enough to the
     * edge that "too many open files" would show up as a random load failure.
     */
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024,
    };
    bsp_sdcard_cfg_t sd_cfg = {
        .mount = &mount_cfg,
    };
    const esp_err_t err = bsp_sdcard_sdmmc_mount(&sd_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAB5_TAG, "SD mount failed: %s (ok if no card)", esp_err_to_name(err));
        s_mounted = false;
        return err;
    }
    s_mounted = true;
    ESP_LOGI(TAB5_TAG, "SD mounted at %s", BSP_SD_MOUNT_POINT);
    /* HOJY opens save files with fopen("wb"); FatFS will not create the
     * directory, so a card prepared without save/ loses every save silently. */
    if (mkdir(TAB5_SD_GAME_ROOT "/save", 0777) != 0 && errno != EEXIST) {
        ESP_LOGW(TAB5_TAG, "cannot create %s/save (%s); saving may fail",
                 TAB5_SD_GAME_ROOT, strerror(errno));
    }
    return ESP_OK;
}

bool tab5_fs_ready(void)
{
    return s_mounted;
}

bool tab5_fs_has_game(void)
{
    if (!s_mounted) {
        return false;
    }
    FILE *cfg = fopen(TAB5_SD_GAME_ROOT "/config.toml", "r");
    if (cfg != NULL) {
        fclose(cfg);
        return true;
    }
    FILE *z = fopen(TAB5_SD_GAME_ROOT "/data/Z.DAT", "r");
    if (z != NULL) {
        fclose(z);
        return true;
    }
    return false;
}

esp_err_t tab5_fs_chdir_game(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    /* IDF FatFS is built with FF_FS_RPATH=0 and has no VFS chdir.
     * HOJY must open files via absolute /sdcard/jinyong/... paths. */
    if (chdir(TAB5_SD_GAME_ROOT) != 0) {
        ESP_LOGW(TAB5_TAG, "chdir %s unsupported (%s); using absolute paths",
                 TAB5_SD_GAME_ROOT, strerror(errno));
    } else {
        ESP_LOGI(TAB5_TAG, "cwd is %s", TAB5_SD_GAME_ROOT);
    }
    return ESP_OK;
}
