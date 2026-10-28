/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "tx_engine.h"
#include "tx_seq.h"
#include "tx_fm.h"
#include "tx_grain.h"
#include "tx_fx.h"
#include "tx_mod.h"
#include "tx_scale.h"

#define FM_LEVEL 1.0f           /* the FM voice into the buffer and the dry path */
#define MIDI_OUT_LEN 96

static struct {
    float fs;
    tx_pstate ps;
    tx_seq seq;
    tx_grains grains;
    tx_fx fx;
    tx_mod mod;
    uint8_t edit;
    float L[TX_BLOCK], R[TX_BLOCK];
    int pos;                    /* frames of L/R already handed out */
    uint8_t midi_out[MIDI_OUT_LEN];
    int mo_len;
    float peak;
} E;

static void midi_push(uint8_t a, uint8_t b, uint8_t c)
{
    if (E.mo_len + 3 > MIDI_OUT_LEN) return;
    E.midi_out[E.mo_len++] = a;
    E.midi_out[E.mo_len++] = b;
    E.midi_out[E.mo_len++] = c;
}

void tx_init(float fs, uint32_t seed)
{
    memset(&E, 0, sizeof E);
    E.fs = fs;
    E.pos = TX_BLOCK;
    E.edit = TX_SNAP_A;
    txp_init(&E.ps);
    tx_seq_init(&E.seq, seed ^ 0x51u);
    tx_fm_init(fs);
    tx_grains_init(&E.grains, fs, seed ^ 0x6Au);
    tx_fx_init(&E.fx, fs);
    tx_mod_init(&E.mod, seed ^ 0x7Bu);
}

static void block(void)
{
    const int N = TX_BLOCK;
    tx_pstate *ps = &E.ps;
    float bpm = tx_seq_bpm(&E.seq, ps);

    txp_morph(ps);
    tx_mod_block(&E.mod, ps, bpm, (float)N / E.fs, E.fx.follower, E.seq.last_note, E.seq.last_vel);
    E.grains.scale_mask = tx_scale_mask(tx_int(ps, TXP_NOTE_SCALE), tx_int(ps, TXP_NOTE_CUSTOM));

    /* the sequencer plays the FM voice */
    tx_event ev[16];
    int ne = tx_seq_block(&E.seq, ps, E.fs, N, ev, 16);
    for (int i = 0; i < ne; i++) {
        if (ev[i].type == TX_EV_ON) {
            tx_fm_note_on(ps, ev[i].note, ev[i].vel);
            tx_grains_hit(&E.grains, ps);
            midi_push(0x90, ev[i].note, ev[i].vel);
        } else {
            tx_fm_note_off(ev[i].note);
            midi_push(0x80, ev[i].note, 0);
        }
    }

    float fm[TX_BLOCK], in[TX_BLOCK];
    memset(fm, 0, sizeof fm);
    tx_fm_render(ps, fm);
    for (int s = 0; s < N; s++) {
        fm[s] *= FM_LEVEL;
        in[s] = fm[s] + E.fx.fb[s];
    }

    float cl[TX_BLOCK], cr[TX_BLOCK];
    memset(cl, 0, sizeof cl);
    memset(cr, 0, sizeof cr);
    tx_grains_block(&E.grains, ps, in, cl, cr);

    float blend = tx_val(ps, TXP_OUT_BLEND) * 0.01f;
    for (int s = 0; s < N; s++) {
        E.L[s] = fm[s] * (1.0f - blend) + cl[s] * blend;
        E.R[s] = fm[s] * (1.0f - blend) + cr[s] * blend;
    }
    tx_fx_block(&E.fx, ps, bpm, E.L, E.R);

    float pk = 0.0f;
    for (int s = 0; s < N; s++) pk = fmaxf(pk, fmaxf(fabsf(E.L[s]), fabsf(E.R[s])));
    E.peak = fmaxf(pk, E.peak * 0.97f);
}

void tx_render(float *L, float *R, int n)
{
    while (n > 0) {
        if (E.pos >= TX_BLOCK) { block(); E.pos = 0; }
        int k = TX_BLOCK - E.pos;
        if (k > n) k = n;
        memcpy(L, E.L + E.pos, (size_t)k * sizeof *L);
        memcpy(R, E.R + E.pos, (size_t)k * sizeof *R);
        L += k; R += k; n -= k; E.pos += k;
    }
}

/* ------------------------------------------------------------ parameters */

void tx_set(int id, int raw) { txp_set(&E.ps, id, E.edit, raw); }

int tx_get(int id) { return txp_get(&E.ps, id, E.edit == TX_SNAP_B ? TX_SNAP_B : TX_SNAP_A); }
void tx_edit_snap(int snap) { E.edit = (uint8_t)snap; }
int tx_edit_snap_get(void) { return E.edit; }
void tx_copy_snap(int from, int to) { txp_copy(&E.ps, from, to); }
tx_pstate *tx_params(void) { return &E.ps; }

/* ------------------------------------------------------------ transport */

void tx_play(int on)
{
    if (on && !E.seq.running) { tx_seq_start(&E.seq); tx_mod_reset_phase(&E.mod); }
    if (!on && E.seq.running) tx_seq_stop(&E.seq);
}

int tx_playing(void) { return E.seq.running; }

void tx_freeze(int on) { txp_set(&E.ps, TXP_BUF_FREEZE, TX_SNAP_BOTH, on ? 1 : 0); }

void tx_note_on(int note, int vel)
{
    tx_fm_note_on(&E.ps, note, vel);
    tx_grains_hit(&E.grains, &E.ps);
}

void tx_note_off(int note) { tx_fm_note_off(note); }

void tx_panic(void)
{
    tx_seq_stop(&E.seq);
    tx_fm_all_off();
    tx_grains_clear(&E.grains);
    tx_fx_clear(&E.fx);
    E.mo_len = 0;
}

void tx_midi_in(const uint8_t *m, int len)
{
    if (len < 1) return;
    uint8_t st = m[0];
    if (st == 0xF8) { tx_seq_midi_clock(&E.seq); return; }
    if (st == 0xFA || st == 0xFB) { if (tx_int(&E.ps, TXP_SEQ_CLOCK) == 1) tx_play(1); return; }
    if (st == 0xFC) { if (tx_int(&E.ps, TXP_SEQ_CLOCK) == 1) tx_play(0); return; }
    if (len < 3) return;
    switch (st & 0xF0) {
    case 0x90:
        if (m[2]) tx_note_on(m[1], m[2]);
        else tx_note_off(m[1]);                         /* velocity 0 = note off */
        break;
    case 0x80: tx_note_off(m[1]); break;
    default: break;
    }
}

int tx_midi_out(uint8_t *buf, int cap)
{
    int n = E.mo_len < cap ? E.mo_len : cap;
    n -= n % 3;
    memcpy(buf, E.midi_out, (size_t)n);
    memmove(E.midi_out, E.midi_out + n, (size_t)(E.mo_len - n));
    E.mo_len -= n;
    return n;
}

/* ------------------------------------------------------------ patches */

size_t tx_patch_save(uint8_t *buf, size_t cap) { return txp_save(&E.ps, buf, cap); }

int tx_patch_load(const uint8_t *buf, size_t len)
{
    tx_pstate tmp;
    if (txp_load(&tmp, buf, len)) return -1;
    E.ps = tmp;
    return 0;
}

void tx_patch_init(void) { txp_init(&E.ps); }

/* ------------------------------------------------------------ live view */

void tx_get_status(tx_status *st)
{
    memset(st, 0, sizeof *st);
    st->playing = E.seq.running;
    st->frozen = (uint8_t)tx_int(&E.ps, TXP_BUF_FREEZE);
    st->step = (uint8_t)(E.seq.step < 0 ? 0 : E.seq.step);
    st->steps = (uint8_t)E.seq.steps;
    st->pattern = E.seq.pattern;
    st->head = tx_grains_head(&E.grains);
    st->ngrains = tx_grains_live(&E.grains, st->gpos, st->gamp, TX_LIVE_GRAINS);
    st->fm_voices = tx_fm_active();
    st->last_note = E.seq.last_note;
    st->last_vel = E.seq.last_vel;
    for (int i = 0; i < 10; i++) st->mod[i] = E.mod.src[i];
    st->peak = E.peak;
    st->bpm = tx_seq_bpm(&E.seq, &E.ps);
}

void tx_get_overview(uint8_t *out, int bins) { tx_grains_overview(&E.grains, out, bins); }
