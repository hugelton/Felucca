# Felucca on the M5Stack CoreS3 (research)

Felucca (this tree's sources, three folders up) on the M5Stack CoreS3. MVP: the same 240 x 240 UI in the middle of
the screen and the sound on the built-in speaker. Nothing of Felucca's sources changes: `main/felucca_m5.c` builds them as one file, the way Felucca's browser emulator
(`web/emu/felucca_web.c`) does, and `main/cores3_hw.c` is the CoreS3's side (power, LCD, speaker, touch), `main/usb_audio.c` USB audio.

The UI runs on core 0, the audio on core 1. Felucca renders at 44.1 kHz; the speaker plays it resampled to
44642 Hz (the AW88298's PLL locks only on a whole clock divider) and USB gets it resampled to 48 kHz: the CoreS3 is a
USB audio input device (UAC 2.0, stereo 16-bit; macOS lists it as "microphone").

Controls: the left side VOL -, the right side VOL + (Felucca's MASTER), a tap on the UI PLAY / STOP. At power-on a
demo pattern (bass, chords, drums) is written into the tracks and plays. Nothing is saved over a power cycle. Not
here: keys, knobs, USB MIDI, TRS MIDI.

## Build

ESP-IDF 5.5, Python 3 with Pillow and fontTools.

```
git clone --recurse-submodules -b research/platforms https://github.com/hugelton/Felucca.git
cd Felucca
platforms/gen.sh                  # build/gen: fonts, tables, samples
cd platforms/m5stack
. ~/esp/esp-idf-v5.5.5/export.sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

USB belongs to the audio device once it runs: to flash again, hold the reset button about 3 s (green LED) for
the download mode first. Without USB audio (the serial log) the log prints the load and the level every 2 s.

## Licence

GPL-3.0-only, as Felucca. The register values in `main/cores3_hw.c` follow M5Unified / M5GFX (MIT).
