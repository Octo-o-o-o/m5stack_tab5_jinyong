/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tab5_platform.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#define KB_REG_INT_CFG   0x00
#define KB_REG_INT_STA   0x01
#define KB_REG_EVENT_NUM 0x02
#define KB_REG_MODE      0x10
#define KB_REG_HID       0x30
#define KB_REG_VERSION   0xF0
#define KB_MODE_HID      1

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static int64_t s_next_probe_us;
static int64_t s_next_poll_us;
static int s_read_fails;
static int s_logged_events;

/* The keyboard MCU pulls INT low while its event FIFO is non-empty. Reading
 * EVENT_NUM over I2C costs ~0.4 ms at 100 kHz and the game loop asks on every
 * SDL_PollEvent, so gate on the pin. The pin is only a fast path: a broken or
 * unwired INT degrades to a 30 ms poll instead of making the keyboard dead. */
#define KB_IDLE_POLL_US 30000

static esp_err_t kb_write_u8(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 80);
}

static esp_err_t kb_read(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 80);
}

static esp_err_t kb_bind_device(void)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dev != NULL) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg;
    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TAB5_KB_I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAB5_TAG, "keyboard add device failed: %s", esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    uint8_t ver = 0;
    if (kb_read(KB_REG_VERSION, &ver, 1) == ESP_OK) {
        ESP_LOGI(TAB5_TAG, "keyboard FW version register=0x%02x", ver);
    }

    err = kb_write_u8(KB_REG_MODE, KB_MODE_HID);
    if (err != ESP_OK) {
        ESP_LOGE(TAB5_TAG, "keyboard set HID mode failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Enable HID-mode INT; default 0x07 enables all three modes. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(kb_write_u8(KB_REG_INT_CFG, 0x02));
    ESP_LOGI(TAB5_TAG, "keyboard ready: I2C 0x6D HID mode");
    return ESP_OK;
}

static void kb_try_attach(void)
{
    if (s_dev != NULL || s_bus == NULL) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now < s_next_probe_us) {
        return;
    }
    s_next_probe_us = now + 1000000;
    if (i2c_master_probe(s_bus, TAB5_KB_I2C_ADDR, 200) != ESP_OK) {
        return;
    }
    if (kb_bind_device() == ESP_OK) {
        ESP_LOGI(TAB5_TAG, "keyboard attached (hotplug or late probe)");
    }
}

esp_err_t tab5_input_start(void)
{
    const gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << TAB5_KB_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&int_cfg));

    i2c_master_bus_config_t bus_cfg;
    memset(&bus_cfg, 0, sizeof(bus_cfg));
    bus_cfg.i2c_port = I2C_NUM_1;
    bus_cfg.sda_io_num = TAB5_KB_SDA;
    bus_cfg.scl_io_num = TAB5_KB_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAB5_TAG, "keyboard I2C bus G0/G1 failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(s_bus, TAB5_KB_I2C_ADDR, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAB5_TAG, "keyboard 0x6D not found yet (%s); will retry after attach",
                 esp_err_to_name(err));
        s_next_probe_us = esp_timer_get_time() + 500000;
        return ESP_OK;
    }
    return kb_bind_device();
}

int tab5_input_poll(tab5_hid_event_t *out, int cap)
{
    if (out == NULL || cap <= 0) {
        return 0;
    }
    kb_try_attach();
    if (s_dev == NULL) {
        return 0;
    }

    const int64_t now = esp_timer_get_time();
    if (gpio_get_level(TAB5_KB_INT) != 0 && now < s_next_poll_us) {
        return 0;
    }
    s_next_poll_us = now + KB_IDLE_POLL_US;

    uint8_t count = 0;
    if (kb_read(KB_REG_EVENT_NUM, &count, 1) != ESP_OK) {
        /* Unplugged, or the STM32 wedged. Keeping the bound device means every
         * later read fails too and the keyboard never comes back. Drop it and
         * let kb_try_attach() re-probe. */
        if (++s_read_fails >= 5) {
            ESP_LOGW(TAB5_TAG, "keyboard unresponsive; will re-probe");
            i2c_master_bus_rm_device(s_dev);
            s_dev = NULL;
            s_read_fails = 0;
            s_next_probe_us = esp_timer_get_time() + 500000;
        }
        return 0;
    }
    s_read_fails = 0;
    if (count == 0 || count > 32) {
        return 0;
    }

    int n = 0;
    for (uint8_t i = 0; i < count && n < cap; ++i) {
        uint8_t pkt[2] = {0xFF, 0xFF};
        if (kb_read(KB_REG_HID, pkt, sizeof(pkt)) != ESP_OK) {
            break;
        }
        if (pkt[0] == 0xFF && pkt[1] == 0xFF) {
            break;
        }
        out[n].modifier = pkt[0];
        out[n].keycode = pkt[1];
        out[n].pressed = (pkt[1] != 0);
        if (s_logged_events < 24) {
            ESP_LOGI(TAB5_TAG, "kb I2C-HID mod=0x%02x key=0x%02x %s",
                     pkt[0], pkt[1], out[n].pressed ? "press" : "release");
            ++s_logged_events;
        }
        ++n;
    }
    if (n > 0) {
        /* Release INT so the STM32 can raise the next event. */
        (void)kb_write_u8(KB_REG_INT_STA, 0);
    }
    return n;
}
