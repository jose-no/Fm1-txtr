/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "tx_fx.h"

/* reverb line lengths at Size 100% (mutually prime) and input allpass lengths */
static const uint16_t RV_LEN[TX_RV_LINES] = {3593, 3203, 2861, 2459};
static const uint16_t RV_AP_LEN[TX_RV_AP] = {142, 107, 379, 277};

void tx_pshift_init(tx_pshift *p) { memset(p, 0, sizeof *p); }

float tx_pshift_run(tx_pshift *p, float x, float ratio)
{
    p->buf[p->w] = x;
    float W = (float)(TX_PS_LEN - 4);
    p->phase += (1.0f - ratio) / W;
    p->phase -= floorf(p->phase);
    float y = 0.0f;
    for (int k = 0; k < 2; k++) {
        float ph = p->phase + 0.5f * (float)k;
        ph -= floorf(ph);
        float d = 2.0f + ph * W;
        float r = (float)p->w - d;
        if (r < 0.0f) r += TX_PS_LEN;
        int i0 = (int)r;
        float f = r - (float)i0;
        int i1 = (i0 + 1) % TX_PS_LEN;
        float s = p->buf[i0] + (p->buf[i1] - p->buf[i0]) * f;
        float g = 1.0f - fabsf(2.0f * ph - 1.0f);        /* triangle: the two taps sum to 1 */
        y += s * g;
    }
    p->w = (p->w + 1) % TX_PS_LEN;
    return y;
}

void tx_fx_clear(tx_fx *F)
{
    float fs = F->fs;
    memset(F, 0, sizeof *F);
    F->fs = fs;
}

void tx_fx_init(tx_fx *F, float fs)
{
    F->fs = fs;
    tx_fx_clear(F);
}

static inline float rd16(const int16_t *b, uint32_t len, float pos)
{
    while (pos < 0.0f) pos += (float)len;
    int i0 = (int)pos;
    if ((uint32_t)i0 >= len) i0 -= (int)len;
    float f = pos - floorf(pos);
    int i1 = i0 + 1;
    if ((uint32_t)i1 >= len) i1 = 0;
    return ((float)b[i0] + ((float)b[i1] - (float)b[i0]) * f) * (1.0f / 32768.0f);
}

static inline int16_t to16(float x)
{
    x = tx_clampf(x, -1.0f, 1.0f);
    return (int16_t)lrintf(x * 32767.0f);
}

void tx_fx_block(tx_fx *F, const tx_pstate *ps, float bpm, float *L, float *R)
{
    const int N = TX_BLOCK;
    const float fs = F->fs;
    float *ch[2] = {L, R};

    /* ---------------- filter */
    {
        int type = tx_int(ps, TXP_SP_FTYPE);
        float fc = tx_clampf(tx_val(ps, TXP_SP_FCUT), 20.0f, 0.45f * fs);
        float res = tx_val(ps, TXP_SP_FRES) * 0.01f;
        float drive = tx_val(ps, TXP_SP_FDRIVE) * 0.01f;
        float g = tanf((float)M_PI * fc / fs), k = 2.0f - 1.94f * res;
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float pre = 1.0f + drive * 6.0f, post = 1.0f / (1.0f + drive * 2.0f);
        int bypass = type == 0 && fc > 0.44f * fs && res < 0.01f && drive < 0.01f;
        if (!bypass)
            for (int c = 0; c < 2; c++)
                for (int s = 0; s < N; s++) {
                    float x = ch[c][s];
                    if (drive > 0.0f) x = tx_soft(x * pre) * post * (1.0f + drive);
                    float v3 = x - F->f_ic2[c];
                    float v1 = a1 * F->f_ic1[c] + a2 * v3;
                    float v2 = F->f_ic2[c] + a2 * F->f_ic1[c] + a3 * v3;
                    F->f_ic1[c] = 2.0f * v1 - F->f_ic1[c];
                    F->f_ic2[c] = 2.0f * v2 - F->f_ic2[c];
                    ch[c][s] = type == 0 ? v2 : type == 1 ? v1 * k : type == 2 ? x - k * v1 - v2 : x - k * v1;
                }
    }

    /* ---------------- color */
    {
        float sat = tx_val(ps, TXP_SP_SAT) * 0.01f, fold = tx_val(ps, TXP_SP_FOLD) * 0.01f;
        float crush = tx_val(ps, TXP_SP_CRUSH) * 0.01f, srr = tx_val(ps, TXP_SP_SRR) * 0.01f;
        float q = crush > 0.0f ? exp2f(15.0f - crush * 13.0f) : 0.0f;
        float hold = 1.0f + srr * srr * 31.0f;
        for (int s = 0; s < N; s++) {
            if (srr > 0.0f) {
                F->srr_phase += 1.0f;
                if (F->srr_phase >= hold) {
                    F->srr_phase -= hold;
                    F->srr_hold[0] = L[s];
                    F->srr_hold[1] = R[s];
                }
                L[s] = F->srr_hold[0];
                R[s] = F->srr_hold[1];
            }
            for (int c = 0; c < 2; c++) {
                float x = ch[c][s];
                if (sat > 0.0f) x = tx_lerp(x, tx_soft(x * (1.0f + 4.0f * sat)), sat);
                if (fold > 0.0f) x = tx_lerp(x, sinf(x * (1.0f + 5.0f * fold) * 1.5707963f), fold);
                if (q > 0.0f) x = floorf(x * q + 0.5f) / q;
                ch[c][s] = x;
            }
        }
    }

    /* ---------------- delay (two int16 lines at half rate) */
    {
        float tv = tx_val(ps, TXP_DL_TIME);
        float ms = tv > 0.0f ? tv : -tv * 4.0f * 60.0f / bpm * 1000.0f;
        float maxms = (float)(TX_DLY_LEN - 64) * TX_DLY_DECIM / fs * 1000.0f;
        ms = tx_clampf(ms, 5.0f, maxms);
        float fb = tx_val(ps, TXP_DL_FB) * 0.01f;
        float tone = tx_val(ps, TXP_DL_TONE) * 0.01f;
        float lpk = 1.0f - expf(-TX_TWO_PI * (500.0f * powf(24.0f, tone)) / fs);
        float wp = tx_val(ps, TXP_DL_WIDTH) * 0.01f;
        float mrate = tx_val(ps, TXP_DL_MRATE), mdepth = tx_val(ps, TXP_DL_MDEPTH) * 0.01f;
        float diff = tx_val(ps, TXP_DL_DIFF) * 0.01f * 0.6f;
        float mix = tx_val(ps, TXP_DL_MIX) * 0.01f;
        float dfs = fs / TX_DLY_DECIM;
        if (mix > 0.0f || fb > 0.0f) {
            for (int s = 0; s < N; s++) {
                F->dl_lfo += mrate / fs;
                F->dl_lfo -= floorf(F->dl_lfo);
                float dms = ms + sinf(TX_TWO_PI * F->dl_lfo) * mdepth * 5.0f;
                float d = dms * 0.001f * dfs;
                float pos = (float)F->dl_w + 0.5f * (float)F->dl_phase - d;
                float y[2];
                for (int c = 0; c < 2; c++) y[c] = rd16(F->dl[c], TX_DLY_LEN, pos);
                /* diffusion: one allpass per channel in the loop */
                if (diff > 0.0f) {
                    uint32_t ai = F->dl_ap_i;
                    for (int c = 0; c < 2; c++) {
                        float b = F->dl_ap[c][ai];
                        float v = y[c] + diff * b;
                        F->dl_ap[c][ai] = v;
                        y[c] = b - diff * v;
                    }
                    F->dl_ap_i = (ai + 1) % 181u;          /* allpass length, half-rate samples */
                }
                for (int c = 0; c < 2; c++) F->dl_lp[c] += (y[c] - F->dl_lp[c]) * lpk;
                float yl = F->dl_lp[0], yr = F->dl_lp[1];
                float mono = 0.5f * (L[s] + R[s]);
                float wl = (1.0f - wp) * L[s] + wp * mono + fb * ((1.0f - wp) * yl + wp * yr);
                float wr = (1.0f - wp) * R[s] + fb * ((1.0f - wp) * yr + wp * yl);
                wl = tx_soft(wl); wr = tx_soft(wr);
                if (++F->dl_phase >= TX_DLY_DECIM) {
                    F->dl_phase = 0;
                    F->dl[0][F->dl_w] = to16(0.5f * (wl + F->dl_in_prev[0]));
                    F->dl[1][F->dl_w] = to16(0.5f * (wr + F->dl_in_prev[1]));
                    if (++F->dl_w >= TX_DLY_LEN) F->dl_w = 0;
                }
                F->dl_in_prev[0] = wl; F->dl_in_prev[1] = wr;
                L[s] = L[s] * (1.0f - 0.5f * mix) + yl * mix;
                R[s] = R[s] * (1.0f - 0.5f * mix) + yr * mix;
            }
        }
    }

    /* ---------------- reverb (4-line feedback delay network) */
    {
        float mix = tx_val(ps, TXP_RV_MIX) * 0.01f;
        if (mix > 0.0f) {
            float size = 0.3f + 0.7f * tx_val(ps, TXP_RV_SIZE) * 0.01f;
            float rt = tx_val(ps, TXP_RV_DECAY);
            float damp = tx_val(ps, TXP_RV_DAMP) * 0.01f;
            float dk = 1.0f - 0.9f * damp;                  /* one-pole coefficient in each line */
            float pre = tx_val(ps, TXP_RV_PRE) * 0.001f * fs / 2.0f;
            float modd = tx_val(ps, TXP_RV_MOD) * 0.01f * 12.0f;
            float shim = tx_val(ps, TXP_RV_SHIMMER) * 0.01f;
            float hpk = expf(-TX_TWO_PI * tx_val(ps, TXP_RV_LOWCUT) / fs);
            float len[TX_RV_LINES], gain[TX_RV_LINES];
            for (int i = 0; i < TX_RV_LINES; i++) {
                len[i] = (float)RV_LEN[i] * size;
                gain[i] = powf(10.0f, -3.0f * len[i] / (fs * rt));
            }
            for (int s = 0; s < N; s++) {
                float x = 0.5f * (L[s] + R[s]);
                /* pre-delay at half rate */
                float pd = rd16(F->rv_pre, TX_RV_PRE, (float)F->rv_pre_w + 0.5f * (float)F->rv_pre_phase - pre - 1.0f);
                if (++F->rv_pre_phase >= 2) {
                    F->rv_pre_phase = 0;
                    F->rv_pre[F->rv_pre_w] = to16(0.5f * (x + F->rv_pre_prev));
                    if (++F->rv_pre_w >= TX_RV_PRE) F->rv_pre_w = 0;
                }
                F->rv_pre_prev = x;
                if (pre < 1.0f) pd = x;
                /* low cut */
                float hp = hpk * (F->rv_hp_y + pd - F->rv_hp_x);
                F->rv_hp_x = pd; F->rv_hp_y = hp;
                float in = hp;
                /* input diffusion */
                for (int a = 0; a < TX_RV_AP; a++) {
                    uint32_t i = F->rv_ap_i[a];
                    float b = F->rv_ap[a][i];
                    float v = in + 0.6f * b;
                    F->rv_ap[a][i] = v;
                    in = b - 0.6f * v;
                    F->rv_ap_i[a] = (i + 1) % RV_AP_LEN[a];
                }
                /* the tank */
                F->rv_lfo += 0.6f / fs;
                F->rv_lfo -= floorf(F->rv_lfo);
                float o[TX_RV_LINES];
                for (int i = 0; i < TX_RV_LINES; i++) {
                    float m = i & 1 ? 0.0f : sinf(TX_TWO_PI * (F->rv_lfo + 0.25f * (float)i)) * modd;
                    o[i] = rd16(F->rv_line[i], TX_RV_MAX, (float)F->rv_w - len[i] - m - 2.0f);
                    F->rv_damp[i] += (o[i] - F->rv_damp[i]) * dk;
                    o[i] = F->rv_damp[i] * gain[i];
                }
                /* shimmer: an octave up, back into the tank */
                float sh = shim > 0.0f ? tx_pshift_run(&F->shimmer, 0.5f * (o[0] + o[1]), 2.0f) * shim * 0.5f : 0.0f;
                float h = 0.5f * (o[0] + o[1] + o[2] + o[3]);   /* Householder: x - 2/N * sum */
                for (int i = 0; i < TX_RV_LINES; i++)
                    F->rv_line[i][F->rv_w] = to16(o[i] - h + in * 0.5f + sh);
                if (++F->rv_w >= TX_RV_MAX) F->rv_w = 0;
                float wl = o[0] + o[2], wr = o[1] + o[3];
                L[s] = L[s] * (1.0f - 0.5f * mix) + wl * mix;
                R[s] = R[s] * (1.0f - 0.5f * mix) + wr * mix;
            }
        }
    }

    /* ---------------- feedback path: back into the grain buffer next block */
    {
        float amt = tx_val(ps, TXP_FB_AMT) * 0.01f;
        if (amt > 0.0f) {
            float flt = tx_val(ps, TXP_FB_FILTER) * 0.01f;
            float lpk = flt < 0.0f ? 1.0f - expf(-TX_TWO_PI * 18000.0f * powf(0.011f, -flt) / fs) : 1.0f;
            float hpk = flt > 0.0f ? expf(-TX_TWO_PI * 20.0f * powf(100.0f, flt) / fs) : 1.0f;
            float ratio = tx_semis((float)tx_int(ps, TXP_FB_PITCH));
            float sat = tx_val(ps, TXP_FB_SAT) * 0.01f;
            int shift = tx_int(ps, TXP_FB_PITCH) != 0;
            for (int s = 0; s < N; s++) {
                float x = 0.5f * (L[s] + R[s]);
                F->fb_lp += (x - F->fb_lp) * lpk;
                x = F->fb_lp;
                if (flt > 0.0f) {
                    float y = hpk * (F->fb_hp_y + x - F->fb_hp_x);
                    F->fb_hp_x = x; F->fb_hp_y = y; x = y;
                }
                if (shift) x = tx_pshift_run(&F->fb_ps, x, ratio);
                if (sat > 0.0f) x = tx_lerp(x, tx_soft(x * (1.0f + 3.0f * sat)), sat);
                F->fb[s] = x * amt;
            }
        } else {
            memset(F->fb, 0, sizeof F->fb);
        }
    }

    /* ---------------- output: width, limiter, level */
    {
        float width = tx_val(ps, TXP_OUT_WIDTH) * 0.01f;
        float limit = tx_val(ps, TXP_OUT_LIMIT) * 0.01f;
        float level = tx_val(ps, TXP_OUT_LEVEL) * 0.015f;          /* 100% = +3.5 dB, the limiter guards the top */
        float thr = 1.0f - 0.6f * limit;
        float rel = 1.0f - expf(-1.0f / (0.15f * fs));
        float fol = F->follower, fk = 1.0f - expf(-1.0f / (0.01f * fs));
        for (int s = 0; s < N; s++) {
            for (int c = 0; c < 2; c++) {             /* DC blocker, ~10 Hz */
                float x = ch[c][s];
                float y = x - F->dc_x[c] + 0.9986f * F->dc_y[c];
                F->dc_x[c] = x; F->dc_y[c] = y;
                ch[c][s] = y;
            }
            float m = 0.5f * (L[s] + R[s]), d = 0.5f * (L[s] - R[s]) * width;
            float l = (m + d) * level, r = (m - d) * level;
            float pk = fmaxf(fabsf(l), fabsf(r));
            if (pk > F->lim_env) F->lim_env = pk; else F->lim_env += (pk - F->lim_env) * rel;
            float g = limit > 0.0f && F->lim_env > thr ? thr / F->lim_env : 1.0f;
            l *= g; r *= g;
            L[s] = tx_clampf(l, -1.0f, 1.0f);
            R[s] = tx_clampf(r, -1.0f, 1.0f);
            fol += (pk - fol) * fk;
        }
        F->follower = fol;
    }
}
