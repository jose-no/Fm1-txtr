/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — sizes and rates. Everything is static: no malloc anywhere in core/.
 *
 * RAM on the FM-1 (fm1-base): the big buffers (~240 KiB) live in the 336 KiB pool, the rest in the 96 KiB of
 * .data/.bss; see docs/MEMORY.md. The two big ones are the grain buffer (TX_BUF_LEN * 2 bytes) and the delay
 * (2 * TX_DLY_LEN * 2). */
#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#define TX_BLOCK        64          /* frames per render block (msfa computes 64 at a time) */

/* Grain buffer: mono int16 at half the engine rate. */
#define TX_BUF_DECIM    2
#define TX_BUF_LEN      72000u      /* ~3.26 s at 22.05 kHz: 141 KiB */
#define TX_MAX_GRAINS   48

#define TX_FM_VOICES    6

/* Delay: two int16 lines at half rate. */
#define TX_DLY_DECIM    2
#define TX_DLY_LEN      11264u      /* ~0.51 s at 22.05 kHz: 44 KiB for both lines */

#define TX_LIVE_GRAINS  32          /* grains reported to the UI per status frame */

/* the big buffers (grain buffer, delay, reverb) go in the FM-1's RAM pool, not in .bss */
#ifdef TX_FM1
#define TX_POOL __attribute__((section(".pool")))
#else
#define TX_POOL
#endif

#endif
