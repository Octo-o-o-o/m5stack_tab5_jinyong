#include "bringup.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include <string.h>

/* Official published mapping: GT911 @ 0x14; ST712x touch @ 0x55.
 * FW 1 = ST7121, FW 3 = ST7123 (M5Stack / espp public notes).
 * This is probe-only. Display init still goes through the official BSP.
 */

static esp_err_t read_u8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, 1, 50);
}

void bringup_probe_panel(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(BRINGUP_TAG, "panel probe: BSP I2C handle is null");
        return;
    }

    const esp_err_t gt911 = i2c_master_probe(bus, 0x14, 50);
    const esp_err_t st = i2c_master_probe(bus, 0x55, 50);
    ESP_LOGI(BRINGUP_TAG, "panel probe GT911(0x14)=%s  ST712x(0x55)=%s",
             esp_err_to_name(gt911), esp_err_to_name(st));

    if (st != ESP_OK) {
        if (gt911 == ESP_OK) {
            ESP_LOGI(BRINGUP_TAG, "panel guess: ILI9881C + GT911");
        } else {
            ESP_LOGW(BRINGUP_TAG, "panel guess: unknown (no 0x14 / 0x55 ACK)");
        }
        return;
    }

    i2c_device_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x55;
    cfg.scl_speed_hz = 100000;

    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "panel probe: cannot add 0x55 device");
        return;
    }

    uint8_t fw = 0;
    const esp_err_t rd = read_u8(dev, 0x00, &fw);
    i2c_master_bus_rm_device(dev);

    if (rd != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "panel probe: 0x55 present, FW read failed (%s)",
                 esp_err_to_name(rd));
        return;
    }

    if (fw == 1) {
        ESP_LOGI(BRINGUP_TAG, "panel guess: ST7121 (FW %u). Official BSP 1.3.0 lists ili9881c+st7123 only.",
                 (unsigned)fw);
    } else if (fw == 3) {
        ESP_LOGI(BRINGUP_TAG, "panel guess: ST7123 (FW %u)", (unsigned)fw);
    } else {
        ESP_LOGW(BRINGUP_TAG, "panel guess: ST712x-like FW=%u (not 1 or 3)", (unsigned)fw);
    }
}
