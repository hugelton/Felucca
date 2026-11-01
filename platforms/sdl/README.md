# Felucca on a desktop (SDL2, beta)

Felucca on macOS and Linux with SDL2: the same 240 x 240 UI and the FM-1's panel beside it, the sound, the keyboard,
the mouse, a gamepad, MIDI in, plugin engines, and the flash kept in a file. It is a frontend of the host
(`platforms/host`: the firmware against a simulated FM-1) with the pieces the desktop frontends share
(`platforms/common`: the panel, the keyboard and gamepad map, the flash file, MIDI's bytes, the plugin loader).

Not here yet: MIDI clock and SysEx (MIDI in takes channel messages), USB, the serial console.

## Build

A C compiler and SDL2 (Debian / Ubuntu: `libsdl2-dev`, and `libasound2-dev` for MIDI; macOS: `brew install sdl2`,
MIDI through CoreMIDI), and Python 3 with Pillow and fontTools for `build/gen` (`./build.sh gen`).

```
./build.sh sdl                              # -> build/sdl/felucca (gen first if needed)
build/sdl/felucca                         # 1040 x 480, the flash in ./felucca_flash.bin
build/sdl/felucca my.bin --size 1280 720
build/sdl/felucca --fullscreen
build/sdl/felucca --layout none --size 720 720
```

| Option | |
|---|---|
| `FLASH` | the flash image file (default `felucca_flash.bin`), read at start, written a second after a change and at the end |
| `--size W H` | the window's size, set at start; the layout that shows the panel larger (WIDE: the screen left, the panel right; TALL: the screen above it) |
| `--fullscreen` | the display's size (or `--size`'s) |
| `--layout wide\|tall\|none` | that layout (`none`: the screen alone) |
| `--no-midi` | no MIDI in |
| `--plugins DIR` | the plugin engines' folder (default `./plugins`): every `.so`, `.dylib` or `.dll` there |

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

## Plugins

Every plugin library in the plugins folder is loaded at start ([docs/PLUGINS.md](../../docs/PLUGINS.md)). A plugin of
its own (a new engine) is listed on the ENG knob after Felucca's engines; one that replaces an engine of Felucca's plays
as that engine. The numbers given to plugins of their own are kept in `felucca_plugins.txt` beside the flash file.
A library built for another platform, or one that the firmware refuses, is named on stderr and skipped.

## Checks without a window

```
build/sdl/felucca --headless 3000      # the heavy song of platforms/web/emu_test.mjs, then a user preset saved (SAVE OCT+ OCT+)
build/sdl/felucca f.bin --headless 1000   # twice on one file: the second finds the preset at boot
build/sdl/felucca --panel-check        # the layouts, the hits, the cursor's reach; PLAY, MASTER, a latch, the arrow
build/sdl/felucca --midi-check 4000    # what MIDI in brought, and the sound of it
build/sdl/felucca --shot f.bmp --size 1280 720 [--marks]   # the window as it would draw it (the cursor, a latch, the arrow)
build/sdl/felucca --engines-check --plugins DIR   # each engine a plugin can replace, every preset: a hash a line
build/sdl/felucca --plugins-check --plugins DIR   # the plugins of their own listed and playing; a missing one: NO PLUGIN
build/sdl/felucca --project-check --plugins DIR   # a project keeps its plugin's id across devices
```
