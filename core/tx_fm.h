/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the FM voice: msfa's 6-operator DX7 engine in C (third_party/fm6, Apache-2.0) plus a voice
 * amp envelope, a post-FM filter per voice, unison detune and glide. See tx_fm.c. */
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
/* the 155-byte DX7 single voice the SOUND parameters describe, for velocity vel */
void tx_fm_build_patch(const tx_pstate *ps, int vel, uint8_t out[155]);

#ifdef __cplusplus
}
#endif

#endif
