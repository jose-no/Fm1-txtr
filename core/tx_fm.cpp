/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — FM voice on msfa. The way msfa is driven (static placement, no global
 * constructors, carrier silence check) follows Bubba Machine's core/bm_fm.cpp (GPL-3.0). */
#include <new>
#include <string.h>
#include <math.h>
#include "tx_config.h"
#include "tx_fm.h"
#include "tx_util.h"
#include "synth.h"
#include "sin.h"
#include "exp2.h"
#include "freqlut.h"
#include "env.h"
#include "pitchenv.h"
#include "porta.h"
#include "lfo.h"
#include "controllers.h"
#include "dx7note.h"
#include "EngineMsfa.h"

namespace {

enum { ENV_OFF, ENV_ATT, ENV_DEC, ENV_SUS, ENV_REL };

struct Voice {
    Dx7Note note;
    Lfo lfo;
    int32_t buf[_N_];
    uint8_t active, gate, key, vel, alg;
    uint32_t age;
    /* amp envelope */
    uint8_t stage;
    float level;
    /* post filter (TPT state variable) */
    float ic1, ic2;
    float gain;
};

alignas(8) unsigned char mem_voice[TX_FM_VOICES][sizeof(Voice)];
alignas(8) unsigned char mem_ctl[TX_FM_VOICES][sizeof(Controllers)];
alignas(8) unsigned char mem_core[sizeof(EngineMsfa)];
float g_fs = 44100.0f;
uint32_t g_clock;
int g_last_note = -1;
uint8_t g_patch[156];               /* last patch built (velocity 100), to see edits */
bool g_ready;

inline Voice &V(int i) { return *reinterpret_cast<Voice *>(mem_voice[i]); }
inline Controllers &C(int i) { return *reinterpret_cast<Controllers *>(mem_ctl[i]); }

/* level of every algorithm kept even: one carrier = 1, six ~ 0.4 */
float gain_for(int alg)
{
    static const float G[7] = {1.0f, 1.0f, 0.75f, 0.6f, 0.5f, 0.45f, 0.4f};
    uint8_t c = FmCore::get_carrier_operators((uint8_t)(alg & 31));
    int n = 0;
    for (int i = 0; i < 6; i++) n += (c >> i) & 1;
    return G[n];
}

/* unison spread per voice slot, as a fraction of the detune amount */
const float SPREAD[TX_FM_VOICES] = {0.0f, 1.0f, -1.0f, 0.6f, -0.6f, 0.3f};

int porta_of(const tx_pstate *ps)
{
    float g = tx_val(ps, TXP_FM_GLIDE);
    return g < 0.5f ? -1 : (int)(g * 1.27f);
}

}  // namespace

#ifdef TX_NO_CXXLIB
extern "C" void __cxa_pure_virtual(void) { for (;;) { } }
void operator delete(void *) noexcept { }
void operator delete(void *, size_t) noexcept { }
#endif

extern "C" {

void tx_fm_build_patch(const tx_pstate *ps, int vel, uint8_t p[156])
{
    memset(p, 0, 156);
    int alg = tx_int(ps, TXP_FM_ALG) - 1;
    uint8_t carriers = FmCore::get_carrier_operators((uint8_t)alg);
    int bright = (int)lrintf(tx_val(ps, TXP_FM_BRIGHT) * 0.3f);       /* +-30 output levels on modulators */
    const int stride = TXP_OP2_RATIO - TXP_OP1_RATIO;                 /* operator blocks are laid out alike */
    const int rstride = TXP_OP2_R1 - TXP_OP1_R1, lstride = TXP_OP2_L1 - TXP_OP1_L1, xstride = TXP_OP2_VEL - TXP_OP1_VEL;
    for (int n = 1; n <= 6; n++) {
        int d = (6 - n);                    /* VCED stores OP6 first */
        uint8_t *o = p + d * 21;
        for (int i = 0; i < 4; i++) {
            o[i] = (uint8_t)tx_int(ps, TXP_OP1_R1 + (n - 1) * rstride + i);
            o[4 + i] = (uint8_t)tx_int(ps, TXP_OP1_L1 + (n - 1) * lstride + i);
        }
        o[8] = 39;                          /* breakpoint C3, no level scaling */
        o[13] = (uint8_t)tx_int(ps, TXP_OP1_KRS + (n - 1) * xstride);
        o[14] = (uint8_t)tx_int(ps, TXP_OP1_AMS + (n - 1) * xstride);
        o[15] = (uint8_t)tx_int(ps, TXP_OP1_VEL + (n - 1) * xstride);
        int lvl = tx_int(ps, TXP_OP1_LEVEL + (n - 1) * stride);
        if (!(carriers & (1u << d))) lvl += bright;
        o[16] = (uint8_t)tx_clampi(lvl, 0, 99);
        o[17] = 0;                          /* ratio mode */
        o[18] = (uint8_t)tx_int(ps, TXP_OP1_RATIO + (n - 1) * stride);
        o[19] = (uint8_t)tx_int(ps, TXP_OP1_FINE + (n - 1) * stride);
        o[20] = (uint8_t)(tx_int(ps, TXP_OP1_DETUNE + (n - 1) * stride) + 7);
    }
    /* pitch envelope: from centre, to centre + amount at the attack rate, back at the decay rate */
    float amt = tx_val(ps, TXP_PENV_AMT) * 0.01f;
    amt *= 1.0f - tx_val(ps, TXP_PENV_VEL) * 0.01f * (1.0f - (float)vel / 127.0f);
    int lv = tx_clampi(50 + (int)lrintf(amt * 49.0f), 0, 99);
    p[126] = (uint8_t)tx_clampi(99 - (int)(tx_val(ps, TXP_PENV_ATT) * 0.99f), 0, 99);
    p[127] = (uint8_t)tx_clampi(99 - (int)(tx_val(ps, TXP_PENV_DEC) * 0.9f), 0, 99);
    p[128] = 99; p[129] = 99;
    p[130] = (uint8_t)lv; p[131] = 50; p[132] = 50; p[133] = 50;
    p[134] = (uint8_t)alg;
    p[135] = (uint8_t)tx_int(ps, TXP_FM_FB);
    p[136] = 1;                             /* oscillator key sync */
    p[137] = 30; p[138] = 0; p[139] = 0; p[140] = 40; p[141] = 0; p[142] = 0; p[143] = 0;   /* voice LFO: for AMS only */
    p[144] = (uint8_t)(24 + tx_int(ps, TXP_FM_TRANSPOSE));
    memcpy(p + 145, "TXTR      ", 10);
}

void tx_fm_init(float fs)
{
    g_fs = fs;
    if (!g_ready) {
        Exp2::init();
        Sin::init();
        Freqlut::init(fs);
        Lfo::init(fs);
        PitchEnv::init(fs);
        Env::init_sr(fs);
        Porta::init_sr(fs);
    }
    FmCore *core = new (mem_core) EngineMsfa();
    for (int i = 0; i < TX_FM_VOICES; i++) {
        Controllers *c = new (mem_ctl[i]) Controllers();
        c->values_[kControllerPitch] = 0x2000;
        c->values_[kControllerPitchRange] = 0;
        c->values_[kControllerPitchStep] = 0;
        c->values_[kControllerPortamentoGlissando] = 0;
        c->masterTune = 0;
        c->opSwitch = 0x3f;
        c->modwheel_cc = c->foot_cc = c->breath_cc = c->aftertouch_cc = 0;
        c->portamento_enable_cc = false;
        c->portamento_cc = 0;
        c->portamento_gliss_cc = false;
        c->core = core;
        c->refresh();
        Voice *v = new (mem_voice[i]) Voice();
        v->active = 0;
    }
    memset(g_patch, 0, sizeof g_patch);
    g_ready = true;
}

static void set_controls(const tx_pstate *ps, int i)
{
    Controllers &c = C(i);
    float cents = tx_val(ps, TXP_FM_DETUNE) * 0.15f * SPREAD[i];      /* up to +-15 cents */
    c.masterTune = (int32_t)(cents / 1200.0f * (float)(1 << 24));
    uint8_t sw = 0;
    for (int n = 1; n <= 6; n++)
        if (tx_int(ps, TXP_OP1_ON + (n - 1) * (TXP_OP2_ON - TXP_OP1_ON))) sw |= (uint8_t)(1u << (6 - n));
    c.opSwitch = sw;
}

void tx_fm_note_on(const tx_pstate *ps, int note, int vel)
{
    int nv = tx_clampi(tx_int(ps, TXP_FM_VOICES), 1, TX_FM_VOICES);
    int pick = -1;
    for (int i = 0; i < nv && pick < 0; i++) if (V(i).active && V(i).key == note) pick = i;   /* retrigger */
    for (int i = 0; i < nv && pick < 0; i++) if (!V(i).active) pick = i;
    if (pick < 0) {                         /* steal: the oldest released voice, else the oldest */
        uint32_t best = 0;
        for (int pass = 0; pass < 2 && pick < 0; pass++)
            for (int i = 0; i < nv; i++) {
                if (pass == 0 && V(i).gate) continue;
                uint32_t a = g_clock - V(i).age;
                if (pick < 0 || a > best) { best = a; pick = i; }
            }
    }
    Voice &v = V(pick);
    uint8_t patch[156];
    tx_fm_build_patch(ps, vel, patch);
    set_controls(ps, pick);
    int src = g_last_note >= 0 ? g_last_note : note;
    v.note.init(patch, note, vel, src, porta_of(ps), &C(pick));
    v.note.oscSync();
    v.lfo.reset(patch + 137);
    v.lfo.keydown();
    v.alg = patch[134];
    v.gain = gain_for(v.alg);
    v.key = (uint8_t)note;
    v.vel = (uint8_t)vel;
    v.gate = 1;
    v.age = g_clock;
    if (!v.active) { v.level = 0.0f; v.ic1 = v.ic2 = 0.0f; }
    v.stage = ENV_ATT;
    v.active = 1;
    g_last_note = note;
}

void tx_fm_note_off(int note)
{
    for (int i = 0; i < TX_FM_VOICES; i++) {
        Voice &v = V(i);
        if (v.active && v.gate && v.key == note) {
            v.gate = 0;
            v.stage = ENV_REL;
            v.note.keyup();
        }
    }
}

void tx_fm_all_off(void)
{
    for (int i = 0; i < TX_FM_VOICES; i++) {
        Voice &v = V(i);
        if (v.active && v.gate) { v.gate = 0; v.stage = ENV_REL; v.note.keyup(); }
    }
}

int tx_fm_active(void)
{
    int n = 0;
    for (int i = 0; i < TX_FM_VOICES; i++) n += V(i).active;
    return n;
}

static bool carriers_silent(Voice &v)
{
    VoiceStatus st;
    memset(&st, 0, sizeof st);
    v.note.peekVoiceStatus(st);
    uint8_t car = FmCore::get_carrier_operators(v.alg);
    for (int op = 0; op < 6; op++)
        if ((car & (1 << op)) && !(st.amp[op] <= VOICE_SILENCE_LEVEL && st.ampStep[op] == 4)) return false;
    return true;
}

void tx_fm_render(const tx_pstate *ps, float *out)
{
    g_clock++;
    const float n = (float)TX_BLOCK, fs = g_fs;

    /* live edits: rebuild the patch; voices sounding take it */
    uint8_t patch[156];
    tx_fm_build_patch(ps, 100, patch);
    bool edited = memcmp(patch, g_patch, sizeof patch) != 0;
    if (edited) memcpy(g_patch, patch, sizeof patch);

    float a_ms = tx_val(ps, TXP_AMP_A), d_ms = tx_val(ps, TXP_AMP_D), r_ms = tx_val(ps, TXP_AMP_R);
    float sus = tx_val(ps, TXP_AMP_S) * 0.01f;
    float att_step = n / (a_ms * 0.001f * fs);
    float dec_k = 1.0f - expf(-n * 4.6f / (d_ms * 0.001f * fs));       /* ~ -40 dB over the decay time */
    float rel_k = 1.0f - expf(-n * 6.9f / (r_ms * 0.001f * fs));       /* ~ -60 dB over the release time */
    float vs = tx_val(ps, TXP_FM_VELSENS) * 0.01f;
    int ftype = tx_int(ps, TXP_FLT_TYPE);
    float cut = tx_val(ps, TXP_FLT_CUT), res = tx_val(ps, TXP_FLT_RES) * 0.01f;
    float fenv = tx_val(ps, TXP_FLT_ENV) * 0.01f;
    float k = 2.0f - 1.94f * res;

    for (int i = 0; i < TX_FM_VOICES; i++) {
        Voice &v = V(i);
        if (!v.active) continue;
        if (edited) {
            uint8_t vp[156];
            tx_fm_build_patch(ps, v.vel, vp);
            set_controls(ps, i);
            v.alg = vp[134];
            v.gain = gain_for(v.alg);
            v.note.update(vp, v.key, v.vel, porta_of(ps), &C(i));
            v.lfo.reset(vp + 137);
        }
        /* envelope, once per block */
        float l0 = v.level;
        switch (v.stage) {
        case ENV_ATT: v.level += att_step; if (v.level >= 1.0f) { v.level = 1.0f; v.stage = ENV_DEC; } break;
        case ENV_DEC: v.level += (sus - v.level) * dec_k; if (fabsf(v.level - sus) < 1e-3f) v.stage = ENV_SUS; break;
        case ENV_SUS: v.level += (sus - v.level) * 0.05f; break;
        case ENV_REL: v.level -= v.level * rel_k; break;
        default: break;
        }
        if ((v.stage == ENV_REL && v.level < 1e-4f) || (!v.gate && carriers_silent(v))) {
            v.active = 0;
            continue;
        }
        memset(v.buf, 0, sizeof v.buf);
        v.note.compute(v.buf, v.lfo.getsample(), v.lfo.getdelay(), &C(i));

        float velg = 1.0f - vs * (1.0f - (float)v.vel / 127.0f);
        float g0 = l0 * velg * v.gain, g1 = v.level * velg * v.gain;
        float fc = cut * exp2f(fenv * v.level * 6.0f);
        fc = tx_clampf(fc, 20.0f, 0.45f * fs);
        float g = tanf((float)M_PI * fc / fs);
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float scale = 1.0f / (float)(1 << 24);     /* msfa: ~2^24 = a full-level carrier */
        for (int s = 0; s < TX_BLOCK; s++) {
            float x = (float)v.buf[s] * scale;
            float v3 = x - v.ic2;
            float v1 = a1 * v.ic1 + a2 * v3;
            float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2.0f * v1 - v.ic1;
            v.ic2 = 2.0f * v2 - v.ic2;
            float y = ftype == 0 ? v2 : ftype == 1 ? v1 * k : x - k * v1 - v2;
            float gg = g0 + (g1 - g0) * ((float)s * (1.0f / TX_BLOCK));
            out[s] += y * gg;
        }
    }
}

}  // extern "C"
