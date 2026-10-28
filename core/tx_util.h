/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — small shared helpers: random numbers, clamps, fast curves. */
#ifndef TX_UTIL_H
#define TX_UTIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef TX_FM1
/* The FM-1 has no C library: fm1-base's fastmath.h (single precision, branch-light) stands in for libm, and
 * felucca-bsp's libc.c gives memset, memcpy and memcmp. */
#include "fastmath.h"
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#define sinf(x) fm_sinf(x)
#define cosf(x) fm_cosf(x)
#define tanf(x) fm_tanf(x)
#define expf(x) fm_expf(x)
#define exp2f(x) fm_exp2f(x)
#define logf(x) fm_logf(x)
#define powf(x, y) fm_powf(x, y)
#define sqrtf(x) fm_sqrtf(x)
#define floorf(x) fm_floorf(x)
#define fabsf(x) fm_fabsf(x)
#define fminf(a, b) fm_minf(a, b)
#define fmaxf(a, b) fm_maxf(a, b)
#define lrintf(x) ((long)fm_floorf((x) + 0.5f))
#define isfinite(x) ((x) == (x) && fm_fabsf(x) < 3.0e38f)
#else
#include <string.h>
#include <math.h>
#endif

#define TX_PI 3.14159265358979f
#define TX_TWO_PI 6.28318530717958647692f

static inline float tx_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int tx_clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float tx_lerp(float a, float b, float t) { return a + (b - a) * t; }

/* xorshift32: fast, deterministic per seed (the browser build and the FM-1 match). */
typedef struct { uint32_t s; } tx_rng;
static inline uint32_t tx_rand(tx_rng *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}
static inline float tx_randf(tx_rng *r) { return (float)(tx_rand(r) >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */
static inline float tx_randb(tx_rng *r) { return tx_randf(r) * 2.0f - 1.0f; }                          /* -1..1 */

static inline float tx_semis(float st) { return exp2f(st * (1.0f / 12.0f)); }

/* one-pole smoothing coefficient for a time constant in seconds */
static inline float tx_onepole(float seconds, float fs)
{
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - expf(-1.0f / (seconds * fs));
}

/* soft clip, cheap and smooth (tanh-like, exact at 0, saturates at +-1) */
static inline float tx_soft(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

#endif
