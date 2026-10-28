/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the grain cloud.
 *
 * The FM voice (plus feedback) is written into a mono int16 ring buffer at half the engine rate.
 * Grains read from it: position is how far back from the write head (0 = newest), size, pitch,
 * window, direction, pan. Freeze stops writing so the last few seconds become the material.
 * Modes: Granular (free/hits/burst triggering), Pitch shift (overlapping grains right behind the
 * write head), Loop delay (a repeating region with crossfades). */
#ifndef TX_GRAIN_H
#define TX_GRAIN_H

#include <stdint.h>
#include "tx_config.h"
#include "tx_util.h"
#include "tx_param.h"

typedef struct {
    float pos;          /* read position in buffer samples */
    float inc;          /* buffer samples per output sample (negative = reverse) */
    float phase, dphase;/* 0..1 through the window */
    float gl, gr;       /* gains, pan and amplitude included */
    uint8_t active, win, win2;
    float wmorph, peak; /* window morph amount, skewed peak position */
    float ka, kb;       /* 0.5 / peak, 0.5 / (1 - peak): the skew without a divide per sample */
    float fade;         /* 1 while it plays; falls to 0 over 5 ms when the load governor ends it early */
    uint8_t dying;
    int32_t delay;      /* samples into the block before it starts */
} tx_grain;

typedef struct {
    int16_t buf[TX_BUF_LEN];
    uint32_t w;                 /* write index */
    uint32_t len;               /* active length in samples */
    float lp, dec_prev;         /* decimation filter */
    uint8_t dec_phase;
    tx_grain g[TX_MAX_GRAINS];
    float until_spawn;          /* output samples to the next grain (free / burst) */
    int burst_left;
    float burst_gap, until_burst;
    float scan;                 /* scan offset, 0..1 */
    int8_t scan_dir;
    float loop_until;           /* loop delay: samples to the next loop grain */
    tx_rng rng;
    float fs;
    uint16_t scale_mask;        /* from the sequencer, for pitch quantize */
    int nactive;
    int limit;                  /* the load governor's cap (0: none), below the Grain cap parameter */
    uint8_t trim;               /* fade out the grains above the limit now */
    int cap_now;                /* the cap in force at the last block */
} tx_grains;

void tx_grains_init(tx_grains *G, float fs, uint32_t seed);
void tx_grains_clear(tx_grains *G);
/* writes TX_BLOCK mono input samples (unless frozen) and adds the cloud to outL/outR */
void tx_grains_block(tx_grains *G, const tx_pstate *ps, const float *in, float *outL, float *outR);
void tx_grains_hit(tx_grains *G, const tx_pstate *ps);         /* a sequencer hit (Trigger = Hits) */
/* the load governor: at most n grains (0: the Grain cap alone); trim: fade out the ones above it now */
void tx_grains_limit(tx_grains *G, int n, int trim);

/* For the live view: write head and up to max grains as 0..1 positions in the buffer. */
float tx_grains_head(const tx_grains *G);
int   tx_grains_live(const tx_grains *G, float *pos, float *amp, int max);
/* peak per bin over the active buffer, 0..255, in buffer order (tx_grains_head marks the write position) */
void  tx_grains_overview(const tx_grains *G, uint8_t *out, int bins);

#endif
