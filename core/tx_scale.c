/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tx_scale.h"

#define M(a) (uint16_t)(a)
static uint16_t bits(const int8_t *s, int n)
{
    uint16_t m = 0;
    for (int i = 0; i < n; i++) m |= (uint16_t)(1u << s[i]);
    return m;
}

/* same order as SCALES in params/params.py */
static const int8_t S_MAJOR[] = {0, 2, 4, 5, 7, 9, 11};
static const int8_t S_MINOR[] = {0, 2, 3, 5, 7, 8, 10};
static const int8_t S_DORIAN[] = {0, 2, 3, 5, 7, 9, 10};
static const int8_t S_PHRYG[] = {0, 1, 3, 5, 7, 8, 10};
static const int8_t S_LYD[] = {0, 2, 4, 6, 7, 9, 11};
static const int8_t S_MIXO[] = {0, 2, 4, 5, 7, 9, 10};
static const int8_t S_LOCR[] = {0, 1, 3, 5, 6, 8, 10};
static const int8_t S_HMIN[] = {0, 2, 3, 5, 7, 8, 11};
static const int8_t S_MMIN[] = {0, 2, 3, 5, 7, 9, 11};
static const int8_t S_MAJP[] = {0, 2, 4, 7, 9};
static const int8_t S_MINP[] = {0, 3, 5, 7, 10};
static const int8_t S_WHOLE[] = {0, 2, 4, 6, 8, 10};
static const int8_t S_HIRA[] = {0, 2, 3, 7, 8};
static const int8_t S_INSEN[] = {0, 1, 5, 7, 10};

uint16_t tx_scale_mask(int scale, int custom_mask)
{
    switch (scale) {
    case 0: return bits(S_MAJOR, 7);
    case 1: return bits(S_MINOR, 7);
    case 2: return bits(S_DORIAN, 7);
    case 3: return bits(S_PHRYG, 7);
    case 4: return bits(S_LYD, 7);
    case 5: return bits(S_MIXO, 7);
    case 6: return bits(S_LOCR, 7);
    case 7: return bits(S_HMIN, 7);
    case 8: return bits(S_MMIN, 7);
    case 9: return bits(S_MAJP, 5);
    case 10: return bits(S_MINP, 5);
    case 11: return bits(S_WHOLE, 6);
    case 12: return bits(S_HIRA, 5);
    case 13: return bits(S_INSEN, 5);
    case 14: return M(0xFFF);
    default: return (uint16_t)((custom_mask & 0xFFF) | 1);   /* the root is always in */
    }
}

int tx_scale_degrees(uint16_t mask, int8_t out[12])
{
    int n = 0;
    mask |= 1;
    for (int s = 0; s < 12; s++)
        if (mask & (1u << s)) out[n++] = (int8_t)s;
    return n;
}

int tx_scale_snap(uint16_t mask, int semis)
{
    mask |= 1;
    for (int d = 0; d < 12; d++) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int s = semis + sgn * d;
            int pc = ((s % 12) + 12) % 12;
            if (mask & (1u << pc)) return s;
        }
    }
    return semis;
}
