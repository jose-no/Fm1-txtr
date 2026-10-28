# fm1-txtr

A generative granular texture synth firmware for the M-VAVE FM-1.

A Euclidean sequencer picks notes from a scale and plays them on the FM-1's 6-operator FM voice. The voice streams into a short ring buffer, and a grain cloud sculpts that buffer into evolving textures. Drift loosens the timing, Freeze holds the buffer, and feedback writes the cloud back into its own buffer for drones.

```
sequencer -> FM voice -> ring buffer -> grain cloud -> SPACE -> out
                              ^------------ feedback -----------'
```

## Status

| Part | State |
| --- | --- |
| Parameter definitions (254 parameters, 38 sub-pages) | done: `params/params.py` |
| Engine: sequencer, FM voice, grains, effects, mod matrix, A/B morph, patches | done: `core/`, tested on the computer |
| Browser build and control page | done: `web/` |
| FM-1 firmware on fm1-base (screen UI, 12 patch slots, web installer) | done: `app.c`, built by CI |
| Control page talking to the FM-1 over USB | not started (protocol drafted in `docs/PROTOCOL.md`) |

## Install on the FM-1

Open the web installer in Chrome or Edge, connect the FM-1 by USB and press Install:
**https://jose-no.github.io/Fm1-txtr/install/**. The same site has the [manual](https://jose-no.github.io/Fm1-txtr/manual/)
and the FM-1 app [running in the browser](https://jose-no.github.io/Fm1-txtr/emu/). Going back to M-VAVE's firmware
is on the installer page too; see `docs/INSTALL.md`.

The site is published from the `site` branch by the Firmware workflow (Settings > Pages > Deploy from a branch: `site`).

## Try the control page in the browser

The browser build is already in the repo (`web/txtr.wasm`), so nothing needs compiling:

```
python3 -m http.server -d web 8000
```

Open http://localhost:8000 in Chrome or Edge, press **Start audio**, then **Play**. The page has every parameter on one screen, a live view of the buffer and its grains, the Euclidean ring, A/B snapshots with morph, patch save/load (`.txtr` files) and MIDI in/out through Web MIDI.

## Build

```
make            # generate tables, build and run the tests (gcc/g++)
make render     # out/demo.wav: 30 s of the default patch
make web        # web/txtr.wasm (needs Zig: pip install ziglang)
make gen        # after editing params/params.py
```

## Layout

| Path | What |
| --- | --- |
| `params/params.py` | every parameter, defined once: name, page, knob, range, default |
| `tools/gen_params.py` | turns it into `core/gen/tx_params.*` (firmware) and `web/gen/params.js` (page) |
| `core/` | the engine in portable C/C++, no malloc: `tx_seq` sequencer, `tx_fm` FM voice, `tx_grain` cloud, `tx_fx` SPACE, `tx_mod` modulation, `tx_param` snapshots/morph/patches, `tx_engine` the API |
| `third_party/msfa/` | the DX7 engine (Apache-2.0), as vendored by Bubba Machine with its speed fixes for the AC79 chip |
| `web/` | control page, AudioWorklet, WebAssembly wrapper |
| `tests/` | `make test` |
| `docs/` | memory budget, USB protocol draft |

## Design notes

- The grain buffer is mono int16 at half the engine rate (about 22 kHz): 3.6 s in 156 KiB. One setting in `core/tx_config.h` changes it.
- Patches store (parameter id hash, A value, B value) triples, so patches saved by older firmware still load. One patch is 1.5 KB and fits a 4 KiB flash sector.
- The spec this follows: [FM-1 Granular Texture Synth spec](https://claude.ai/artifact/XSGWPjJB3XipheUJXCXCs7).

## Credits and license

GPL-3.0-or-later (see `LICENSE`). The FM engine is msfa (Google, Apache-2.0) in the Synth_Dexed version, with the FM-1 speed fixes from [Bubba Machine](https://github.com/Erbubar23/bubba-machine) (GPL-3.0); the way it is driven follows Bubba Machine's `core/bm_fm.cpp`. The grain engine is original code whose feature set is modelled on Mutable Instruments Clouds. M-VAVE and FM-1 are trademarks of their owners; this project is not affiliated with them.
