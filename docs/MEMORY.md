# Memory budget

## RAM

The FM-1 has about 511 KiB of RAM. Bubba Machine measured that the JieLi SDK needs about 50 KiB at boot and its heap must stay above 120 KiB, which leaves roughly 390 KiB for a firmware's static data.

Measured on the computer (`sizeof`, 32-bit layout), October 2026:

| Part | Size | Notes |
| --- | ---: | --- |
| Grain buffer and grains (`tx_grains`) | 158 KiB | 80 000 int16 samples at ~22 kHz = 3.6 s |
| Effects (`tx_fx`) | 118 KiB | delay 69 KiB (2 x 0.8 s int16, half rate), reverb lines 28 KiB, diffusers, pre-delay and two pitch shifters ~21 KiB |
| FM voices (`tx_fm`) | 6 KiB | 6 msfa voices and controllers |
| Parameters, sequencer, mod | 3 KiB | |
| Window tables | 6 KiB | 6 shapes x 257 floats |
| msfa tables | ~28 KiB | sine, exp2, frequency (Bubba Machine's figure) |
| **Total** | **~320 KiB** | of ~390 KiB available |

Still to measure on the device: stacks, the USB and screen buffers of the hardware layer.

If RAM runs short, the first things to shrink are `TX_DLY_LEN` (delay) and `TX_BUF_LEN` (grain buffer) in `core/tx_config.h`.

## Flash (writable area, ~276 KiB)

| Use | Size |
| --- | ---: |
| Updater staging (reserved) | 20 KiB |
| Global settings | 12 KiB |
| Boot log | 4 KiB |
| 12 patch slots, one 4 KiB sector each | 48 KiB |
| Safe-save spare sector | 4 KiB |
| Captured audio | ~224 KiB |
