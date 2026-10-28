/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — core tests (run on the computer: make test). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "tx_engine.h"
#include "tx_seq.h"
#include "tx_scale.h"
#include "tx_param.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void pattern(int steps, int hits, int rot, char *out)
{
    for (int i = 0; i < steps; i++) out[i] = tx_euclid(steps, hits, rot, i) ? 'x' : '.';
    out[steps] = 0;
}

static void test_euclid(void)
{
    char p[40];
    pattern(8, 3, 0, p);  CHECK(!strcmp(p, "x..x..x."));
    pattern(16, 4, 0, p); CHECK(!strcmp(p, "x...x...x...x..."));
    pattern(8, 3, 1, p);  CHECK(!strcmp(p, ".x..x..x"));
    pattern(5, 0, 0, p);  CHECK(!strcmp(p, "....."));
    pattern(5, 9, 0, p);  CHECK(!strcmp(p, "xxxxx"));
    for (int n = 1; n <= 32; n++)
        for (int k = 0; k <= n; k++) {
            int c = 0;
            for (int i = 0; i < n; i++) c += tx_euclid(n, k, 0, i);
            CHECK(c == k);
        }
}

static void test_scale(void)
{
    CHECK(tx_scale_mask(0, 0) == 2741);                 /* major */
    int8_t d[12];
    CHECK(tx_scale_degrees(tx_scale_mask(10, 0), d) == 5);
    CHECK(d[1] == 3 && d[4] == 10);                     /* minor pentatonic */
    uint16_t maj = tx_scale_mask(0, 0);
    CHECK(tx_scale_snap(maj, 1) == 0 || tx_scale_snap(maj, 1) == 2);
    CHECK(tx_scale_snap(maj, 6) == 5 || tx_scale_snap(maj, 6) == 7);
    CHECK(tx_scale_snap(maj, -1) == -1);                /* B below the root */
    CHECK(tx_scale_snap(maj, 12) == 12);
    CHECK(tx_scale_mask(15, 0) == 1);                   /* custom: the root is always in */
}

static void test_params(void)
{
    tx_pstate a, b;
    txp_init(&a);
    for (int i = 0; i < TX_NPARAMS; i++) {
        CHECK(a.snap[0][i] >= tx_pdefs[i].rmin && a.snap[0][i] <= tx_pdefs[i].rmax);
        float n = txp_norm(i, a.snap[0][i]);
        CHECK(txp_raw(i, n) == a.snap[0][i]);
    }
    /* defaults land where params.py says */
    CHECK(fabsf(txp_phys(TXP_GR_SIZE, a.base[TXP_GR_SIZE]) - 180.0f) < 2.0f);
    CHECK(tx_int(&a, TXP_SEQ_STEPS) == 16);
    CHECK(fabsf(tx_val(&a, TXP_SEQ_RATE) - 0.25f) < 1e-6f);
    /* morph: halfway between A and B */
    txp_set(&a, TXP_GR_POS, TX_SNAP_A, 0);
    txp_set(&a, TXP_GR_POS, TX_SNAP_B, 1000);
    txp_set(&a, TXP_MORPH, TX_SNAP_A, 500);
    txp_morph(&a);
    CHECK(fabsf(a.base[TXP_GR_POS] - 0.5f) < 1e-3f);
    /* save / load round trip */
    uint8_t buf[TX_PATCH_MAX];
    strcpy(a.name, "Roundtrip");
    size_t n = txp_save(&a, buf, sizeof buf);
    CHECK(n > 0 && n <= 4096);
    CHECK(txp_load(&b, buf, n) == 0);
    CHECK(!memcmp(a.snap, b.snap, sizeof a.snap));
    CHECK(!strcmp(b.name, "Roundtrip"));
    buf[40] ^= 1;
    CHECK(txp_load(&b, buf, n) == -1);                  /* a damaged patch is refused */
}

static void test_seq_drift_zero(void)
{
    /* with drift 0, hits land exactly on step boundaries (block resolution) */
    tx_pstate ps;
    txp_init(&ps);
    txp_set(&ps, TXP_SEQ_DRIFT, TX_SNAP_BOTH, 0);
    txp_set(&ps, TXP_SEQ_DENSITY, TX_SNAP_BOTH, 16);
    txp_set(&ps, TXP_SEQ_RATE, TX_SNAP_BOTH, 7);          /* 1/8 bar */
    txp_set(&ps, TXP_SEQ_BPM, TX_SNAP_BOTH, 120);
    txp_morph(&ps); memcpy(ps.eff, ps.base, sizeof ps.eff);
    tx_seq s;
    tx_seq_init(&s, 1);
    tx_seq_start(&s);
    const float fs = 48000.0f;                           /* step = 0.25 s = 12000 samples = 187.5 blocks */
    int ons = 0, last = -1, ok = 1;
    for (int b = 0; b < 2000; b++) {
        tx_event ev[16];
        int n = tx_seq_block(&s, &ps, fs, 64, ev, 16);
        for (int i = 0; i < n; i++) if (ev[i].type == TX_EV_ON) {
            if (last >= 0) { int d = b - last; if (d < 187 || d > 188) ok = 0; }
            last = b; ons++;
        }
    }
    CHECK(ons >= 9);
    CHECK(ok);
}

static void test_engine_audio(void)
{
    tx_init(44100.0f, 7);
    tx_play(1);
    static float L[44100], R[44100];
    double energy = 0.0;
    int bad = 0;
    for (int sec = 0; sec < 8; sec++) {
        tx_render(L, R, 44100);
        for (int i = 0; i < 44100; i++) {
            if (!isfinite(L[i]) || !isfinite(R[i]) || fabsf(L[i]) > 1.0f || fabsf(R[i]) > 1.0f) bad++;
            if (sec >= 4) energy += (double)L[i] * L[i] + (double)R[i] * R[i];
        }
    }
    CHECK(bad == 0);
    double rms = sqrt(energy / (4.0 * 44100 * 2));
    printf("  engine: rms %.4f over seconds 4-8\n", rms);
    CHECK(rms > 0.005 && rms < 0.7);
    tx_status st;
    tx_get_status(&st);
    printf("  engine: %d grains, %d FM voices, step %d/%d\n", st.ngrains, st.fm_voices, st.step, st.steps);
    CHECK(st.ngrains > 0);
    CHECK(st.ngrains <= tx_get(TXP_GR_CAP));
    /* freeze stops the write head */
    tx_freeze(1);
    tx_render(L, R, 4410);
    float h0 = (tx_get_status(&st), st.head);
    tx_render(L, R, 44100);
    tx_get_status(&st);
    CHECK(st.head == h0 && st.frozen);
    /* every engine mode and trigger mode runs clean */
    tx_freeze(0);
    for (int mode = 0; mode < 3; mode++)
        for (int trig = 0; trig < 3; trig++) {
            tx_edit_snap(TX_SNAP_BOTH);
            tx_set(TXP_GR_MODE, mode);
            tx_set(TXP_GR_TRIG, trig);
            tx_set(TXP_FB_AMT, 600);
            tx_set(TXP_FB_PITCH, 7);
            tx_set(TXP_RV_SHIMMER, 500);
            tx_render(L, R, 22050);
            for (int i = 0; i < 22050; i++) if (!isfinite(L[i]) || fabsf(L[i]) > 1.0f) bad++;
        }
    CHECK(bad == 0);
}

int main(void)
{
    test_euclid();
    test_scale();
    test_params();
    test_seq_drift_zero();
    test_engine_audio();
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("all tests passed\n");
    return 0;
}
