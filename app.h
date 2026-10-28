/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr on the M-VAVE FM-1: the app's half of the contract with fm1-base (fm1-base/device/unity.c,
 * fm1-base/host/plat_host.c). The engine is in core/; this app is the panel, the screen and the patch slots. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TXTR"
#define APP_TAGLINE "granular texture synth"
#define APP_SPLASH_RGB RGB(90, 170, 255)
#define APP_PALETTE 2u                   /* gfx.c PALETTES: CYAN */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */

#define TXTR_SLOTS 12                    /* patch slots, keys 1..12 (F3..E4) */

/* the store: settings, the panel calibration (the platform's), the 12 patches. Each object takes two 4 KiB
 * sectors (A/B) from its offset, in the data area 0x97000..0xDFFFF or Felucca's settings pair at 0xFC000 */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PATCH0, OBJ_NOBJ = OBJ_PATCH0 + TXTR_SLOTS };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, \
    0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, \
    0xA3000u, 0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u}

/* the render ran late (over 90 % of its time): shed grains for a while */
#define APP_OVERLOAD
void app_overload(void);
/* the control page's SysEx (docs/PROTOCOL.md): parameters over USB */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
