/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — parameter state: the A and B snapshots, the morph between them, the modulated
 * values the DSP reads, and saving/loading patches.
 *
 * Values are stored "raw" (see tools/gen_params.py): continuous parameters 0..1000, integers as
 * themselves, enums as an index. The DSP reads tx_val() / tx_int(), which already include the
 * morph and the mod matrix. */
#ifndef TX_PARAM_H
#define TX_PARAM_H

#include <stdint.h>
#include <stddef.h>
#include "gen/tx_params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TX_NAME_LEN   16
#define TX_SNAP_A     0
#define TX_SNAP_B     1
#define TX_SNAP_BOTH  2

typedef struct {
    int16_t snap[2][TX_NPARAMS];  /* A and B */
    char name[TX_NAME_LEN];
    float base[TX_NPARAMS];       /* 0..1 after the morph */
    float eff[TX_NPARAMS];        /* 0..1 after the mod matrix: what the DSP reads */
    uint8_t dirty;                /* a raw value changed: recompute base */
} tx_pstate;

void  txp_init(tx_pstate *ps);                        /* every parameter at its default, A = B */
void  txp_set(tx_pstate *ps, int id, int snap, int raw);
int   txp_get(const tx_pstate *ps, int id, int snap);
void  txp_copy(tx_pstate *ps, int from, int to);      /* A -> B or B -> A */
void  txp_morph(tx_pstate *ps);                       /* base[] from A, B and the morph */

float txp_norm(int id, int raw);                      /* raw -> 0..1 */
int   txp_raw(int id, float norm);                    /* 0..1 -> raw (rounded, clamped) */
float txp_phys(int id, float norm);                   /* 0..1 -> real units (Hz, ms, %, the enum's value) */

static inline float tx_val(const tx_pstate *ps, int id) { return txp_phys(id, ps->eff[id]); }
static inline int   tx_int(const tx_pstate *ps, int id) { return txp_raw(id, ps->eff[id]); }

/* Patches: a self-describing record of (uid, A, B) triples, so patches saved by an older
 * firmware still load (missing parameters keep their defaults, unknown ones are skipped).
 * TX_PATCH_MAX bytes always fit one 4 KiB flash sector. */
#define TX_PATCH_MAX  (28u + TX_NPARAMS * 6u + 4u)
size_t txp_save(const tx_pstate *ps, uint8_t *buf, size_t cap);   /* bytes written, 0 if cap too small */
int    txp_load(tx_pstate *ps, const uint8_t *buf, size_t len);   /* 0 ok, -1 bad record */
uint32_t tx_crc32(const uint8_t *d, size_t n);

#ifdef __cplusplus
}
#endif

#endif
