/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — modulation: 3 LFOs, S&H, random walk, envelope follower, plus the sequencer's
 * note, velocity and the drift knob, routed by an 8-slot matrix (source, target, amount, via).
 * Runs once per block and writes ps->eff from ps->base. */
#ifndef TX_MOD_H
#define TX_MOD_H

#include "tx_util.h"
#include "tx_param.h"

enum { MS_OFF, MS_LFO1, MS_LFO2, MS_LFO3, MS_SH, MS_WALK, MS_FOLLOW, MS_NOTE, MS_VEL, MS_DRIFT, MS_COUNT };

typedef struct {
    tx_rng rng;
    float ph[3];                /* LFO phases 0..1 */
    float rnd_a[3], rnd_b[3];   /* LFO S&H / smooth: previous and next random value */
    float sh_ph, sh_target, sh_out;
    float walk, walk_out;
    float fol;
    float src[MS_COUNT];        /* current value of every source (LFOs bipolar, the rest 0..1 or bipolar) */
} tx_mod;

void tx_mod_init(tx_mod *M, uint32_t seed);
void tx_mod_reset_phase(tx_mod *M);
/* dt = seconds per block; follower = output envelope; note/vel 0..1 from the sequencer */
void tx_mod_block(tx_mod *M, tx_pstate *ps, float bpm, float dt, float follower, float note, float vel);

#endif
