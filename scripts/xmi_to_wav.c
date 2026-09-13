/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Host-only: render AIL XMI to 16-bit stereo WAV via libADLMIDI (DosBox OPL). */
#include "adlmidi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void w16(FILE *f, uint16_t v)
{
    unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, f);
}

static void w32(FILE *f, uint32_t v)
{
    unsigned char b[4] = {
        (unsigned char)v, (unsigned char)(v >> 8),
        (unsigned char)(v >> 16), (unsigned char)(v >> 24),
    };
    fwrite(b, 1, 4, f);
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: xmi_to_wav <in.xmi> <out.wav> [rate=22050] [max_sec=45]\n");
        return 2;
    }
    const char *in = argv[1];
    const char *out_path = argv[2];
    const long rate = (argc >= 4) ? strtol(argv[3], NULL, 10) : 22050;
    const int max_sec = (argc >= 5) ? (int)strtol(argv[4], NULL, 10) : 45;
    if (rate < 8000 || rate > 48000 || max_sec < 1 || max_sec > 180) {
        fprintf(stderr, "bad rate/max_sec\n");
        return 2;
    }

    struct ADL_MIDIPlayer *p = adl_init(rate);
    if (!p) {
        fprintf(stderr, "adl_init failed\n");
        return 1;
    }
    adl_switchEmulator(p, ADLMIDI_EMU_DOSBOX);
    adl_setLoopEnabled(p, 0);
    if (adl_openFile(p, in) < 0) {
        fprintf(stderr, "adl_openFile failed: %s\n", in);
        adl_close(p);
        return 1;
    }

    const int chunk = 4096; /* samples, stereo interleaved */
    short *buf = (short *)malloc((size_t)chunk * sizeof(short));
    if (!buf) {
        adl_close(p);
        return 1;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        perror(out_path);
        free(buf);
        adl_close(p);
        return 1;
    }
    /* placeholder header */
    fwrite("RIFF", 1, 4, f);
    w32(f, 0);
    fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16);
    w16(f, 1);
    w16(f, 2);
    w32(f, (uint32_t)rate);
    w32(f, (uint32_t)rate * 4);
    w16(f, 4);
    w16(f, 16);
    fwrite("data", 1, 4, f);
    w32(f, 0);

    const int max_samples = (int)rate * 2 * max_sec;
    int written = 0;
    for (;;) {
        int want = chunk;
        if (written + want > max_samples) {
            want = max_samples - written;
        }
        if (want <= 0) {
            break;
        }
        const int got = adl_play(p, want, buf);
        if (got <= 0) {
            break;
        }
        fwrite(buf, sizeof(short), (size_t)got, f);
        written += got;
    }

    const uint32_t data_bytes = (uint32_t)written * 2u;
    fseek(f, 4, SEEK_SET);
    w32(f, 36 + data_bytes);
    fseek(f, 40, SEEK_SET);
    w32(f, data_bytes);
    fclose(f);
    free(buf);
    adl_close(p);
    printf("wrote %s (%d sec @ %ld Hz stereo)\n", out_path, written / (int)(rate * 2), rate);
    return written > 0 ? 0 : 1;
}
