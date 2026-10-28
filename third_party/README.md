# Third-party code

| Folder | What | From | License | Changes |
| --- | --- | --- | --- | --- |
| `msfa/` | 6-operator DX7-compatible FM engine ("music synthesizer for android", Google, maintained in Dexed) | [Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) `src/`, as vendored by [Bubba Machine](https://github.com/Erbubar23/bubba-machine) `third_party/msfa` | Apache-2.0 (`msfa/LICENSE`) | Bubba Machine's: no 64-bit multiplies in the inner loop (`sin.h`, `fm_op_kernel.cpp`, marked in each file). `EngineMkI` is not included. |
