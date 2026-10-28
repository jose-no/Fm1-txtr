/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — the browser build's exports (WebAssembly, run inside an AudioWorklet).
 * Everything goes through small static buffers so JavaScript reads them as typed arrays. */
#include <string.h>
#include "tx_engine.h"
#include "tx_param.h"

#define EXPORT __attribute__((visibility("default")))
#define FRAMES 128

static float out_l[FRAMES], out_r[FRAMES];
/* status, flat: see web/worklet.js for the layout */
static float status[16 + 10 + 2 * TX_LIVE_GRAINS];
static uint8_t overview[512];
static uint8_t patch[TX_PATCH_MAX];
static uint8_t midi[96];

EXPORT void txw_init(float fs, uint32_t seed) { tx_init(fs, seed); }
EXPORT float *txw_left(void) { return out_l; }
EXPORT float *txw_right(void) { return out_r; }
EXPORT void txw_render(int n) { tx_render(out_l, out_r, n > FRAMES ? FRAMES : n); }

EXPORT int txw_nparams(void) { return TX_NPARAMS; }
EXPORT void txw_set(int id, int raw) { tx_set(id, raw); }
EXPORT int txw_get(int id) { return tx_get(id); }
EXPORT int txw_get_snap(int id, int snap) { return txp_get(tx_params(), id, snap); }
EXPORT float *txw_eff(void) { return tx_params()->eff; }
EXPORT void txw_edit_snap(int s) { tx_edit_snap(s); }
EXPORT void txw_copy_snap(int from, int to) { tx_copy_snap(from, to); }

EXPORT void txw_play(int on) { tx_play(on); }
EXPORT int txw_playing(void) { return tx_playing(); }
EXPORT void txw_freeze(int on) { tx_freeze(on); }
EXPORT void txw_note_on(int n, int v) { tx_note_on(n, v); }
EXPORT void txw_note_off(int n) { tx_note_off(n); }
EXPORT void txw_panic(void) { tx_panic(); }

EXPORT uint8_t *txw_midi_buf(void) { return midi; }
EXPORT void txw_midi_in(int len) { tx_midi_in(midi, len); }
EXPORT int txw_midi_out(void) { return tx_midi_out(midi, sizeof midi); }

EXPORT uint8_t *txw_patch_buf(void) { return patch; }
EXPORT int txw_patch_cap(void) { return (int)sizeof patch; }
EXPORT int txw_patch_save(void) { return (int)tx_patch_save(patch, sizeof patch); }
EXPORT int txw_patch_load(int len) { return tx_patch_load(patch, (size_t)len); }
EXPORT void txw_patch_init(void) { tx_patch_init(); }
EXPORT char *txw_patch_name(void) { return tx_params()->name; }

EXPORT float *txw_status(void)
{
    tx_status st;
    tx_get_status(&st);
    float *s = status;
    s[0] = st.playing; s[1] = st.frozen; s[2] = st.step; s[3] = st.steps;
    s[4] = (float)(st.pattern & 0xFFFF); s[5] = (float)(st.pattern >> 16);
    s[6] = st.head; s[7] = (float)st.ngrains; s[8] = (float)st.fm_voices;
    s[9] = st.last_note; s[10] = st.last_vel; s[11] = st.peak; s[12] = st.bpm;
    s[13] = (float)tx_edit_snap_get(); s[14] = 0; s[15] = 0;
    for (int i = 0; i < 10; i++) s[16 + i] = st.mod[i];
    for (int i = 0; i < TX_LIVE_GRAINS; i++) {
        s[26 + i] = i < st.ngrains ? st.gpos[i] : -1.0f;
        s[26 + TX_LIVE_GRAINS + i] = i < st.ngrains ? st.gamp[i] : 0.0f;
    }
    return status;
}

EXPORT uint8_t *txw_overview(int bins)
{
    if (bins > (int)sizeof overview) bins = (int)sizeof overview;
    tx_get_overview(overview, bins);
    return overview;
}
