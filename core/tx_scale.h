/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-txtr — scales as 12-bit masks (bit n = semitone n above the root). */
#ifndef TX_SCALE_H
#define TX_SCALE_H

#include <stdint.h>

#define TX_SCALE_CUSTOM 15        /* index of "Custom" in the Scale enum */

uint16_t tx_scale_mask(int scale, int custom_mask);
int      tx_scale_degrees(uint16_t mask, int8_t out[12]);       /* semitones of each degree, ascending; returns count */
/* nearest semitone offset (from 0) that lies on the scale, searching across octaves */
int      tx_scale_snap(uint16_t mask, int semis);

#endif
