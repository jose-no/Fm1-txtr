/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — grain cloud. Original code; the feature set (position/size/density/texture,
 * freeze, pitch-shift and looping modes) is modelled on Mutable Instruments Clouds. */
#include "tx_grain.h"
#include "tx_scale.h"

#define WIN_N 256
enum { W_GAUSS, W_HANN, W_TRI, W_RECT, W_EXPO, W_REXPO, W_COUNT };
static float WIN[W_COUNT][WIN_N + 1];
static int win_ready;

static void win_init(void)
{
    for (int i = 0; i <= WIN_N; i++) {
        float u = (float)i / WIN_N;
        float e = (u - 0.5f) / 0.17f;
        float edge = fminf(1.0f, fminf(u, 1.0f - u) / 0.02f);           /* short fades at the very ends */
        WIN[W_GAUSS][i] = expf(-0.5f * e * e) * edge;
        WIN[W_HANN][i] = 0.5f - 0.5f * cosf(TX_TWO_PI * u);
        WIN[W_TRI][i] = 1.0f - fabsf(2.0f * u - 1.0f);
        WIN[W_RECT][i] = edge;
        float ex = u < 0.01f ? u / 0.01f : expf(-5.0f * (u - 0.01f) / 0.99f);
        WIN[W_EXPO][i] = ex * fminf(1.0f, (1.0f - u) / 0.02f);
        float u2 = 1.0f - u;
        float rx = u2 < 0.01f ? u2 / 0.01f : expf(-5.0f * (u2 - 0.01f) / 0.99f);
        WIN[W_REXPO][i] = rx * fminf(1.0f, u / 0.02f);
    }
    win_ready = 1;
}

static inline float win_at(int w, float u)
{
    float x = u * WIN_N;
    int i = (int)x;
    if (i >= WIN_N) return WIN[w][WIN_N];
    float f = x - (float)i;
    return WIN[w][i] + (WIN[w][i + 1] - WIN[w][i]) * f;
}

void tx_grains_clear(tx_grains *G)
{
    memset(G->buf, 0, sizeof G->buf);
    for (int i = 0; i < TX_MAX_GRAINS; i++) G->g[i].active = 0;
    G->w = 0;
    G->nactive = 0;
}

void tx_grains_init(tx_grains *G, float fs, uint32_t seed)
{
    if (!win_ready) win_init();
    memset(G, 0, sizeof *G);
    G->fs = fs;
    G->len = TX_BUF_LEN;
    G->rng.s = seed ? seed : 0xC0FFEEu;
    G->scan_dir = 1;
    G->scale_mask = 0xFFF;
}

/* settings read once per block */
typedef struct {
    int mode, trig, quant, ivset, win, cap;
    float pos, size_ms, density, pitch, spray, sizernd, pspray, pan, ivchance, detrnd;
    float skew, winmorph, amprnd, rev, speed, norm;
    int frozen;
} gcfg;

static void read_cfg(tx_grains *G, const tx_pstate *ps, gcfg *c)
{
    c->mode = tx_int(ps, TXP_GR_MODE);
    c->trig = tx_int(ps, TXP_GR_TRIG);
    c->quant = tx_int(ps, TXP_GR_QUANT);
    c->ivset = tx_int(ps, TXP_GR_IVSET);
    c->win = tx_int(ps, TXP_GR_WIN);
    c->cap = tx_clampi(tx_int(ps, TXP_GR_CAP) - G->shed, 2, TX_MAX_GRAINS);
    c->pos = tx_val(ps, TXP_GR_POS) * 0.01f;
    c->size_ms = tx_val(ps, TXP_GR_SIZE);
    c->density = tx_val(ps, TXP_GR_DENSITY);
    c->pitch = tx_val(ps, TXP_GR_PITCH);
    c->spray = tx_val(ps, TXP_GR_SPRAY) * 0.01f;
    c->sizernd = tx_val(ps, TXP_GR_SIZERND) * 0.01f;
    c->pspray = tx_val(ps, TXP_GR_PSPRAY) * 0.01f;
    c->pan = tx_val(ps, TXP_GR_PAN) * 0.01f;
    c->ivchance = tx_val(ps, TXP_GR_IVCHANCE) * 0.01f;
    c->detrnd = tx_val(ps, TXP_GR_DETRND);
    c->skew = tx_val(ps, TXP_GR_SKEW) * 0.01f;
    c->winmorph = tx_val(ps, TXP_GR_WINMORPH) * 0.01f;
    c->amprnd = tx_val(ps, TXP_GR_AMPRND) * 0.01f;
    c->rev = tx_val(ps, TXP_GR_REV) * 0.01f;
    c->speed = tx_val(ps, TXP_GR_SPEED);
    c->frozen = tx_int(ps, TXP_BUF_FREEZE);
    if (c->mode == 1) {                     /* pitch shift: steady overlap right behind the head */
        c->trig = 0;
        c->density = 2.5f / (c->size_ms * 0.001f);
        c->pos = 0.0f;
        c->spray *= 0.1f;
        c->rev = 0.0f;
        c->win = W_HANN;
        c->winmorph = 0.0f;
    }
    /* keep the level steady however many grains overlap */
    float overlap = c->density * c->size_ms * 0.001f;
    c->norm = 1.0f / sqrtf(overlap > 1.0f ? overlap : 1.0f);
    if (G->shed && ++G->shed_age > 700u) { G->shed--; G->shed_age = 0; }   /* one grain back every ~1 s */
}

static float grain_semis(tx_grains *G, const gcfg *c)
{
    float st = c->pitch + tx_randb(&G->rng) * c->pspray * 12.0f;
    if (tx_randf(&G->rng) < c->ivchance) {
        static const int8_t OCT[] = {12, -12};
        static const int8_t O5[] = {12, -12, 7, 19, -5};
        int r = (int)(tx_rand(&G->rng) >> 8);
        if (c->ivset == 0) st += OCT[r % 2];
        else if (c->ivset == 1) st += O5[r % 5];
        else {
            int8_t d[12];
            int m = tx_scale_degrees(G->scale_mask, d);
            st += d[r % m] + ((r >> 8) & 1 ? 12 : 0) - 12 * ((r >> 9) & 1);
        }
    }
    if (c->quant) st = (float)tx_scale_snap(G->scale_mask, (int)lrintf(st));
    st += tx_randb(&G->rng) * c->detrnd * 0.01f;
    return st;
}

static void spawn(tx_grains *G, const gcfg *c, int delay, float size_ms, float pos_norm, int loop)
{
    if (G->nactive >= c->cap) return;
    tx_grain *g = 0;
    for (int i = 0; i < TX_MAX_GRAINS; i++) if (!G->g[i].active) { g = &G->g[i]; break; }
    if (!g) return;

    float ratio = tx_semis(grain_semis(G, c)) * c->speed;
    int rev = tx_randf(&G->rng) < c->rev;
    float inc = (1.0f / TX_BUF_DECIM) * ratio * (rev ? -1.0f : 1.0f);
    float L = size_ms * 0.001f * G->fs;
    if (!loop) L *= 1.0f + tx_randb(&G->rng) * c->sizernd * 0.8f;
    if (L < 16.0f) L = 16.0f;

    /* where it can start without running into the write head during its life */
    float len = (float)G->len, head = c->frozen ? 0.0f : 1.0f / TX_BUF_DECIM, m = 32.0f;
    float lo = fmaxf(m, (inc - head) * L + m);
    float hi = len - fmaxf(0.0f, (head - inc) * L) - m;
    float back;
    if (lo > hi) back = 0.5f * (lo + hi);
    else back = lo + tx_clampf(pos_norm, 0.0f, 1.0f) * (hi - lo);
    back = tx_clampf(back, m, len - m);
    float p = (float)G->w - back;
    while (p < 0.0f) p += len;

    float pan = 0.5f + (loop ? 0.0f : tx_randb(&G->rng) * c->pan * 0.5f);
    float amp = (1.0f - tx_randf(&G->rng) * c->amprnd) * c->norm;
    g->pos = p;
    g->inc = inc;
    g->phase = 0.0f;
    g->dphase = 1.0f / L;
    g->gl = cosf(pan * 1.5707963f) * amp * 1.41421356f;
    g->gr = sinf(pan * 1.5707963f) * amp * 1.41421356f;
    g->win = (uint8_t)(loop ? W_RECT : c->win);
    g->win2 = (uint8_t)((g->win + 1) % W_COUNT);
    g->wmorph = loop ? 0.0f : c->winmorph;
    g->peak = 0.5f + tx_clampf(c->skew, -1.0f, 1.0f) * 0.45f;
    g->delay = delay;
    g->active = 1;
    G->nactive++;
}

static float effective_pos(tx_grains *G, const gcfg *c)
{
    float p = c->pos + G->scan + tx_randb(&G->rng) * c->spray * 0.5f;
    p -= floorf(p);
    return p;
}

void tx_grains_overload(tx_grains *G)
{
    G->shed = G->shed + 4 > TX_MAX_GRAINS ? TX_MAX_GRAINS : G->shed + 4;
    G->shed_age = 0;
}

void tx_grains_hit(tx_grains *G, const tx_pstate *ps)
{
    if (tx_int(ps, TXP_GR_TRIG) != 1 || tx_int(ps, TXP_GR_MODE) != 0) return;
    G->burst_left = tx_int(ps, TXP_GR_BURST);
    G->burst_gap = tx_val(ps, TXP_GR_BURSTSPR) * 0.001f * G->fs / (float)G->burst_left;
    G->until_burst = 0.0f;
}

void tx_grains_block(tx_grains *G, const tx_pstate *ps, const float *in, float *outL, float *outR)
{
    gcfg c;
    read_cfg(G, ps, &c);
    const int N = TX_BLOCK;

    /* buffer length follows the Length parameter */
    uint32_t len = (uint32_t)(tx_val(ps, TXP_BUF_LEN) * G->fs / TX_BUF_DECIM);
    if (len > TX_BUF_LEN) len = TX_BUF_LEN;
    if (len < 1024) len = 1024;
    if (len != G->len) {
        G->len = len;
        if (G->w >= len) G->w = 0;
        for (int i = 0; i < TX_MAX_GRAINS; i++) if (G->g[i].active && G->g[i].pos >= (float)len) G->g[i].pos -= (float)len * floorf(G->g[i].pos / (float)len);
    }

    /* scan */
    float sr = tx_val(ps, TXP_GR_SCAN) * 0.01f;
    sr = sr * sr * 0.5f * (float)N / G->fs;              /* up to half the buffer per second */
    int sdir = tx_int(ps, TXP_GR_SCANDIR);
    if (sdir == 0) G->scan += sr;
    else if (sdir == 1) G->scan -= sr;
    else {
        G->scan += sr * (float)G->scan_dir;
        if (G->scan > 1.0f) { G->scan = 1.0f; G->scan_dir = -1; }
        if (G->scan < 0.0f) { G->scan = 0.0f; G->scan_dir = 1; }
    }
    if (sdir != 2) G->scan -= floorf(G->scan);

    /* triggering: grain start offsets within this block */
    for (int s = 0; s < N; s++) {
        if (c.mode == 2) {                                /* loop delay */
            G->loop_until -= 1.0f;
            if (G->loop_until <= 0.0f) {
                float L = c.size_ms * 0.001f * G->fs;
                spawn(G, &c, s, c.size_ms, c.pos, 1);
                G->loop_until += L * 0.9f;                /* 10% crossfade */
            }
            continue;
        }
        if (c.trig == 0 || c.trig == 2) {
            G->until_spawn -= 1.0f;
            if (G->until_spawn <= 0.0f) {
                float rate = c.trig == 2 ? c.density / (float)tx_int(ps, TXP_GR_BURST) : c.density;
                float period = G->fs / (rate > 0.01f ? rate : 0.01f);
                float j = tx_val(ps, TXP_GR_JITTER) * 0.01f;
                float u = tx_randf(&G->rng);
                float expo = -logf(u > 1e-6f ? u : 1e-6f);
                G->until_spawn += period * ((1.0f - j) + j * expo);
                if (c.trig == 0) spawn(G, &c, s, c.size_ms, effective_pos(G, &c), 0);
                else {
                    G->burst_left = tx_int(ps, TXP_GR_BURST);
                    G->burst_gap = tx_val(ps, TXP_GR_BURSTSPR) * 0.001f * G->fs / (float)G->burst_left;
                    G->until_burst = 0.0f;
                }
            }
        }
        if (G->burst_left > 0) {
            G->until_burst -= 1.0f;
            if (G->until_burst <= 0.0f) {
                spawn(G, &c, s, c.size_ms, effective_pos(G, &c), 0);
                G->burst_left--;
                float j = tx_val(ps, TXP_GR_JITTER) * 0.01f;
                G->until_burst += G->burst_gap * (1.0f + tx_randb(&G->rng) * j);
            }
        }
    }

    /* render the grains */
    const float flen = (float)G->len;
    static float accL[TX_BLOCK], accR[TX_BLOCK];
    memset(accL, 0, sizeof accL);
    memset(accR, 0, sizeof accR);
    int n_active = 0;
    for (int i = 0; i < TX_MAX_GRAINS; i++) {
        tx_grain *g = &G->g[i];
        if (!g->active) continue;
        int s = g->delay;
        g->delay = 0;
        float pk = g->peak;
        for (; s < N; s++) {
            float t = g->phase;
            if (t >= 1.0f) { g->active = 0; break; }
            float u = t < pk ? 0.5f * t / pk : 0.5f + 0.5f * (t - pk) / (1.0f - pk);
            float w = win_at(g->win, u);
            if (g->wmorph > 0.0f) w += (win_at(g->win2, u) - w) * g->wmorph;
            int i0 = (int)g->pos;
            float f = g->pos - (float)i0;
            int i1 = i0 + 1;
            if ((uint32_t)i1 >= G->len) i1 = 0;
            float x = ((float)G->buf[i0] + ((float)G->buf[i1] - (float)G->buf[i0]) * f) * (1.0f / 32768.0f);
            x *= w;
            accL[s] += x * g->gl;
            accR[s] += x * g->gr;
            g->phase += g->dphase;
            g->pos += g->inc;
            if (g->pos >= flen) g->pos -= flen;
            else if (g->pos < 0.0f) g->pos += flen;
        }
        if (g->active) n_active++;
    }
    G->nactive = n_active;

    /* write the input into the buffer (after reading, so a grain never reads half a block ahead) */
    if (!c.frozen) {
        float gin = tx_val(ps, TXP_BUF_IN) * 0.01f;
        int dub = tx_int(ps, TXP_BUF_MODE);
        float a = 1.0f - expf(-TX_TWO_PI * 9000.0f / G->fs);
        for (int s = 0; s < N; s++) {
            G->lp += (in[s] * gin - G->lp) * a;
            if (++G->dec_phase < TX_BUF_DECIM) { G->dec_prev = G->lp; continue; }
            G->dec_phase = 0;
            float y = 0.5f * (G->lp + G->dec_prev);
            if (dub) y += (float)G->buf[G->w] * (1.0f / 32768.0f) * 0.8f;
            y = tx_clampf(y, -1.0f, 1.0f);
            G->buf[G->w] = (int16_t)lrintf(y * 32767.0f);
            if (++G->w >= G->len) G->w = 0;
        }
    }

    /* stereo width and level */
    float width = tx_val(ps, TXP_GR_WIDTH) * 0.02f;
    float lvl = tx_val(ps, TXP_GR_LEVEL) * 0.01f;
    for (int s = 0; s < N; s++) {
        float m = 0.5f * (accL[s] + accR[s]), d = 0.5f * (accL[s] - accR[s]) * width;
        outL[s] += (m + d) * lvl;
        outR[s] += (m - d) * lvl;
    }
}

float tx_grains_head(const tx_grains *G) { return (float)G->w / (float)G->len; }

int tx_grains_live(const tx_grains *G, float *pos, float *amp, int max)
{
    int n = 0;
    for (int i = 0; i < TX_MAX_GRAINS && n < max; i++) {
        const tx_grain *g = &G->g[i];
        if (!g->active) continue;
        pos[n] = g->pos / (float)G->len;
        amp[n] = win_at(g->win, tx_clampf(g->phase, 0.0f, 1.0f)) * fabsf(g->gl + g->gr) * 0.5f;
        n++;
    }
    return n;
}

void tx_grains_overview(const tx_grains *G, uint8_t *out, int bins)
{
    uint32_t len = G->len;
    for (int b = 0; b < bins; b++) {
        uint32_t s0 = len / (uint32_t)bins * (uint32_t)b, s1 = len / (uint32_t)bins * (uint32_t)(b + 1);
        int peak = 0;
        for (uint32_t s = s0; s < s1; s += 4) {       /* every 4th sample is plenty for a picture */
            int v = G->buf[s] < 0 ? -G->buf[s] : G->buf[s];
            if (v > peak) peak = v;
        }
        out[b] = (uint8_t)(peak >> 7);
    }
}
