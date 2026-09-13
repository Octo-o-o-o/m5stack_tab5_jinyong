/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sdl_internal.h"

#include "esp_log.h"
#include "tab5_platform.h"

#define EVENT_CAP 64

#define HELD_CAP 8

static SDL_Event s_q[EVENT_CAP];
static int s_qhead;
static int s_qcount;
/*
 * The I2C HID protocol reports a release as {modifier, 0x00}: it does not say
 * WHICH key came up. Tracking a single "last key" loses a key whenever two
 * are overlapped, and a lost KEYUP leaves InputRepeater repeating that
 * direction forever (runaway walking). Track every key we reported as down and
 * release all of them on any release packet.
 */
static uint8_t s_held[HELD_CAP];
static int s_held_count;
static uint8_t s_last_mod;
/* SDL only delivers SDL_TEXTINPUT between StartTextInput/StopTextInput.
 * HOJY calls those around the new-game name prompt (Window::beginInput). */
static int s_text_input;

/*
 * USB HID usage id -> ASCII, unshifted / shifted. The I2C keyboard reports raw
 * usage ids, and SDL scancodes are defined to be those same ids, so this table
 * is indexed the same way sdl_input.cc indexes keyboardMap().
 * Keypad ids are deliberately absent: 0x59-0x63 are already bound to the
 * arrows / accept / cancel actions.
 * There is no IME on this device, so a name can only be ASCII.
 */
static char hid_to_ascii(uint8_t code, uint8_t mod)
{
    const int shift = (mod & 0x22) != 0;
    if ((mod & 0x11) || (mod & 0x44)) {
        return 0; /* Ctrl/Alt chords are not text */
    }
    if (code >= 0x04 && code <= 0x1D) {
        return (char)((shift ? 'A' : 'a') + (code - 0x04));
    }
    if (code >= 0x1E && code <= 0x27) {
        static const char digits[10] = {'1','2','3','4','5','6','7','8','9','0'};
        static const char shifted[10] = {'!','@','#','$','%','^','&','*','(',')'};
        return shift ? shifted[code - 0x1E] : digits[code - 0x1E];
    }
    switch (code) {
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return 0;
    }
}

static void push_event(const SDL_Event *ev)
{
    if (s_qcount >= EVENT_CAP) {
        return;
    }
    const int idx = (s_qhead + s_qcount) % EVENT_CAP;
    s_q[idx] = *ev;
    ++s_qcount;
}

static void push_key(uint32_t type, uint8_t scancode, uint8_t mod, int repeat)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.key.state = (type == SDL_KEYDOWN) ? 1 : 0;
    /* SDL marks auto-repeat as repeat=1 and SdlInputCollector drops those.
     * Reporting a held key's extra packets as fresh presses made one physical
     * Enter act twice: it confirmed the name AND answered the attribute
     * screen's 是, so the game started without the player choosing. */
    ev.key.repeat = (uint8_t)(repeat ? 1 : 0);
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.keysym.sym = scancode;
    ev.key.keysym.mod = mod;
    ev.key.timestamp = (Uint32)(tab5_clock_us() / 1000ULL);
    push_event(&ev);
}

static void release_all_held(uint8_t mod)
{
    for (int i = 0; i < s_held_count; ++i) {
        push_key(SDL_KEYUP, s_held[i], mod, 0);
    }
    s_held_count = 0;
}

void sdl_pump_keyboard(void)
{
    tab5_hid_event_t evs[16];
    const int n = tab5_input_poll(evs, 16);
    for (int i = 0; i < n; ++i) {
        if (evs[i].pressed) {
            const uint8_t code = evs[i].keycode;
            s_last_mod = evs[i].modifier;
            int already = 0;
            for (int k = 0; k < s_held_count; ++k) {
                if (s_held[k] == code) {
                    already = 1;
                    break;
                }
            }
            if (!already) {
                if (s_held_count >= HELD_CAP) {
                    /* Cannot track more; drop the oldest so we never leak a
                     * permanently-held key. */
                    push_key(SDL_KEYUP, s_held[0], s_last_mod, 0);
                    for (int k = 1; k < s_held_count; ++k) {
                        s_held[k - 1] = s_held[k];
                    }
                    --s_held_count;
                }
                s_held[s_held_count++] = code;
            }
            ESP_LOGD(TAB5_TAG, "SDL_KEYDOWN scan=0x%02x", (unsigned)code);
            push_key(SDL_KEYDOWN, code, evs[i].modifier, already);
            if (s_text_input) {
                const char ch = hid_to_ascii(code, evs[i].modifier);
                if (ch != 0) {
                    SDL_Event tev;
                    memset(&tev, 0, sizeof(tev));
                    tev.type = SDL_TEXTINPUT;
                    tev.text.timestamp = (Uint32)(tab5_clock_us() / 1000ULL);
                    tev.text.text[0] = ch;
                    push_event(&tev);
                }
            }
        } else {
            release_all_held(evs[i].modifier ? evs[i].modifier : s_last_mod);
        }
    }
}

void SDL_PumpEvents(void)
{
    sdl_pump_keyboard();
}

int SDL_PollEvent(SDL_Event *event)
{
    SDL_PumpEvents();
    if (s_qcount <= 0 || event == NULL) {
        return 0;
    }
    *event = s_q[s_qhead];
    s_qhead = (s_qhead + 1) % EVENT_CAP;
    --s_qcount;
    return 1;
}

void SDL_StartTextInput(void)
{
    s_text_input = 1;
}

void SDL_StopTextInput(void)
{
    s_text_input = 0;
}
void SDL_SetTextInputRect(const SDL_Rect *rect) { (void)rect; }

int SDL_GameControllerEventState(int state)
{
    (void)state;
    return 0;
}

SDL_GameController *SDL_GameControllerOpen(int index)
{
    (void)index;
    return NULL;
}

void SDL_GameControllerClose(SDL_GameController *gc)
{
    (void)gc;
}

SDL_GameController *SDL_GameControllerFromInstanceID(Sint32 joyid)
{
    (void)joyid;
    return NULL;
}
