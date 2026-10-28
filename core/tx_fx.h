/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — SPACE: filter, color, delay, reverb (with shimmer), the feedback path back into the
 * grain buffer, and the output stage (width, limiter, level). All original code. */
#ifndef TX_FX_H
#define TX_FX_H

#include <stdint.h>
#include "tx_config.h"
#include "tx_util.h"
#include "tx_param.h"

#define TX_PS_LEN   1024        /* pitch shifter window (samples) */
#define TX_RV_LINES 4
#define TX_RV_MAX   3600        /* longest reverb line at Size 100% */
#define TX_RV_PRE   2400        /* pre-delay, int16 at half rate: ~100 ms */
#define TX_RV_AP    4

typedef struct {               /* two crossfaded taps on a short delay line */
    float buf[TX_PS_LEN];
    uint32_t w;
    float phase;
} tx_pshift;

typedef struct {
    float fs;
    /* filter + color */
    float f_ic1[2], f_ic2[2];
    float srr_phase, srr_hold[2];
    /* delay */
    int16_t dl[2][TX_DLY_LEN];
    uint32_t dl_w;
    uint8_t dl_phase;
    float dl_in_prev[2], dl_lp[2], dl_lfo;
    float dl_ap[2][256];
    uint32_t dl_ap_i;
    float dl_y[2];
    /* reverb */
    int16_t rv_pre[TX_RV_PRE];
    uint32_t rv_pre_w;
    uint8_t rv_pre_phase;
    float rv_pre_prev;
    float rv_hp_x, rv_hp_y;
    float rv_ap[TX_RV_AP][400];
    uint32_t rv_ap_i[TX_RV_AP];
    int16_t rv_line[TX_RV_LINES][TX_RV_MAX];
    uint32_t rv_w;
    float rv_damp[TX_RV_LINES];
    float rv_lfo;
    tx_pshift shimmer;
    /* feedback path */
    tx_pshift fb_ps;
    float fb_lp, fb_hp_x, fb_hp_y;
    float fb[TX_BLOCK];         /* what goes back into the buffer next block */
    /* output */
    float lim_env;
    float dc_x[2], dc_y[2];     /* DC blocker */
    float follower;             /* envelope of the output, for the mod matrix */
} tx_fx;

void tx_fx_init(tx_fx *F, float fs);
void tx_fx_clear(tx_fx *F);
/* processes TX_BLOCK stereo samples in place; bpm for synced delay times */
void tx_fx_block(tx_fx *F, const tx_pstate *ps, float bpm, float *L, float *R);

void  tx_pshift_init(tx_pshift *p);
float tx_pshift_run(tx_pshift *p, float x, float ratio);

#endif
