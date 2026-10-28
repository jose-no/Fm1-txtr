/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — sizes and rates. Everything is static: no malloc anywhere in core/.
 *
 * RAM on the FM-1: about 320 KiB of the ~390 KiB available, measured in docs/MEMORY.md.
 * The two big ones are the grain buffer (TX_BUF_LEN * 2 bytes) and the delay (2 * TX_DLY_LEN * 2). */
#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#define TX_BLOCK        64          /* frames per render block (msfa computes 64 at a time) */

/* Grain buffer: mono int16 at half the engine rate. */
#define TX_BUF_DECIM    2
#define TX_BUF_LEN      80000u      /* ~3.6 s at 22.05 kHz */
#define TX_MAX_GRAINS   48

#define TX_FM_VOICES    6

/* Delay: two int16 lines at half rate. */
#define TX_DLY_DECIM    2
#define TX_DLY_LEN      17664u      /* ~0.8 s at 22.05 kHz */

#define TX_LIVE_GRAINS  32          /* grains reported to the UI per status frame */

#endif
