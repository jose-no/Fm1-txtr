/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — render the engine to a WAV file, to hear changes without a device or a browser.
 *
 *   ./build/render out.wav [seconds] [patch.txtr]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tx_engine.h"

static void le32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void le16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s out.wav [seconds] [patch.txtr]\n", argv[0]); return 2; }
    const int fs = 44100;
    int secs = argc > 2 ? atoi(argv[2]) : 30;
    tx_init((float)fs, 1);
    if (argc > 3) {
        static uint8_t buf[8192];
        FILE *p = fopen(argv[3], "rb");
        if (!p) { perror(argv[3]); return 1; }
        size_t n = fread(buf, 1, sizeof buf, p);
        fclose(p);
        if (tx_patch_load(buf, n)) { fprintf(stderr, "%s: not a valid patch\n", argv[3]); return 1; }
    }
    tx_play(1);
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t frames = (uint32_t)(secs * fs);
    fwrite("RIFF", 1, 4, f); le32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16); le16(f, 1); le16(f, 2); le32(f, fs); le32(f, fs * 4); le16(f, 4); le16(f, 16);
    fwrite("data", 1, 4, f); le32(f, frames * 4);
    static float L[1024], R[1024];
    for (uint32_t done = 0; done < frames;) {
        int n = frames - done > 1024 ? 1024 : (int)(frames - done);
        tx_render(L, R, n);
        for (int i = 0; i < n; i++) {
            le16(f, (uint16_t)(int16_t)(L[i] * 32767.0f));
            le16(f, (uint16_t)(int16_t)(R[i] * 32767.0f));
        }
        done += (uint32_t)n;
    }
    fclose(f);
    printf("wrote %s (%d s)\n", argv[1], secs);
    return 0;
}
