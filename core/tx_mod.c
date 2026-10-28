/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "tx_mod.h"

void tx_mod_init(tx_mod *M, uint32_t seed)
{
    memset(M, 0, sizeof *M);
    M->rng.s = seed ? seed : 0xBADA55u;
    for (int i = 0; i < 3; i++) { M->rnd_a[i] = tx_randb(&M->rng); M->rnd_b[i] = tx_randb(&M->rng); }
}

void tx_mod_reset_phase(tx_mod *M)
{
    for (int i = 0; i < 3; i++) M->ph[i] = 0.0f;
}

static float rate_hz(float v, float bpm)
{
    if (v > 0.0f) return v;
    return bpm / (-v * 4.0f * 60.0f);       /* synced: -bars per cycle */
}

static float lfo_shape(tx_mod *M, int i, int shape, float ph)
{
    switch (shape) {
    case 0: return sinf(TX_TWO_PI * ph);
    case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case 2: return 2.0f * ph - 1.0f;
    case 3: return 1.0f - 2.0f * ph;
    case 4: return ph < 0.5f ? 1.0f : -1.0f;
    case 5: return M->rnd_b[i];
    default: {
        float t = 0.5f - 0.5f * cosf((float)M_PI * ph);
        return M->rnd_a[i] + (M->rnd_b[i] - M->rnd_a[i]) * t;
    }
    }
}

void tx_mod_block(tx_mod *M, tx_pstate *ps, float bpm, float dt, float follower, float note, float vel)
{
    /* the sources read the morphed base values (modulating the modulators is not supported) */
    const float *b = ps->base;
#define BV(id) txp_phys(id, b[id])
#define BI(id) txp_raw(id, b[id])

    static const int LFO_P[3][4] = {
        {TXP_LFO1_SHAPE, TXP_LFO1_RATE, TXP_LFO1_PHASE, TXP_LFO1_DEPTH},
        {TXP_LFO2_SHAPE, TXP_LFO2_RATE, TXP_LFO2_PHASE, TXP_LFO2_DEPTH},
        {TXP_LFO3_SHAPE, TXP_LFO3_RATE, TXP_LFO3_PHASE, TXP_LFO3_DEPTH},
    };
    for (int i = 0; i < 3; i++) {
        float hz = rate_hz(BV(LFO_P[i][1]), bpm);
        M->ph[i] += hz * dt;
        if (M->ph[i] >= 1.0f) {
            M->ph[i] -= floorf(M->ph[i]);
            M->rnd_a[i] = M->rnd_b[i];
            M->rnd_b[i] = tx_randb(&M->rng);
        }
        float ph = M->ph[i] + (float)BI(LFO_P[i][2]) / 360.0f;
        ph -= floorf(ph);
        M->src[MS_LFO1 + i] = lfo_shape(M, i, BI(LFO_P[i][0]), ph) * BV(LFO_P[i][3]) * 0.01f;
    }

    /* random: S&H and a walk, both smoothed */
    float smooth = BV(TXP_RND_SMOOTH) * 0.01f;
    float depth = BV(TXP_RND_DEPTH) * 0.01f;
    float shz = BV(TXP_RND_SH);
    M->sh_ph += shz * dt;
    if (M->sh_ph >= 1.0f) { M->sh_ph -= floorf(M->sh_ph); M->sh_target = tx_randb(&M->rng); }
    float sk = smooth > 0.0f ? 1.0f - expf(-dt / (smooth / shz + 1e-4f)) : 1.0f;   /* glide over up to one period */
    M->sh_out += (M->sh_target - M->sh_out) * sk;
    float whz = BV(TXP_RND_WALK);
    M->walk += tx_randb(&M->rng) * sqrtf(whz * dt) * 0.9f;
    if (M->walk > 1.0f) M->walk = 2.0f - M->walk;
    if (M->walk < -1.0f) M->walk = -2.0f - M->walk;
    float wk = smooth > 0.0f ? 1.0f - expf(-dt / (smooth * 0.5f + 1e-4f)) : 1.0f;
    M->walk_out += (M->walk - M->walk_out) * wk;
    M->src[MS_SH] = M->sh_out * depth;
    M->src[MS_WALK] = M->walk_out * depth;

    /* envelope follower on the output */
    float target = tx_clampf(follower * (0.5f + BV(TXP_EF_SENS) * 0.04f), 0.0f, 1.0f);
    float t = target > M->fol ? BV(TXP_EF_ATT) : BV(TXP_EF_REL);
    M->fol += (target - M->fol) * (1.0f - expf(-dt / (t * 0.001f)));
    M->src[MS_FOLLOW] = M->fol * BV(TXP_EF_DEPTH) * 0.01f;

    M->src[MS_NOTE] = note;
    M->src[MS_VEL] = vel;
    M->src[MS_DRIFT] = BV(TXP_SEQ_DRIFT) * 0.01f;
    M->src[MS_OFF] = 0.0f;

    /* the matrix */
    memcpy(ps->eff, ps->base, sizeof ps->eff);
    for (int k = 0; k < 8; k++) {
        int stride = TXP_MX2_SRC - TXP_MX1_SRC;
        int src = BI(TXP_MX1_SRC + k * stride);
        int dst = BI(TXP_MX1_DST + k * stride);
        float amt = (float)BI(TXP_MX1_AMT + k * stride) * 0.01f;
        int via = BI(TXP_MX1_VIA + k * stride);
        if (!src || !dst || amt == 0.0f || dst >= TX_NMODT) continue;
        int id = tx_modt[dst];
        if (id >= TX_NPARAMS) continue;
        float v = M->src[src] * amt;
        if (via) {
            float vv = M->src[via];
            if (via <= MS_WALK) vv = 0.5f * (vv + 1.0f);   /* bipolar sources scale as 0..1 */
            v *= vv;
        }
        ps->eff[id] = tx_clampf(ps->eff[id] + v, 0.0f, 1.0f);
    }
#undef BV
#undef BI
}
