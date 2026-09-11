#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-neutral. Core must not consume I2C or USB HID scancodes. */

typedef enum {
    TAB5_KEY_UNKNOWN = 0,
    TAB5_KEY_UP,
    TAB5_KEY_DOWN,
    TAB5_KEY_LEFT,
    TAB5_KEY_RIGHT,
    TAB5_KEY_ACCEPT,
    TAB5_KEY_CANCEL,
    TAB5_KEY_SPACE,
    TAB5_KEY_BACKSPACE,
    TAB5_KEY_QUIT,
} tab5_key_t;

typedef struct {
    tab5_key_t key;
    bool pressed;
    bool released;
    uint64_t timestamp_us;
} tab5_input_event_t;

#ifdef __cplusplus
}
#endif
