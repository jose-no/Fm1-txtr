# fm1-txtr

fm1-txtr turns the M-VAVE FM-1 into a generative granular texture synth. A Euclidean sequencer plays notes from a
scale on the FM-1's 6-operator FM voice; the voice streams into a short buffer, and a cloud of grains sculpts it into
evolving textures. Drift loosens the timing, Freeze holds the buffer, and feedback writes the cloud back into itself.

![fm1-txtr playing: the SEQ page's four parameters, the Euclidean ring, the buffer and its grains](manual/img/screen-playing.png "fm1-txtr on the FM-1's screen")

Version {{VERSION}}. This is an early build, tested in the simulator and the browser, not yet on many FM-1s.
Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the pages, the controls, the patch slots.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 fm1-base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/Jose-no/fm1-txtr): GPL-3.0-or-later. Built on fm1-base and felucca-bsp (MPL-2.0), from
  Felucca by Leo Kuroshita, Hügelton Instruments; the FM voice is msfa (Apache-2.0) in Felucca's C port.

M-VAVE and FM-1 are trademarks of their owners. fm1-txtr is not affiliated with M-VAVE.
