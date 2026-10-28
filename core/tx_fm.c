/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the FM voice: msfa's 6-operator DX7 engine in its integer C port (third_party/fm6/fm6_core.c,
 * Apache-2.0, from Felucca), plus a voice amp envelope, a post-FM filter per voice, unison detune, glide and
 * the brightness macro. The same code runs on the computer, in the browser and on the FM-1.
 *
 * Patches are built from the SOUND parameters every block; sounding voices take edits live (levels, ratios,
 * envelopes: as msfa's note update does). All memory is static. */
#include "tx_util.h"
#include "tx_config.h"
#include "tx_fm.h"

/* fm6_core.c's configuration: 32-sample blocks, one core */
#define CTL 32
#define FELUCCA_CPU2 0
#include "fm6_tables.h"
#include "fm6_core.c"

enum { ENV_OFF, ENV_ATT, ENV_DEC, ENV_SUS, ENV_REL };

typedef struct {
    fm6_note_t n;
    fm6_lfo_t lfo;
    uint8_t patch[FP_SIZE];
    int32_t buf[FM6_N];
    uint8_t active, gate, key, vel, alg;
    uint32_t age;
    uint8_t stage;
    float level;                /* amp envelope */
    float ic1, ic2;             /* post filter (TPT state variable) */
    float gain;
    float pitch, target;        /* in semitones (MIDI note numbers), for glide */
} voice_t;

static voice_t V[TX_FM_VOICES];
static float g_fs = 44100.0f;
static uint32_t g_clock;
static float g_last_pitch = -1.0f;
static uint8_t g_patch[FP_SIZE];          /* last patch built (velocity 100), to see edits */

/* unison spread per voice slot, as a fraction of the detune amount */
static const float SPREAD[TX_FM_VOICES] = {0.0f, 1.0f, -1.0f, 0.6f, -0.6f, 0.3f};

/* level kept even across algorithms: one carrier = 1, six ~ 0.4 */
static float gain_for(int alg)
{
    static const float G[7] = {1.0f, 1.0f, 0.75f, 0.6f, 0.5f, 0.45f, 0.4f};
    uint32_t c = fm6_carriers((uint32_t)alg);
    int n = 0;
    for (int i = 0; i < 6; i++) n += (int)((c >> i) & 1u);
    return G[n];
}

void tx_fm_build_patch(const tx_pstate *ps, int vel, uint8_t p[FP_SIZE])
{
    memset(p, 0, FP_SIZE);
    const int stride = TXP_OP2_RATIO - TXP_OP1_RATIO;     /* operator blocks are laid out alike */
    const int rstride = TXP_OP2_R1 - TXP_OP1_R1, lstride = TXP_OP2_L1 - TXP_OP1_L1, xstride = TXP_OP2_VEL - TXP_OP1_VEL;
    for (int n = 1; n <= 6; n++) {
        uint8_t *o = p + (6 - n) * FP_OP;                 /* the sixth operator first */
        for (int i = 0; i < 4; i++) {
            o[FP_R1 + i] = (uint8_t)tx_int(ps, TXP_OP1_R1 + (n - 1) * rstride + i);
            o[FP_L1 + i] = (uint8_t)tx_int(ps, TXP_OP1_L1 + (n - 1) * lstride + i);
        }
        o[FP_BP] = 39;                                    /* breakpoint C3, no level scaling */
        o[FP_RS] = (uint8_t)tx_int(ps, TXP_OP1_KRS + (n - 1) * xstride);
        o[FP_AMS] = (uint8_t)tx_int(ps, TXP_OP1_AMS + (n - 1) * xstride);
        o[FP_KVS] = (uint8_t)tx_int(ps, TXP_OP1_VEL + (n - 1) * xstride);
        int on = tx_int(ps, TXP_OP1_ON + (n - 1) * xstride);
        o[FP_OL] = on ? (uint8_t)tx_int(ps, TXP_OP1_LEVEL + (n - 1) * stride) : 0;
        o[FP_MODE] = 0;                                   /* ratio */
        o[FP_FC] = (uint8_t)tx_int(ps, TXP_OP1_RATIO + (n - 1) * stride);
        o[FP_FF] = (uint8_t)tx_int(ps, TXP_OP1_FINE + (n - 1) * stride);
        o[FP_DET] = (uint8_t)(tx_int(ps, TXP_OP1_DETUNE + (n - 1) * stride) + 7);
    }
    /* pitch envelope: from centre to centre + amount at the attack rate, back at the decay rate */
    float amt = tx_val(ps, TXP_PENV_AMT) * 0.01f;
    amt *= 1.0f - tx_val(ps, TXP_PENV_VEL) * 0.01f * (1.0f - (float)vel / 127.0f);
    int lv = tx_clampi(50 + (int)lrintf(amt * 49.0f), 0, 99);
    p[FP_PR1] = (uint8_t)tx_clampi(99 - (int)(tx_val(ps, TXP_PENV_ATT) * 0.99f), 0, 99);
    p[FP_PR1 + 1] = (uint8_t)tx_clampi(99 - (int)(tx_val(ps, TXP_PENV_DEC) * 0.9f), 0, 99);
    p[FP_PR1 + 2] = 99; p[FP_PR1 + 3] = 99;
    p[FP_PL1] = (uint8_t)lv; p[FP_PL1 + 1] = 50; p[FP_PL1 + 2] = 50; p[FP_PL1 + 3] = 50;
    p[FP_ALG] = (uint8_t)(tx_int(ps, TXP_FM_ALG) - 1);
    p[FP_FB] = (uint8_t)tx_int(ps, TXP_FM_FB);
    p[FP_OKS] = 1;
    p[FP_LFS] = 30; p[FP_LFD] = 0; p[FP_LPMD] = 0; p[FP_LAMD] = 40; p[FP_LKS] = 0; p[FP_LFW] = 0; p[FP_LPMS] = 0;
    p[FP_TRNSP] = 24;
    memcpy(p + FP_NAME, "TXTR      ", 10);
}

/* msfa's note update: new levels, rates and pitches on a sounding note, its envelopes where they are */
static void note_update(fm6_note_t *n, const uint8_t *p, int note, int vel)
{
    note = tx_clampi(note, 0, 127);
    n->fixed = 0;
    for (uint32_t k = 0; k < 6u; k++) {
        const uint8_t *op = p + k * FP_OP;
        int32_t ol = fm6_scaleout(op[FP_OL]) + fm6_scale_level(note, op);
        ol = (ol > 127 ? 127 : ol) << 5;
        ol += fm6_scale_vel(vel, op[FP_KVS]);
        n->env[k].outlevel = (int16_t)(ol < 0 ? 0 : ol);
        n->env[k].rscale = (uint8_t)fm6_scale_rate(note, op[FP_RS]);
        n->base[k] = fm6_osc_base(op, note, 0);
        n->fixed |= (uint8_t)((op[FP_MODE] & 1u) << k);
    }
}

void tx_fm_init(float fs)
{
    g_fs = fs;
    memset(V, 0, sizeof V);
    memset(g_patch, 0, sizeof g_patch);
    g_last_pitch = -1.0f;
}

static float detune_cents(const tx_pstate *ps, int i) { return tx_val(ps, TXP_FM_DETUNE) * 0.15f * SPREAD[i]; }

void tx_fm_note_on(const tx_pstate *ps, int note, int vel)
{
    int nv = tx_clampi(tx_int(ps, TXP_FM_VOICES), 1, TX_FM_VOICES);
    int pick = -1;
    for (int i = 0; i < nv && pick < 0; i++) if (V[i].active && V[i].key == note) pick = i;   /* retrigger */
    for (int i = 0; i < nv && pick < 0; i++) if (!V[i].active) pick = i;
    if (pick < 0) {                         /* steal: the oldest released voice, else the oldest */
        uint32_t best = 0;
        for (int pass = 0; pass < 2 && pick < 0; pass++)
            for (int i = 0; i < nv; i++) {
                if (pass == 0 && V[i].gate) continue;
                uint32_t a = g_clock - V[i].age;
                if (pick < 0 || a > best) { best = a; pick = i; }
            }
    }
    voice_t *v = &V[pick];
    int fresh = !v->active;
    int tr = tx_int(ps, TXP_FM_TRANSPOSE);
    int played = tx_clampi(note + tr, 0, 127);
    tx_fm_build_patch(ps, vel, v->patch);
    fm6_note_init(&v->n, v->patch, played, vel, fresh);
    fm6_lfo_reset(&v->lfo, v->patch);
    fm6_lfo_key(&v->lfo);
    v->alg = v->patch[FP_ALG];
    v->gain = gain_for(v->alg);
    v->key = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->age = g_clock;
    v->target = (float)played;
    if (tx_val(ps, TXP_FM_GLIDE) > 0.5f && g_last_pitch >= 0.0f) v->pitch = g_last_pitch;
    else v->pitch = v->target;
    if (fresh) { v->level = 0.0f; v->ic1 = v->ic2 = 0.0f; }
    v->stage = ENV_ATT;
    v->active = 1;
    g_last_pitch = v->target;
}

void tx_fm_note_off(int note)
{
    for (int i = 0; i < TX_FM_VOICES; i++) {
        voice_t *v = &V[i];
        if (v->active && v->gate && v->key == note) {
            v->gate = 0;
            v->stage = ENV_REL;
            fm6_note_key(&v->n, v->patch, 0);
        }
    }
}

void tx_fm_all_off(void)
{
    for (int i = 0; i < TX_FM_VOICES; i++) {
        voice_t *v = &V[i];
        if (v->active && v->gate) { v->gate = 0; v->stage = ENV_REL; fm6_note_key(&v->n, v->patch, 0); }
    }
}

int tx_fm_active(void)
{
    int n = 0;
    for (int i = 0; i < TX_FM_VOICES; i++) n += V[i].active;
    return n;
}

void tx_fm_render(const tx_pstate *ps, float *out)
{
    g_clock++;
    const float fs = g_fs;

    /* live edits: rebuild the patch; voices sounding take it */
    uint8_t patch[FP_SIZE];
    tx_fm_build_patch(ps, 100, patch);
    int edited = memcmp(patch, g_patch, FP_SIZE) != 0;
    if (edited) memcpy(g_patch, patch, FP_SIZE);

    float a_ms = tx_val(ps, TXP_AMP_A), d_ms = tx_val(ps, TXP_AMP_D), r_ms = tx_val(ps, TXP_AMP_R);
    float sus = tx_val(ps, TXP_AMP_S) * 0.01f;
    const float n = (float)TX_BLOCK;
    float att_step = n / (a_ms * 0.001f * fs);
    float dec_k = 1.0f - expf(-n * 4.6f / (d_ms * 0.001f * fs));       /* ~ -40 dB over the decay time */
    float rel_k = 1.0f - expf(-n * 6.9f / (r_ms * 0.001f * fs));       /* ~ -60 dB over the release time */
    float vs = tx_val(ps, TXP_FM_VELSENS) * 0.01f;
    int ftype = tx_int(ps, TXP_FLT_TYPE);
    float cut = tx_val(ps, TXP_FLT_CUT), res = tx_val(ps, TXP_FLT_RES) * 0.01f;
    float fenv = tx_val(ps, TXP_FLT_ENV) * 0.01f;
    float k = 2.0f - 1.94f * res;
    /* brightness: the modulators' level, +-24 dB (Q24 log2: 2^24 = 6 dB) */
    int32_t modlvl = (int32_t)(tx_val(ps, TXP_FM_BRIGHT) * 0.01f * 4.0f * 16777216.0f);
    float glide = tx_val(ps, TXP_FM_GLIDE) * 0.01f;
    float gk = glide > 0.005f ? 1.0f - expf(-(float)FM6_N / (glide * glide * 2.0f * fs)) : 1.0f;
    int tr = tx_int(ps, TXP_FM_TRANSPOSE);
    static const int32_t dt0[6] = {0, 0, 0, 0, 0, 0};

    for (int i = 0; i < TX_FM_VOICES; i++) {
        voice_t *v = &V[i];
        if (!v->active) continue;
        if (edited) {
            tx_fm_build_patch(ps, v->vel, v->patch);
            note_update(&v->n, v->patch, tx_clampi(v->key + tr, 0, 127), v->vel);
            fm6_lfo_reset(&v->lfo, v->patch);
            v->alg = v->patch[FP_ALG];
            v->gain = gain_for(v->alg);
            v->target = (float)tx_clampi(v->key + tr, 0, 127);
        }
        /* amp envelope, once per block */
        float l0 = v->level;
        switch (v->stage) {
        case ENV_ATT: v->level += att_step; if (v->level >= 1.0f) { v->level = 1.0f; v->stage = ENV_DEC; } break;
        case ENV_DEC: v->level += (sus - v->level) * dec_k; if (fabsf(v->level - sus) < 1e-3f) v->stage = ENV_SUS; break;
        case ENV_SUS: v->level += (sus - v->level) * 0.05f; break;
        case ENV_REL: v->level -= v->level * rel_k; break;
        default: break;
        }
        if ((v->stage == ENV_REL && v->level < 1e-4f) || (!v->gate && fm6_note_done(&v->n, v->patch, v->alg))) {
            v->active = 0;
            continue;
        }

        float velg = 1.0f - vs * (1.0f - (float)v->vel / 127.0f);
        float g0 = l0 * velg * v->gain, g1 = v->level * velg * v->gain;
        float fc = tx_clampf(cut * exp2f(fenv * v->level * 6.0f), 20.0f, 0.45f * fs);
        float g = tanf(TX_PI * fc / fs);
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float scale = 1.0f / 16777216.0f;           /* Q24: a full-level carrier */
        int32_t fine = (int32_t)(detune_cents(ps, i) * 2.366f);   /* 1 + fine / 4096 */

        for (int half = 0; half < TX_BLOCK / FM6_N; half++) {
            v->pitch += (v->target - v->pitch) * gk;
            int32_t lfo_val = fm6_lfo_sample(&v->lfo), lfo_delay = fm6_lfo_delay(&v->lfo);
            int32_t logfreq = fm6_note_logfreq((int32_t)lrintf(v->pitch * 16.0f), fine);
            if (!fm6_note_compute(&v->n, v->patch, v->buf, lfo_val, lfo_delay, logfreq, v->alg, v->patch[FP_FB], dt0, modlvl))
                memset(v->buf, 0, sizeof v->buf);
            float *o = out + half * FM6_N;
            for (int s = 0; s < FM6_N; s++) {
                float x = (float)v->buf[s] * scale;
                float v3 = x - v->ic2;
                float v1 = a1 * v->ic1 + a2 * v3;
                float v2 = v->ic2 + a2 * v->ic1 + a3 * v3;
                v->ic1 = 2.0f * v1 - v->ic1;
                v->ic2 = 2.0f * v2 - v->ic2;
                float y = ftype == 0 ? v2 : ftype == 1 ? v1 * k : x - k * v1 - v2;
                float t = (float)(half * FM6_N + s) * (1.0f / TX_BLOCK);
                o[s] += y * (g0 + (g1 - g0) * t);
            }
        }
    }
}
