/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the whole instrument behind one small API, shared by the FM-1 firmware, the
 * browser build and the tests.
 *
 *   sequencer -> FM voice -> ring buffer -> grain cloud -> SPACE -> out
 *                                ^------------- feedback ------------'
 *
 * There is one engine (msfa keeps global tables), so the API has no handle. */
#ifndef TX_ENGINE_H
#define TX_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "tx_config.h"
#include "tx_param.h"

#ifdef __cplusplus
extern "C" {
#endif

void tx_init(float fs, uint32_t seed);
/* n frames of stereo output, any n */
void tx_render(float *L, float *R, int n);

/* parameters: raw values (see tx_param.h). Edits go to the snapshot chosen by tx_edit_snap. */
void tx_set(int id, int raw);
int  tx_get(int id);
void tx_edit_snap(int snap);            /* TX_SNAP_A, TX_SNAP_B or TX_SNAP_BOTH */
int  tx_edit_snap_get(void);
void tx_copy_snap(int from, int to);
tx_pstate *tx_params(void);

/* transport and performance */
void tx_play(int on);
int  tx_playing(void);
void tx_freeze(int on);
void tx_note_on(int note, int vel);     /* played by hand (keys with SHIFT, or MIDI in) */
void tx_note_off(int note);
void tx_panic(void);                    /* everything off, buffer and effects cleared */
void tx_overload(void);                 /* the render ran late: shed grains for a while */

/* MIDI in: one complete message (status + data) */
void tx_midi_in(const uint8_t *msg, int len);
/* MIDI out: generated notes, 3 bytes each. Returns bytes written (multiple of 3). */
int  tx_midi_out(uint8_t *buf, int cap);

/* patches */
size_t tx_patch_save(uint8_t *buf, size_t cap);
int    tx_patch_load(const uint8_t *buf, size_t len);
void   tx_patch_init(void);

/* the live view */
typedef struct {
    uint8_t playing, frozen, step, steps;
    uint32_t pattern;
    float head;                         /* write head, 0..1 of the buffer */
    int ngrains;
    float gpos[TX_LIVE_GRAINS], gamp[TX_LIVE_GRAINS];
    int fm_voices;
    float last_note, last_vel;
    float mod[10];                      /* mod source values */
    float peak;                         /* output level */
    float bpm;
} tx_status;
void tx_get_status(tx_status *st);
void tx_get_overview(uint8_t *out, int bins);

#ifdef __cplusplus
}
#endif

#endif
