# Installing fm1-txtr on an FM-1

fm1-txtr installs over the FM-1's USB cable, from M-VAVE's stock firmware or from another fm1-base / Felucca-based
firmware. Nothing needs opening. Installing is at your own risk; the steps to go back are below.

## Before you start

- Chrome or Edge on a computer (Web MIDI). Safari and Firefox can't install.
- A USB data cable (not a charge-only one).
- Download M-VAVE's own FM-1 firmware now, so you have it if you want to go back:
  [FM-1.fwsc](https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc), from
  [m-vave.com/download](https://www.m-vave.com/download).
- If your FM-1 is on an old stock firmware, update it to V15 first with M-VAVE's updater.

## Install

1. Open the installer page: `install/` on the fm1-txtr site (GitHub Pages from the `site` branch), or the `site`
   branch / the `fm1-txtr-installer-site` artifact of the latest
   [Firmware run](https://github.com/Jose-no/fm1-txtr/actions/workflows/firmware.yml) served locally:
   unzip it, run `python3 -m http.server 8000` inside it, open http://localhost:8000/install/.
2. Connect the FM-1 by USB and switch it on.
3. Press Install and allow MIDI access when Chrome asks. Leave the cable in until the page says it's done; the FM-1
   restarts into fm1-txtr.

From a terminal instead: `pip install mido python-rtmidi`, then
`python3 fm1-base/tools/fm1_install.py fm1-txtr-0.1.fwsc`.

## First sound

Press PLAY. Turn MASTER up. SELECT changes page, ALGORITHM the sub-page, K1 to K4 the values. The
[manual](MANUAL.md) has every control.

## Going back, or if something goes wrong

- **Back to stock:** on the installer page, "Back to the stock firmware": choose the `FM-1.fwsc` you downloaded and
  press the button. Or use M-VAVE's own updater, M-UPGRADE (m-vave.com, Downloads, PC Software).
- **It doesn't start properly:** after two starts that fail within ten seconds, the FM-1 starts in safe mode (no
  sound, USB on). The installer works from there: put fm1-txtr on again, or go back to stock.
- **No sound, or crackles:** the screen's bottom right shows the audio load. If it sits near 100 %, lower GRAIN,
  Engine, Grain cap, or SOUND, Voice, Voices. Grains are also shed automatically when the audio runs late.
- **It doesn't start at all:** fm1-base's `fm1_rescue` (felucca-bsp tools) writes the stock firmware back through the
  chip's own update mode, from a Mac.
