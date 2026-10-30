# Felucca on a desktop (SDL2, beta)

Felucca's sources on Linux (and macOS, for development) with SDL2: the same 240 x 240 UI and the FM-1's panel beside
it, the sound, the keyboard, the mouse, a gamepad, MIDI in, and the flash kept in a file. It builds the browser
emulator's file (`web/emu/felucca_web.c`: the firmware against a simulated FM-1) natively, as
`web/emu/native_check.c` does; nothing of the firmware changes for it. The panel (`platforms/common/panel.c`) is
meant for the other platforms without one of their own too.

Not here yet: MIDI clock and SysEx (MIDI in takes channel messages), USB, the serial console.

## Build

A C compiler and SDL2 (Debian / Ubuntu: `libsdl2-dev`, and `libasound2-dev` for MIDI; macOS: `brew install sdl2`,
MIDI through CoreMIDI), after one `./build.sh` (`build/gen`: the tables, the fonts, the samples).

```
platforms/linux/build.sh                    # -> build/linux/felucca
build/linux/felucca                         # 1040 x 480, the flash in ./felucca_flash.bin
build/linux/felucca my.bin --size 1280 720
build/linux/felucca --fullscreen
build/linux/felucca --layout none --size 720 720
```

| Option | |
|---|---|
| `FLASH` | the flash image file (default `felucca_flash.bin`), read at start, written a second after a change and at the end |
| `--size W H` | the window's size, set at start; the layout that shows the panel larger (WIDE: the screen left, the panel right; TALL: the screen above it) |
| `--fullscreen` | the display's size (or `--size`'s) |
| `--layout wide\|tall\|none` | that layout (`none`: the screen alone) |
| `--no-midi` | no MIDI in |

The screen keeps its 240 x 240 pixels; the panel is drawn in the same pixels by the firmware's own drawing (its fonts, its
rounded cells, the colours of MENU's palette), and the two are scaled to the window together: whole pixels kept
sharp, the fraction smoothed.

## Playing

**The keyboard**, as the browser emulator's page, plus the knobs it has only for the mouse:

| Keys | Control |
|---|---|
| Z X C V B N M , . / (white), S D F H J L ; ' (black) | the keys F3 .. A#4 |
| Q W E R T Y U I O | the keys B4 .. G5 |
| 1 .. 6 | FX SCL ENV LFO EDIT GLO |
| 7 8 9 0 - = | HOME SAVE ARP SEQ PLAY REC |
| [ ] | OCT- OCT+ |
| Up / Down | SELECT |
| Right / Left | ALGORITHM |
| F1 / F2 | PRESETS |
| F3 F4, F5 F6, F7 F8, F9 F10 | KNOB 1 .. 4 |
| F11 / F12 | MASTER |
| Tab | the cursor on / off: the arrows move it, Space holds what it is on, Return latches it, Page Up / Down turn its knob (SELECT elsewhere) |
| Escape | every latch let go |

**The mouse** (or touch) on the panel: a key or a button is held while pressed (drag across the keys to the next),
a knob turns by a drag up or down or by the wheel. The right button latches a key or a button (held until it is
clicked again: a button held while a key is played, with one mouse); a small blue mark shows a latch.

**A gamepad**: the D-pad moves the cursor, the left stick an arrow (the one moved last is used); A holds what the
cursor is on (or clicks where the arrow is), X latches, B lets every latch go, L / R turn, START is PLAY, BACK HOME.

**MIDI in**: a port named Felucca (ALSA sequencer; on macOS a CoreMIDI destination), and every source there at
start; notes, CC, program change, pressure and pitch bend, as from USB MIDI.

## Checks without a window

```
build/linux/felucca --headless 3000      # the heavy song of web/emu/emu_test.mjs, then a user preset saved (SAVE OCT+ OCT+)
build/linux/felucca f.bin --headless 1000   # twice on one file: the second finds the preset at boot
build/linux/felucca --panel-check        # the layouts, the hits, the cursor's reach; PLAY, MASTER, a latch, the arrow
build/linux/felucca --midi-check 4000    # what MIDI in brought, and the sound of it
build/linux/felucca --shot f.bmp --size 1280 720 [--marks]   # the window as it would draw it (the cursor, a latch, the arrow)
```
