/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the FM voice: msfa's 6-operator DX7 engine (third_party/msfa, Apache-2.0) plus a
 * voice amp envelope, a post-FM filter per voice, unison detune and glide.
 *
 * Patches are built from the SOUND parameters every block; voices that are sounding take the
 * edit live. All memory is static. */
#ifndef TX_FM_H
#define TX_FM_H

#include <stdint.h>
#include "tx_param.h"

#ifdef __cplusplus
extern "C" {
#endif

void tx_fm_init(float fs);
void tx_fm_note_on(const tx_pstate *ps, int note, int vel);
void tx_fm_note_off(int note);
void tx_fm_all_off(void);
/* adds TX_BLOCK mono samples to out (about +-1 full scale per voice) */
void tx_fm_render(const tx_pstate *ps, float *out);
int  tx_fm_active(void);                  /* voices sounding */
/* the 156-byte DX7 voice (VCED) the SOUND parameters describe, for velocity vel */
void tx_fm_build_patch(const tx_pstate *ps, int vel, uint8_t out[156]);

#ifdef __cplusplus
}
#endif

#endif
