/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "tx_seq.h"
#include "tx_scale.h"

void tx_seq_init(tx_seq *s, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->rng.s = seed ? seed : 0x1234567u;
    s->wander = 1.0f;
    s->step = -1;
    s->clk_bpm = 120.0f;
}

void tx_seq_start(tx_seq *s)
{
    s->running = 1;
    s->step = -1;
    s->l2_step = -1;
    s->l2_div = 0;
    s->until_step = 0.0f;
    s->wander = 1.0f;
}

void tx_seq_stop(tx_seq *s)
{
    s->running = 0;
    for (int i = 0; i < TX_SEQ_PENDING; i++) {
        tx_pending *p = &s->pend[i];
        if (!p->used) continue;
        if (p->on) p->wait = 0.0f;          /* sounding: release now */
        else p->used = 0;                   /* not started: drop it */
    }
}

void tx_seq_midi_clock(tx_seq *s)
{
    /* tempo from the spacing of ticks, smoothed (ticks are seen at block resolution) */
    if (s->clk_count++ && s->fs > 0.0f) {
        uint32_t dt = s->now - s->clk_last;
        if (dt > 0) {
            float bpm = 60.0f * s->fs / ((float)dt * 24.0f);
            if (bpm > 20.0f && bpm < 400.0f) s->clk_bpm += (bpm - s->clk_bpm) * 0.08f;
        }
    }
    s->clk_last = s->now;
}

float tx_seq_bpm(const tx_seq *s, const tx_pstate *ps)
{
    if (tx_int(ps, TXP_SEQ_CLOCK) == 1) return s->clk_bpm;
    return (float)tx_int(ps, TXP_SEQ_BPM);
}

static void add(tx_seq *s, int note, int vel, int layer, float wait, float gate)
{
    for (int i = 0; i < TX_SEQ_PENDING; i++) {
        tx_pending *p = &s->pend[i];
        if (p->used) continue;
        p->used = 1; p->on = 0;
        p->note = (uint8_t)tx_clampi(note, 0, 127);
        p->vel = (uint8_t)tx_clampi(vel, 1, 127);
        p->layer = (uint8_t)layer;
        p->wait = wait; p->gate = gate;
        return;
    }
}

/* the note set: degrees of the scale over `range` octaves */
typedef struct { int8_t deg[12]; int m, total, base; } noteset;

static void make_set(noteset *ns, const tx_pstate *ps)
{
    uint16_t mask = tx_scale_mask(tx_int(ps, TXP_NOTE_SCALE), tx_int(ps, TXP_NOTE_CUSTOM));
    ns->m = tx_scale_degrees(mask, ns->deg);
    ns->total = ns->m * tx_int(ps, TXP_NOTE_RANGE);
    ns->base = 12 * (tx_int(ps, TXP_NOTE_OCTAVE) + 1) + tx_int(ps, TXP_NOTE_ROOT);
}

static int set_note(const noteset *ns, int idx) { return ns->base + 12 * (idx / ns->m) + ns->deg[idx % ns->m]; }

/* nearest index in the set that is the root or the fifth (or the root if no fifth) */
static int anchor(tx_seq *s, const noteset *ns, int near)
{
    int fifth = -1;
    for (int d = 0; d < ns->m; d++) if (ns->deg[d] == 7) fifth = d;
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < ns->total; i++) {
        int d = i % ns->m;
        if (d != 0 && d != fifth) continue;
        int dist = (i - near) * (i - near) * 4 + (int)(tx_rand(&s->rng) & 3);   /* ties broken at random */
        if (dist < bestd) { bestd = dist; best = i; }
    }
    return best;
}

static int pick(tx_seq *s, const tx_pstate *ps, const noteset *ns)
{
    int motion = tx_int(ps, TXP_NOTE_MOTION);
    int n = ns->total;
    if (s->idx >= n) s->idx = n - 1;
    switch (motion) {
    case 0: s->idx = (int)(tx_rand(&s->rng) % (uint32_t)n); break;
    case 1: {
        int w = tx_int(ps, TXP_NOTE_WALK);
        int d = (int)(tx_rand(&s->rng) % (uint32_t)(2 * w)) - w;
        if (d >= 0) d++;                                   /* -w..-1, 1..w: never stands still */
        s->idx += d;
        if (s->idx < 0) s->idx = -s->idx;                  /* bounce off the ends */
        if (s->idx >= n) s->idx = 2 * (n - 1) - s->idx;
        s->idx = tx_clampi(s->idx, 0, n - 1);
        break;
    }
    case 2: s->idx = (s->idx + 1) % n; break;
    default: s->idx = (s->idx - 1 + n) % n; break;
    }
    if (tx_randf(&s->rng) * 100.0f < tx_val(ps, TXP_NOTE_WEIGHT)) {
        int a = anchor(s, ns, s->idx);
        if (motion >= 2) return set_note(ns, a);           /* arps keep their position */
        s->idx = a;
    }
    return set_note(ns, s->idx);
}

int tx_seq_block(tx_seq *s, const tx_pstate *ps, float fs, int n, tx_event *ev, int max)
{
    int ne = 0;
    s->hit = 0;
    s->now += (uint32_t)n;
    s->fs = fs;

    int steps = tx_int(ps, TXP_SEQ_STEPS);
    int hits = tx_int(ps, TXP_SEQ_DENSITY);
    int rot = tx_int(ps, TXP_SEQ_ROTATE);
    float drift = tx_val(ps, TXP_SEQ_DRIFT) * 0.01f;
    s->steps = steps;
    s->pattern = 0;
    for (int i = 0; i < steps && i < 32; i++) if (tx_euclid(steps, hits, rot, i)) s->pattern |= 1u << i;

    if (s->running) {
        float bars = tx_val(ps, TXP_SEQ_RATE);
        float step_len = bars * 4.0f * 60.0f / tx_seq_bpm(s, ps) * fs;
        s->until_step -= (float)n;
        while (s->until_step <= 0.0f) {
            /* tempo wander above 60% drift */
            float w = drift > 0.6f ? (drift - 0.6f) / 0.4f : 0.0f;
            if (w > 0.0f) {
                s->wander += tx_randb(&s->rng) * 0.03f * w;
                s->wander = tx_clampf(s->wander, 1.0f - 0.15f * w, 1.0f + 0.15f * w);
            } else {
                s->wander += (1.0f - s->wander) * 0.2f;
            }
            float len = step_len / s->wander;
            s->until_step += len;
            s->step = (s->step + 1) % (steps > 0 ? steps : 1);

            noteset ns;
            make_set(&ns, ps);
            float gate_pct = tx_val(ps, TXP_SEQ_GATE) * 0.01f;
            float vel0 = (float)tx_int(ps, TXP_SEQ_VEL);

            if (tx_euclid(steps, hits, rot, s->step)) {
                int note = pick(s, ps, &ns);
                float late = tx_randf(&s->rng) * drift * 0.5f * len;
                float vel = vel0 * (1.0f + tx_randb(&s->rng) * 0.35f * drift);
                float gate = len * gate_pct * (1.0f + tx_randb(&s->rng) * 0.4f * drift);
                if (gate < 0.02f * fs) gate = 0.02f * fs;
                add(s, note, (int)vel, 0, late + s->until_step - len + (float)n, gate);
                s->last_note = ns.total > 1 ? (float)s->idx / (float)(ns.total - 1) : 0.0f;
                s->last_vel = tx_clampf(vel / 127.0f, 0.0f, 1.0f);
            }

            /* layer 2: root / fifth drones on a slower ring */
            int l2 = tx_int(ps, TXP_L2_MODE);
            if (l2) {
                int div = l2 == 1 ? 1 : l2 == 2 ? 2 : 4;
                if (++s->l2_div >= div) {
                    s->l2_div = 0;
                    int st2 = tx_int(ps, TXP_L2_STEPS);
                    s->l2_step = (s->l2_step + 1) % st2;
                    if (tx_euclid(st2, tx_int(ps, TXP_L2_DENSITY), 0, s->l2_step)) {
                        int fifth = 0;
                        for (int d = 0; d < ns.m; d++) if (ns.deg[d] == 7) fifth = 7;
                        int note = ns.base + 12 * tx_int(ps, TXP_L2_OCTAVE) + ((tx_rand(&s->rng) & 3) == 0 ? fifth : 0);
                        float l2len = len * (float)div;
                        float late = tx_randf(&s->rng) * drift * 0.5f * len;
                        add(s, note, (int)(vel0 * 0.85f), 1, late + s->until_step - len + (float)n, l2len * 0.9f);
                    }
                }
            }
        }
    }

    /* fire what is due during this block */
    for (int i = 0; i < TX_SEQ_PENDING && ne < max; i++) {
        tx_pending *p = &s->pend[i];
        if (!p->used) continue;
        p->wait -= (float)n;
        if (p->wait > 0.0f) continue;
        if (!p->on) {
            ev[ne++] = (tx_event){TX_EV_ON, p->note, p->vel, p->layer};
            s->hit = 1;
            p->on = 1;
            p->wait += p->gate;
            if (p->wait > 0.0f) continue;
            if (ne >= max) break;
        }
        ev[ne++] = (tx_event){TX_EV_OFF, p->note, 0, p->layer};
        p->used = 0;
    }
    return ne;
}
