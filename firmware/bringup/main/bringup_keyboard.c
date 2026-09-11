#include "bringup.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include <string.h>

/* Official protocol: Tab5_Keyboard-I2C-Protocol-EN-V1.0
 * Addr 0x6D. HID mode is I2C modifier+keycode, not USB Host HID.
 */

#define KB_REG_INT_CFG   0x00
#define KB_REG_EVENT_NUM 0x02
#define KB_REG_MODE      0x10
#define KB_REG_HID       0x30
#define KB_REG_VERSION   0xF0

#define KB_MODE_HID 1

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t kb_write_u8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

static esp_err_t kb_read(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 50);
}

esp_err_t bringup_keyboard_start(void)
{
    const gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << BRINGUP_KB_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&int_cfg));

    i2c_master_bus_config_t bus_cfg;
    memset(&bus_cfg, 0, sizeof(bus_cfg));
    bus_cfg.i2c_port = I2C_NUM_1;
    bus_cfg.sda_io_num = BRINGUP_KB_SDA;
    bus_cfg.scl_io_num = BRINGUP_KB_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(BRINGUP_TAG, "keyboard I2C bus G0/G1 failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(s_bus, BRINGUP_KB_I2C_ADDR, 100);
    if (err != ESP_OK) {
        ESP_LOGW(BRINGUP_TAG, "keyboard 0x6D not found on G0/G1 (%s). Is the kit attached?",
                 esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BRINGUP_KB_I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t ver = 0;
    if (kb_read(KB_REG_VERSION, &ver, 1) == ESP_OK) {
        ESP_LOGI(BRINGUP_TAG, "keyboard FW version register=0x%02x", ver);
    }

    err = kb_write_u8(KB_REG_MODE, KB_MODE_HID);
    if (err != ESP_OK) {
        ESP_LOGE(BRINGUP_TAG, "keyboard set HID mode failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(kb_write_u8(KB_REG_INT_CFG, 0x02));

    ESP_LOGI(BRINGUP_TAG, "keyboard ready: I2C 0x6D HID mode (not USB Host)");
    return ESP_OK;
}

void bringup_keyboard_poll(void)
{
    if (s_dev == NULL) {
        return;
    }

    uint8_t count = 0;
    if (kb_read(KB_REG_EVENT_NUM, &count, 1) != ESP_OK) {
        return;
    }
    if (count == 0 || count > 32) {
        return;
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t pkt[2] = {0xFF, 0xFF};
        if (kb_read(KB_REG_HID, pkt, sizeof(pkt)) != ESP_OK) {
            break;
        }
        if (pkt[0] == 0xFF && pkt[1] == 0xFF) {
            break;
        }
        const bool pressed = (pkt[1] != 0);
        ESP_LOGI(BRINGUP_TAG, "kb I2C-HID modifier=0x%02x keycode=0x%02x %s",
                 pkt[0], pkt[1], pressed ? "press" : "release");
    }
}
