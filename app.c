/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr on the M-VAVE FM-1: the panel, the screen and the patch slots around the engine (core/).
 * Part of fm1-base's unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are
 * in scope.
 *
 * The panel:
 *   SELECT        page: SEQ, NOTES, SOUND, GRAIN, SPACE, MOD
 *   ALGORITHM     sub-page of the page
 *   K1..K4        the four parameters on screen (SEL held: fine steps)
 *   PRESET        morph from snapshot A to B
 *   PLAY          start / stop the generator; held half a second: freeze
 *   REC           freeze on / off
 *   EDIT          edit snapshot A or B;  GLO held a second: copy A into B
 *   SEQ ARP ENV FX LFO   jump to SEQ, NOTES, SOUND (amp env), SPACE, MOD
 *   OCT- / OCT+   the generator's octave
 *   keys          the root note; SEL held: play notes by hand; on the Custom page: notes in or out of the scale
 *   SAVE + key    save the patch to slot 1..12;  HOME + key: load slot 1..12
 *
 * The render runs in the audio interrupt; the main loop talks to it through a small command ring, so nothing the
 * render reads is half-written. */
#include "fastmath.h"
#include "tx_engine.h"
#include "tx_param.h"

#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

/* ------------------------------------------------------------- commands to the render --- */
enum { CMD_PLAY = 1, CMD_FREEZE, CMD_NOTE_ON, CMD_NOTE_OFF, CMD_PANIC, CMD_APPLY, CMD_SET, CMD_SNAP, CMD_COPY };
typedef struct { uint8_t c, a; int16_t b; uint16_t id; } cmd_t;
#define CMDQ 64u
static cmd_t cmdq[CMDQ];
static volatile uint32_t cq_w, cq_r;
static tx_pstate apply_buf;              /* a patch loaded in the main loop, copied in by the render */
static volatile uint8_t apply_busy, muted;

static int post(uint8_t c, uint8_t a, int16_t b, uint16_t id)
{
    uint32_t w = cq_w, nx = (w + 1u) % CMDQ;
    if (nx == cq_r)
        return 0;                        /* full: dropped (the next frame sends the state again) */
    cmdq[w].c = c;
    cmdq[w].a = a;
    cmdq[w].b = b;
    cmdq[w].id = id;
    RING_PUBLISH();
    cq_w = nx;
    return 1;
}

static void drain(void)                  /* render: the main loop's commands */
{
    while (cq_r != cq_w) {
        cmd_t m = cmdq[cq_r];
        switch (m.c) {
        case CMD_PLAY: tx_play(m.a); break;
        case CMD_FREEZE: tx_freeze(m.a); break;
        case CMD_NOTE_ON: tx_note_on(m.a, m.b); break;
        case CMD_NOTE_OFF: tx_note_off(m.a); break;
        case CMD_PANIC: tx_panic(); break;
        case CMD_APPLY:
            *tx_params() = apply_buf;
            tx_params()->dirty = 1;
            apply_busy = 0;
            break;
        case CMD_SET: {
            int keep = tx_edit_snap_get();
            tx_edit_snap(m.a);
            tx_set(m.id, m.b);
            tx_edit_snap(keep);
            break;
        }
        case CMD_SNAP: tx_edit_snap(m.a); break;
        case CMD_COPY: tx_copy_snap(m.a, m.b); break;
        }
        RING_PUBLISH();
        cq_r = (cq_r + 1u) % CMDQ;
    }
}

/* ------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x52545854u            /* "TXTR" */
typedef struct {
    uint32_t magic;
    int8_t slot;                         /* the slot loaded last, -1 none */
    uint8_t rsv[3];
} settings_t;
static settings_t set;

/* ------------------------------------------------------------- audio --- */
static float bufL[APP_BLOCK], bufR[APP_BLOCK];
static float gain_now;
static volatile uint32_t midi_seen;

static void midi_in(void)                /* render: what came in over USB or the TRS jack */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint8_t m[3] = {(uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24)};
        uint8_t st = m[0];
        if (st >= 0xF8u) {
            if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
                tx_midi_in(m, 1);
            continue;
        }
        midi_seen++;
        if ((st & 0xE0u) == 0x80u)       /* note on / off, any channel */
            tx_midi_in(m, 3);
    }
}

static void midi_out(void)               /* render: the generated notes, out over USB */
{
    uint8_t b[48];
    int n = tx_midi_out(b, (int)sizeof b);
    for (int i = 0; i + 2 < n; i += 3) {
        uint32_t cin = b[i] >> 4;        /* USB-MIDI: the code index is the status's high nibble */
        plat_midi_send(cin | (uint32_t)b[i] << 8 | (uint32_t)b[i + 1] << 16 | (uint32_t)b[i + 2] << 24, TO_USB);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* 24-bit, MASTER read once a block */
    drain();
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    tx_load((int)plat_cpu_pct(), (int)n);   /* the load governor: fewer grains when the audio runs out of time */
    tx_render(bufL, bufR, (int)n);
    midi_out();
    for (uint32_t i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;
        float g = muted ? 0.0f : gain_now;
        lr[2 * i] = (int32_t)(bufL[i] * g);
        lr[2 * i + 1] = (int32_t)(bufR[i] * g);
    }
}

void app_panic(void)
{
    muted = 1;
    post(CMD_PANIC, 0, 0, 0);
}

void app_overload(void) { tx_overload(); }

/* ------------------------------------------------------------- pages --- */
/* a screen's worth of parameters: a sub-page of params.py, or a part of one (a sub-page with more than four
 * parameters, the operators and the mod matrix, comes in fours) */
#define MAX_VSUB 64
typedef struct {
    uint8_t page, sub, part, parts;
    int16_t id[4];                       /* -1: no parameter on that knob */
} vsub_t;
static vsub_t VS[MAX_VSUB];
static int nvs;
static int page_first[TX_NPAGES], page_count[TX_NPAGES], page_at[TX_NPAGES];

static void build_pages(void)
{
    nvs = 0;
    for (int s = 0; s < TX_NSUBS; s++) {
        int ids[64], n = 0;
        for (int i = 0; i < TX_NPARAMS && n < 64; i++)
            if (tx_pdefs[i].sub == s)
                ids[n++] = i;
        int parts = (n + 3) / 4;
        for (int p = 0; p < parts && nvs < MAX_VSUB; p++) {
            vsub_t *v = &VS[nvs++];
            v->page = tx_sub_page[s];
            v->sub = (uint8_t)s;
            v->part = (uint8_t)p;
            v->parts = (uint8_t)parts;
            for (int k = 0; k < 4; k++)
                v->id[k] = (int16_t)(p * 4 + k < n ? ids[p * 4 + k] : -1);
        }
    }
    for (int pg = 0; pg < TX_NPAGES; pg++) {
        page_first[pg] = -1;
        page_count[pg] = 0;
        page_at[pg] = 0;
    }
    for (int i = 0; i < nvs; i++) {
        int pg = VS[i].page;
        if (page_first[pg] < 0)
            page_first[pg] = i;
        page_count[pg]++;
    }
}

/* ------------------------------------------------------------- text --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void fix_text(float v, int dec, char *b)   /* v with dec decimals */
{
    int32_t scale = dec == 0 ? 1 : dec == 1 ? 10 : 100;
    int32_t x = (int32_t)fm_floorf(fm_fabsf(v) * (float)scale + 0.5f);
    char t[16];
    if (v < 0.0f && x)
        *b++ = '-';
    int_text(x / scale, t);
    b[0] = 0;
    text_cat(b, t);
    if (dec) {
        char f[4] = {'.', 0, 0, 0};
        int32_t r = x % scale;
        if (dec == 2) {
            f[1] = (char)('0' + r / 10);
            f[2] = (char)('0' + r % 10);
        } else {
            f[1] = (char)('0' + r);
        }
        text_cat(b, f);
    }
}

static int str_same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* a parameter's value as the screen shows it */
static void value_text(int id, int raw, char *b)
{
    const tx_pdef *d = &tx_pdefs[id];
    b[0] = 0;
    if (d->curve == TXC_ENUM) {
        text_cat(b, tx_plabels[id][raw]);
        return;
    }
    if (id == TXP_NOTE_CUSTOM) {         /* the notes in the custom scale */
        static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int n = 0;
        for (int k = 0; k < 12; k++)
            n += (raw >> k) & 1;
        int_text(n, b);
        text_cat(b, n == 1 ? " NOTE" : " NOTES");
        (void)N;
        return;
    }
    const char *u = "";
    float v = txp_phys(id, txp_norm(id, raw));
    /* the unit is the last word of the parameter's definition (params.py): read it from the curve */
    if (d->curve == TXC_INT) {
        if (id >= TXP_OP1_RATIO && id <= TXP_OP6_DETUNE && (id - TXP_OP1_RATIO) % 4 == 0 && raw == 0) {
            text_cat(b, "0.5");
            return;
        }
        int_text(raw, b);
        return;
    }
    /* continuous: by range */
    if (d->hi >= 1000.0f && (id == TXP_FLT_CUT || id == TXP_SP_FCUT || id == TXP_RV_LOWCUT)) {
        if (v >= 1000.0f) {
            fix_text(v / 1000.0f, v >= 10000.0f ? 0 : 1, b);
            u = " kHz";
        } else {
            fix_text(v, 0, b);
            u = " Hz";
        }
    } else if (id == TXP_AMP_A || id == TXP_AMP_D || id == TXP_AMP_R || id == TXP_GR_SIZE || id == TXP_GR_BURSTSPR ||
               id == TXP_EF_ATT || id == TXP_EF_REL || id == TXP_RV_PRE) {
        if (v >= 1000.0f) {
            fix_text(v / 1000.0f, 2, b);
            u = " s";
        } else {
            fix_text(v, v < 10.0f ? 1 : 0, b);
            u = " ms";
        }
    } else if (id == TXP_BUF_LEN || id == TXP_RV_DECAY) {
        fix_text(v, v < 10.0f ? 2 : 1, b);
        u = " s";
    } else if (id == TXP_GR_DENSITY) {
        fix_text(v, v < 10.0f ? 1 : 0, b);
        u = " /s";
    } else if (id == TXP_DL_MRATE || id == TXP_RND_SH || id == TXP_RND_WALK) {
        fix_text(v, v < 1.0f ? 2 : 1, b);
        u = " Hz";
    } else {                             /* percentages */
        fix_text(v, 0, b);
        u = "%";
    }
    text_cat(b, u);
}

/* ------------------------------------------------------------- UI state --- */
static struct {
    int page;                            /* 0..TX_NPAGES-1 */
    int snap;                            /* snapshot being edited: 0 A, 1 B */
    uint32_t keys, btn;
    uint32_t play_t;                     /* PLAY went down (0: not held, or its hold already acted) */
    uint32_t glo_t;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t redraw_top, redraw_knobs;
    uint32_t live_at, knobs_at;
    uint32_t held_notes;                 /* keys playing notes by hand (SEL held) */
    uint8_t note_of[NKEYS];
} ui;

static int cur_vsub(void) { return page_first[ui.page] + page_at[ui.page]; }

static int pget(int id) { return txp_get(tx_params(), id, ui.snap); }

static void pset(int id, int raw)
{
    const tx_pdef *d = &tx_pdefs[id];
    if (raw < d->rmin)
        raw = d->rmin;
    if (raw > d->rmax)
        raw = d->rmax;
    post(CMD_SET, (uint8_t)ui.snap, (int16_t)raw, (uint16_t)id);
    ui.redraw_knobs = 1;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw_top = 1;
    if (ann_on()) {
        char t[48] = "";
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* ------------------------------------------------------------- patches --- */
static uint8_t patch_io[TX_PATCH_MAX];

static void slot_text(int slot, char *t)
{
    t[0] = 'P';
    t[1] = (char)('0' + (slot + 1) / 10);
    t[2] = (char)('0' + (slot + 1) % 10);
    t[3] = 0;
}

static void patch_save(int slot)
{
    char t[8];
    size_t n = txp_save(tx_params(), patch_io, sizeof patch_io);
    slot_text(slot, t);
    if (n && plat_store_save(OBJ_PATCH0 + (uint32_t)slot, patch_io, (uint32_t)n) == 0) {
        set.slot = (int8_t)slot;
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
        app_say("SAVED", t);
    } else {
        app_say("SAVE FAILED", t);
    }
}

static int patch_load(int slot, int quiet)
{
    char t[8];
    int n = plat_store_load(OBJ_PATCH0 + (uint32_t)slot, patch_io, sizeof patch_io);
    slot_text(slot, t);
    if (apply_busy || n <= 0 || txp_load(&apply_buf, patch_io, (size_t)n) != 0) {
        if (!quiet)
            app_say("EMPTY SLOT", t);
        return -1;
    }
    apply_busy = 1;
    post(CMD_APPLY, 0, 0, 0);
    set.slot = (int8_t)slot;
    if (!quiet) {
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
        app_say("LOADED", t);
    }
    ui.redraw_knobs = ui.redraw_top = 1;
    return 0;
}

void app_init(void)
{
    tx_init(44100.0f, 0x7A11u);
    build_pages();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.slot < -1 || set.slot >= TXTR_SLOTS) {
        set.magic = SET_MAGIC;
        set.slot = -1;
    }
    if (set.slot >= 0 && patch_load(set.slot, 1) == 0)
        drain();                         /* (before the audio starts: apply now) */
}

/* ------------------------------------------------------------- input --- */
static void jump(int page, int sub_name_idx)
{
    ui.page = page;
    if (sub_name_idx >= 0)
        for (int i = 0; i < page_count[page]; i++)
            if (VS[page_first[page] + i].sub == sub_name_idx) {
                page_at[page] = i;
                break;
            }
    ui.redraw_top = ui.redraw_knobs = 1;
}

static int sub_index(const char *name)
{
    for (int s = 0; s < TX_NSUBS; s++)
        if (str_same(tx_subs[s], name))
            return s;
    return -1;
}

static void knob(int k, int32_t e, int fine)
{
    const vsub_t *v = &VS[cur_vsub()];
    int id = v->id[k];
    if (id < 0)
        return;
    const tx_pdef *d = &tx_pdefs[id];
    int step = 1;
    if (d->curve == TXC_LIN || d->curve == TXC_EXP)
        step = fine ? 1 : 10;
    else if (d->curve == TXC_INT && d->rmax - d->rmin > 100 && !fine)
        step = (d->rmax - d->rmin) / 100;
    if (id == TXP_NOTE_CUSTOM)           /* the custom scale is set on the keys */
        return;
    if (e > 1 || e < -1)                 /* a fast turn goes further */
        e *= 2;
    pset(id, pget(id) + e * step);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms();
    uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
    int shift = (btn >> B_SEL) & 1u;
    int32_t e;

    /* buttons */
    if (pressed >> B_PLAY & 1u)
        ui.play_t = now ? now : 1u;
    if (ui.play_t && (btn >> B_PLAY & 1u) && now - ui.play_t >= 500u) {   /* held: freeze */
        ui.play_t = 0;
        int fr = !tx_int(tx_params(), TXP_BUF_FREEZE);
        post(CMD_FREEZE, (uint8_t)fr, 0, 0);
        app_say("FREEZE", fr ? "ON" : "OFF");
    }
    if ((released >> B_PLAY & 1u) && ui.play_t) {
        ui.play_t = 0;
        int on = !tx_playing();
        muted = 0;
        post(CMD_PLAY, (uint8_t)on, 0, 0);
        app_say(on ? "PLAY" : "STOP", "");
    }
    if (pressed >> B_REC & 1u) {
        int fr = !tx_int(tx_params(), TXP_BUF_FREEZE);
        post(CMD_FREEZE, (uint8_t)fr, 0, 0);
        app_say("FREEZE", fr ? "ON" : "OFF");
    }
    if (pressed >> B_EDIT & 1u) {
        ui.snap ^= 1;
        post(CMD_SNAP, (uint8_t)ui.snap, 0, 0);
        app_say("EDITING", ui.snap ? "B" : "A");
        ui.redraw_knobs = 1;
    }
    if (pressed >> B_GLO & 1u)
        ui.glo_t = now ? now : 1u;
    if (ui.glo_t && (btn >> B_GLO & 1u) && now - ui.glo_t >= 1000u) {
        ui.glo_t = 0;
        post(CMD_COPY, 0, 1, 0);
        app_say("COPIED", "A TO B");
    }
    if (released >> B_GLO & 1u)
        ui.glo_t = 0;
    if (pressed >> B_SEQ & 1u)
        jump(0, -1);
    if (pressed >> B_ARP & 1u)
        jump(1, -1);
    if (pressed >> B_ENV & 1u)
        jump(2, sub_index("Amp env"));
    if (pressed >> B_FX & 1u)
        jump(4, -1);
    if (pressed >> B_LFO & 1u)
        jump(5, -1);
    if (pressed >> B_OCTDN & 1u)
        pset(TXP_NOTE_OCTAVE, pget(TXP_NOTE_OCTAVE) - 1);
    if (pressed >> B_OCTUP & 1u)
        pset(TXP_NOTE_OCTAVE, pget(TXP_NOTE_OCTAVE) + 1);

    /* keys */
    for (uint32_t k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int note = 53 + (int)k;          /* key 0 is F3 */
        if ((keys & bit) && !(ui.keys & bit)) {
            if ((btn >> B_SAVE & 1u) && k < TXTR_SLOTS)
                patch_save((int)k);
            else if ((btn >> B_HOME & 1u) && k < TXTR_SLOTS)
                patch_load((int)k, 0);
            else if (VS[cur_vsub()].id[0] == TXP_NOTE_CUSTOM) {
                int root = pget(TXP_NOTE_ROOT), bitn = ((note % 12) - root + 12) % 12;
                if (bitn)                /* the root stays in */
                    pset(TXP_NOTE_CUSTOM, pget(TXP_NOTE_CUSTOM) ^ (1 << bitn));
            } else if (shift) {
                int n = note + 12 * (pget(TXP_NOTE_OCTAVE) - 3);
                ui.note_of[k] = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
                ui.held_notes |= bit;
                muted = 0;
                post(CMD_NOTE_ON, ui.note_of[k], 100, 0);
            } else {
                static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                pset(TXP_NOTE_ROOT, note % 12);
                app_say("ROOT", N[note % 12]);
            }
        } else if (!(keys & bit) && (ui.keys & bit) && (ui.held_notes & bit)) {
            ui.held_notes &= ~bit;
            post(CMD_NOTE_OFF, ui.note_of[k], 0, 0);
        }
    }

    /* encoders */
    if ((e = plat_enc(EN_SELECT)) != 0) {
        ui.page = ((ui.page + (int)e) % TX_NPAGES + TX_NPAGES) % TX_NPAGES;
        ui.redraw_top = ui.redraw_knobs = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        int c = page_count[ui.page];
        page_at[ui.page] = ((page_at[ui.page] + (int)e) % c + c) % c;
        ui.redraw_top = ui.redraw_knobs = 1;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        int m = pget(TXP_MORPH) + (int)e * (shift ? 5 : 25);
        pset(TXP_MORPH, m);
        ui.redraw_top = 1;
    }
    for (int k = 0; k < 4; k++)
        if ((e = plat_enc(EN_K1 + k)) != 0)
            knob(k, e, shift);

    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds((tx_playing() ? 1u << B_PLAY : 0u) | (tx_int(tx_params(), TXP_BUF_FREEZE) ? 1u << B_REC : 0u) |
                  (ui.snap ? 1u << B_EDIT : 0u) | (btn & ((1u << B_SAVE) | (1u << B_HOME) | (1u << B_SEL))),
              keys);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------- drawing --- */
static const char *const PAGE_NAME[TX_NPAGES] = {"SEQ", "NOTES", "SOUND", "GRAIN", "SPACE", "MOD"};

static void draw_top(void)
{
    char t[32];
    uint32_t now = plat_ms();
    cv_begin(240, 30, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        const vsub_t *v = &VS[cur_vsub()];
        int32_t x = cv_text(8, 6, &FONT_B, PAGE_NAME[ui.page], C_WHITE);
        t[0] = 0;
        text_cat(t, tx_subs[v->sub]);
        if (v->parts > 1) {
            char n[8];
            int_text(v->part + 1, n);
            text_cat(t, " ");
            text_cat(t, n);
        }
        cv_text(x + 8, 10, &FONT_XS, t, C_AMB);
        /* right: the slot and the snapshot being edited; the morph as a bar under them */
        char ab[16] = "";
        if (set.slot >= 0) {
            slot_text(set.slot, ab);
            text_cat(ab, "  ");
        }
        text_cat(ab, ui.snap ? "EDIT B" : "EDIT A");
        cv_text(232 - text_w(&FONT_XS, ab), 3, &FONT_XS, ab, ui.snap ? C_HI : C_GRAY);
        int m = pget(TXP_MORPH) / 10;
        cv_rect(150, 23, 82, 3, C_LINE);
        cv_rect(150, 23, (82 * m) / 100, 3, C_HI);
    }
    cv_blit(0, 0);
}

static void draw_knobs(void)
{
    const vsub_t *v = &VS[cur_vsub()];
    const tx_pstate *ps = tx_params();
    char t[32];
    cv_begin(240, 124, C_BLACK);
    for (int k = 0; k < 4; k++) {
        int32_t x = 6 + (k % 2) * 118, y = 2 + (k / 2) * 62;
        int id = v->id[k];
        cv_text(x, y, &FONT_XS, k == 0 ? "K1" : k == 1 ? "K2" : k == 2 ? "K3" : "K4", C_DIM);
        if (id < 0)
            continue;
        cv_text(x + 20, y, &FONT_XS, tx_pnames[id], C_GRAY);
        int raw = pget(id);
        int bdiff = (tx_pdefs[id].flags & TXF_MORPH) && ps->snap[0][id] != ps->snap[1][id];
        value_text(id, raw, t);
        cv_text(x, y + 16, &FONT_B, t, bdiff ? C_WHITE : C_HI);
        /* the bar: the value, and where the mod matrix has moved it */
        int32_t bw = 108, by = y + 42;
        float n = txp_norm(id, raw), eff = ps->eff[id];
        cv_rect(x, by, bw, 5, C_LINE);
        if (tx_pdefs[id].curve != TXC_ENUM) {
            const tx_pdef *d = &tx_pdefs[id];
            if (d->curve == TXC_LIN && d->lo < 0.0f) {          /* bipolar: from the centre */
                int32_t c = x + bw / 2, p = x + (int32_t)(n * (float)bw);
                cv_rect(p < c ? p : c, by, p < c ? c - p : p - c, 5, C_HI);
            } else {
                cv_rect(x, by, (int32_t)(n * (float)bw), 5, C_HI);
            }
            if (fm_fabsf(eff - n) > 0.004f && !bdiff)
                cv_rect(x + (int32_t)(eff * (float)(bw - 2)), by - 2, 2, 9, C_WHITE);
        }
    }
    cv_blit(0, 32);
}

static void draw_live(void)
{
    tx_status st;
    static uint8_t ov[144];
    char t[24];
    tx_get_status(&st);
    tx_get_overview(ov, 144);
    cv_begin(240, 82, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    /* the Euclidean ring */
    int32_t cx = 40, cy = 42, r = 32;
    for (int s = 0; s < st.steps && s < 32; s++) {
        float a = -1.5707963f + (float)s / (float)st.steps * 6.2831853f;
        int32_t px = cx + (int32_t)(fm_cosf(a) * (float)r), py = cy + (int32_t)(fm_sinf(a) * (float)r);
        int hit = (st.pattern >> s) & 1u;
        uint16_t c = (s == st.step && st.playing) ? C_WHITE : hit ? C_HI : C_DIM;
        int32_t sz = hit ? 5 : 3;
        cv_rect(px - sz / 2, py - sz / 2, sz, sz, c);
    }
    int_text(tx_int(tx_params(), TXP_SEQ_DENSITY), t);
    cv_text(cx - text_w(&FONT_XS, t) / 2, cy - 6, &FONT_XS, t, C_GRAY);
    /* the buffer, its write head and the grains */
    int32_t bx = 88, bwid = 144, mid = 34;
    for (int i = 0; i < bwid; i++) {
        int32_t a = ov[i] * 27 / 255;
        if (a)
            cv_rect(bx + i, mid - a, 1, 2 * a, C_LINE);
    }
    cv_rect(bx + (int32_t)(st.head * (float)bwid), 6, 1, 56, st.frozen ? C_WHITE : C_AMB);
    for (int g = 0; g < st.ngrains; g++) {
        int32_t gx = bx + (int32_t)(st.gpos[g] * (float)bwid);
        int32_t gy = mid + ((g * 37) % 17 - 8) * 3;
        int32_t sz = 2 + (int32_t)(st.gamp[g] * 6.0f);
        if (sz > 6)
            sz = 6;
        cv_rect(gx - sz / 2, gy - sz / 2, sz, sz, C_HI);
    }
    /* the line under: play state, grains, load */
    t[0] = 0;
    text_cat(t, st.playing ? "PLAY" : "STOP");
    cv_text(bx, 65, &FONT_XS, t, st.playing ? C_HI : C_GRAY);
    if (st.frozen)
        cv_text(bx + 34, 65, &FONT_XS, "FROZEN", C_WHITE);
    char c[24] = "";
    if (st.glimited) {                   /* the governor is holding grains back: how many it allows */
        char n[8];
        text_cat(c, "AUTO ");
        int_text(st.gcap, n);
        text_cat(c, n);
        text_cat(c, "  ");
    }
    char p[8];
    int_text((int32_t)plat_cpu_pct(), p);
    text_cat(c, p);
    text_cat(c, "%");
    cv_text(232 - text_w(&FONT_XS, c), 65, &FONT_XS, c, st.glimited ? C_AMB : C_DIM);
    cv_blit(0, 158);
}

static void speak(void)
{
    if (ann_on() && ann_where()) {
        const vsub_t *v = &VS[cur_vsub()];
        char t[160] = "", s[24];
        text_cat(t, PAGE_NAME[ui.page]);
        text_cat(t, ". ");
        text_cat(t, tx_subs[v->sub]);
        for (int k = 0; k < 4; k++) {
            if (v->id[k] < 0)
                continue;
            text_cat(t, ". ");
            text_cat(t, tx_pnames[v->id[k]]);
            text_cat(t, " ");
            value_text(v->id[k], pget(v->id[k]), s);
            text_cat(t, s);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw_top = ui.redraw_knobs = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    speak();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw_top = 1;
    }
    if (ui.redraw_top) {
        ui.redraw_top = 0;
        draw_top();
    }
    if (ui.redraw_knobs || now - ui.knobs_at >= 120u) {   /* the mod markers move: a few times a second */
        ui.redraw_knobs = 0;
        ui.knobs_at = now;
        draw_knobs();
    }
    if (now - ui.live_at >= 66u) {
        ui.live_at = now;
        draw_live();
    }
}

/* ------------------------------------------------------------- the control page (SysEx) --- */
/* F0 7D 54 58 cmd data F7 (docs/PROTOCOL.md); p: the bytes between F0 and F7 */
static void sx_send(const uint8_t *body, uint32_t n)
{
    uint8_t f[24];
    uint32_t i = 0;
    f[i++] = 0xF0u;
    f[i++] = 0x7Du;
    f[i++] = 0x54u;
    f[i++] = 0x58u;
    for (uint32_t k = 0; k < n && i < 23u; k++)
        f[i++] = body[k] & 0x7Fu;
    f[i++] = 0xF7u;
    plat_sysex_send(f, i);
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x54u || p[2] != 0x58u)
        return 0;
    uint8_t cmd = p[3];
    if (cmd == 0x01u) {                  /* hello: version 1, the parameter count */
        uint8_t b[5] = {0x41u, 1u, (uint8_t)(TX_NPARAMS & 0x7F), (uint8_t)(TX_NPARAMS >> 7), 0};
        sx_send(b, 5);
    } else if (cmd == 0x02u && n >= 10u) {   /* set: id (2), snapshot (1), raw (3, signed, 7-bit groups) */
        int id = p[4] | p[5] << 7, snap = p[6];
        int32_t raw = (int32_t)(p[7] | p[8] << 7 | p[9] << 14);
        if (raw & (1 << 20))
            raw -= 1 << 21;
        if (id < TX_NPARAMS)
            post(CMD_SET, (uint8_t)(snap & 1), (int16_t)raw, (uint16_t)id);
        ui.redraw_knobs = 1;
    } else if (cmd == 0x03u) {           /* every value, A and B */
        for (int id = 0; id < TX_NPARAMS; id++)
            for (int s = 0; s < 2; s++) {
                int32_t raw = txp_get(tx_params(), id, s);
                uint32_t u = (uint32_t)raw & 0x1FFFFFu;
                uint8_t b[7] = {0x43u, (uint8_t)(id & 0x7F), (uint8_t)(id >> 7), (uint8_t)s,
                                (uint8_t)(u & 0x7F), (uint8_t)((u >> 7) & 0x7F), (uint8_t)(u >> 14)};
                sx_send(b, 7);
            }
    } else if (cmd == 0x09u && n >= 6u) {    /* play, freeze */
        post(CMD_PLAY, p[4] ? 1 : 0, 0, 0);
        post(CMD_FREEZE, p[5] ? 1 : 0, 0, 0);
    } else {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------- host checks --- */
#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "sub"))
        *got = page_at[ui.page];
    else if (!strcmp(what, "playing"))
        *got = tx_playing();
    else if (!strcmp(what, "frozen"))
        *got = tx_int(tx_params(), TXP_BUF_FREEZE);
    else if (!strcmp(what, "snap"))
        *got = ui.snap;
    else if (!strcmp(what, "slot"))
        *got = set.slot;
    else if (!strcmp(what, "root"))
        *got = txp_get(tx_params(), TXP_NOTE_ROOT, 0);
    else if (!strcmp(what, "density"))
        *got = txp_get(tx_params(), TXP_SEQ_DENSITY, 0);
    else if (!strcmp(what, "morph"))
        *got = txp_get(tx_params(), TXP_MORPH, 0);
    else if (!strcmp(what, "grains")) {
        tx_status st;
        tx_get_status(&st);
        *got = st.ngrains;
    } else if (!strcmp(what, "voices")) {
        tx_status st;
        tx_get_status(&st);
        *got = st.fm_voices;
    } else
        return 0;
    return 1;
}
#endif
