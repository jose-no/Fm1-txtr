# USB protocol (draft)

How the control page will talk to the FM-1. Nothing on the device implements this yet; the page currently drives the browser build of the engine instead.

## Transport

USB-MIDI, no drivers. The page uses Web MIDI with SysEx permission (Chrome, Edge). Messages use the non-commercial manufacturer ID `0x7D` and the signature `T X` (`0x54 0x58`), so other gear ignores them:

```
F0 7D 54 58 <cmd> <data...> F7
```

Data bytes are 7-bit. Values wider than 7 bits are sent as 7-bit groups, least significant first.

## Commands

| cmd | Direction | Data | Meaning |
| --- | --- | --- | --- |
| `01` | page to device | – | hello: the device answers `41` |
| `41` | device to page | version (2), param count (2), table hash (4) | the page refuses to edit if the table hash differs from `web/gen/params.js` |
| `02` | page to device | id (2), snapshot (1), raw (3, signed) | set a parameter |
| `42` | device to page | id (2), snapshot (1), raw (3) | a parameter changed on the device (knob turned) |
| `03` | page to device | – | send every value: the device answers with a `43` per parameter |
| `04` | page to device | slot (1) | save the current patch to slot 1–12 |
| `05` | page to device | slot (1) | load slot 1–12 |
| `06` / `46` | both | offset (3), bytes... | patch transfer (the 1.5 KB `.txtr` record) |
| `07` / `47` | both | offset (3), bytes... | captured audio transfer (ADPCM) |
| `48` | device to page | status frame | the live view, about 20 per second, same fields as `tx_status` |
| `09` | page to device | play (1), freeze (1) | transport |

Knob streams are throttled to one `42` per parameter every 20 ms. The main parameters will also answer to MIDI CC/NRPN for DAW automation; that map is still to be written.
