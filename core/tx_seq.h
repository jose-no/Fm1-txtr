/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the generative sequencer.
 *
 * A Euclidean ring (steps, density = hits, rotate) advances at the Rate (16 bars .. 1/8 bar per
 * step). Each hit picks a note from the scale (random, walk, arp up/down, optionally weighted to
 * root and fifth). Drift delays each hit by up to half a step, loosens velocity and gate, and
 * above 60% lets the tempo itself wander. An optional second layer plays root/fifth drones. */
#ifndef TX_SEQ_H
#define TX_SEQ_H

#include <stdint.h>
#include "tx_util.h"
#include "tx_param.h"

#define TX_SEQ_PENDING 24      /* notes waiting for their drifted start or their note-off */

enum { TX_EV_ON = 1, TX_EV_OFF = 2 };

typedef struct {
    uint8_t type, note, vel, layer;   /* layer 0 = main, 1 = layer 2 */
} tx_event;

typedef struct {
    uint8_t used, on;      /* on: waiting for note-off; else waiting for note-on */
    uint8_t note, vel, layer;
    float wait;            /* samples until it fires */
    float gate;            /* samples it stays on, once started */
} tx_pending;

typedef struct {
    tx_rng rng;
    uint8_t running;
    int step;               /* last step played, -1 before the first */
    float until_step;       /* samples to the next step boundary */
    float wander;           /* tempo multiplier from drift, 1 = steady */
    int idx;                /* position in the note set (walk / arp) */
    int l2_step;            /* layer 2: its own step counter */
    int l2_div;             /* main steps since layer 2 last advanced */
    tx_pending pend[TX_SEQ_PENDING];
    /* external clock (MIDI, 24 per quarter) */
    uint32_t clk_count, clk_last;
    float clk_bpm;
    float fs;
    uint32_t now;           /* samples since start */
    /* for the UI and the mod matrix */
    uint32_t pattern;       /* bit i = step i is a hit */
    int steps;
    float last_note;        /* 0..1 within the note range */
    float last_vel;         /* 0..1 */
    uint8_t hit;            /* a hit started during the last block */
} tx_seq;

void tx_seq_init(tx_seq *s, uint32_t seed);
void tx_seq_start(tx_seq *s);
void tx_seq_stop(tx_seq *s);            /* releases what is sounding (events on the next block) */
void tx_seq_midi_clock(tx_seq *s);      /* one MIDI clock tick (0xF8) */
float tx_seq_bpm(const tx_seq *s, const tx_pstate *ps);

/* Runs n frames; writes up to max events (applied at the start of the block). Returns the count. */
int tx_seq_block(tx_seq *s, const tx_pstate *ps, float fs, int n, tx_event *ev, int max);

/* Euclidean rhythm: is step i a hit? (hits spread as evenly as possible, then rotated) */
static inline int tx_euclid(int steps, int hits, int rot, int i)
{
    if (steps <= 0 || hits <= 0) return 0;
    if (hits >= steps) return 1;
    int k = ((i - rot) % steps + steps) % steps;
    return (k * hits) % steps < hits;
}

#endif
