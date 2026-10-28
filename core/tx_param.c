/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <math.h>
#include "tx_param.h"

static int clampraw(int id, int raw)
{
    const tx_pdef *d = &tx_pdefs[id];
    return raw < d->rmin ? d->rmin : raw > d->rmax ? d->rmax : raw;
}

void txp_init(tx_pstate *ps)
{
    memset(ps, 0, sizeof *ps);
    for (int i = 0; i < TX_NPARAMS; i++)
        ps->snap[0][i] = ps->snap[1][i] = tx_pdefs[i].rdef;
    strcpy(ps->name, "Init");
    ps->dirty = 1;
    txp_morph(ps);
    memcpy(ps->eff, ps->base, sizeof ps->eff);
}

void txp_set(tx_pstate *ps, int id, int snap, int raw)
{
    if (id < 0 || id >= TX_NPARAMS) return;
    raw = clampraw(id, raw);
    /* parameters outside the morph live in A only: both snapshots get them */
    if (snap == TX_SNAP_BOTH || !(tx_pdefs[id].flags & TXF_MORPH)) {
        ps->snap[0][id] = ps->snap[1][id] = (int16_t)raw;
    } else {
        ps->snap[snap ? 1 : 0][id] = (int16_t)raw;
    }
    ps->dirty = 1;
}

int txp_get(const tx_pstate *ps, int id, int snap)
{
    if (id < 0 || id >= TX_NPARAMS) return 0;
    return ps->snap[snap == TX_SNAP_B ? 1 : 0][id];
}

void txp_copy(tx_pstate *ps, int from, int to)
{
    memcpy(ps->snap[to ? 1 : 0], ps->snap[from ? 1 : 0], sizeof ps->snap[0]);
    ps->dirty = 1;
}

float txp_norm(int id, int raw)
{
    const tx_pdef *d = &tx_pdefs[id];
    if (d->rmax == d->rmin) return 0.0f;
    return (float)(raw - d->rmin) / (float)(d->rmax - d->rmin);
}

int txp_raw(int id, float norm)
{
    const tx_pdef *d = &tx_pdefs[id];
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return d->rmin + (int)lrintf(norm * (float)(d->rmax - d->rmin));
}

float txp_phys(int id, float norm)
{
    const tx_pdef *d = &tx_pdefs[id];
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    switch (d->curve) {
    case TXC_LIN: return d->lo + norm * (d->hi - d->lo);
    case TXC_EXP: return d->lo * powf(d->hi / d->lo, norm);
    case TXC_INT: return (float)txp_raw(id, norm);
    default: {
        int i = txp_raw(id, norm);
        return d->values ? d->values[i] : (float)i;
    }
    }
}

void txp_morph(tx_pstate *ps)
{
    if (!ps->dirty) return;
    ps->dirty = 0;
    float m = txp_norm(TXP_MORPH, ps->snap[0][TXP_MORPH]);
    for (int i = 0; i < TX_NPARAMS; i++) {
        float a = txp_norm(i, ps->snap[0][i]);
        if (!(tx_pdefs[i].flags & TXF_MORPH) || ps->snap[0][i] == ps->snap[1][i]) {
            ps->base[i] = a;
            continue;
        }
        float b = txp_norm(i, ps->snap[1][i]);
        if (tx_pdefs[i].curve == TXC_ENUM)
            ps->base[i] = m < 0.5f ? a : b;            /* choices switch half way */
        else
            ps->base[i] = a + (b - a) * m;
    }
}

/* ------------------------------------------------------------ patches */

uint32_t tx_crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* layout: "TXTR" ver(1) 0 count(2) name(16) reserved(4) | count * (uid, A, B) | crc32 */
#define HDR 28u

size_t txp_save(const tx_pstate *ps, uint8_t *buf, size_t cap)
{
    size_t n = HDR + TX_NPARAMS * 6u + 4u;
    if (cap < n) return 0;
    memset(buf, 0, HDR);
    memcpy(buf, "TXTR", 4);
    buf[4] = 1;
    put16(buf + 6, TX_NPARAMS);
    memcpy(buf + 8, ps->name, TX_NAME_LEN);
    uint8_t *p = buf + HDR;
    for (int i = 0; i < TX_NPARAMS; i++, p += 6) {
        put16(p, tx_pdefs[i].uid);
        put16(p + 2, (uint16_t)ps->snap[0][i]);
        put16(p + 4, (uint16_t)ps->snap[1][i]);
    }
    uint32_t c = tx_crc32(buf, (size_t)(p - buf));
    put16(p, (uint16_t)c);
    put16(p + 2, (uint16_t)(c >> 16));
    return n;
}

int txp_load(tx_pstate *ps, const uint8_t *buf, size_t len)
{
    if (len < HDR + 4 || memcmp(buf, "TXTR", 4) || buf[4] != 1) return -1;
    size_t count = get16(buf + 6);
    size_t body = HDR + count * 6u;
    if (len < body + 4) return -1;
    uint32_t c = (uint32_t)get16(buf + body) | ((uint32_t)get16(buf + body + 2) << 16);
    if (c != tx_crc32(buf, body)) return -1;

    txp_init(ps);
    memcpy(ps->name, buf + 8, TX_NAME_LEN);
    ps->name[TX_NAME_LEN - 1] = 0;
    const uint8_t *p = buf + HDR;
    for (size_t k = 0; k < count; k++, p += 6) {
        uint16_t uid = get16(p);
        for (int i = 0; i < TX_NPARAMS; i++) {
            if (tx_pdefs[i].uid != uid) continue;
            ps->snap[0][i] = (int16_t)clampraw(i, (int16_t)get16(p + 2));
            ps->snap[1][i] = (int16_t)clampraw(i, (int16_t)get16(p + 4));
            break;
        }
    }
    ps->dirty = 1;
    txp_morph(ps);
    memcpy(ps->eff, ps->base, sizeof ps->eff);
    return 0;
}
