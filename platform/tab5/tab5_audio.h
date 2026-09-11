#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* M1: stub. M4: preconverted PCM, not live MIDI. */

typedef struct {
    int sample_rate_hz;
    int channels;
} tab5_audio_fmt_t;

#ifdef __cplusplus
}
#endif
