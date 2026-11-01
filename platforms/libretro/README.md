# Felucca as a libretro core (beta)

Felucca as a libretro core, for RetroArch and the other libretro frontends: the same 240 x 240 UI and the FM-1's panel
beside it, the sound, a gamepad, the mouse or touch, the keyboard, MIDI in, plugin engines, and the flash kept in a
file. It is a frontend of the host (`platforms/host`: the firmware against a simulated FM-1) with the pieces the desktop
frontends share (`platforms/common`). It has run in RetroArch on macOS so far.

Not here yet: MIDI clock and SysEx (MIDI in takes channel messages), USB, save states, the frontend's reset.

## Build

A C compiler, and Python 3 with Pillow and fontTools for `build/gen`.

```
./build.sh libretro                # -> build/libretro/felucca_libretro.{so,dylib,dll} and felucca_libretro.info
retroarch -L build/libretro/felucca_libretro.dylib
```

The core starts without content. To list it in RetroArch's menus, copy the core to RetroArch's `cores` folder and the
`.info` file to its `info` folder.

`platforms/libretro/lr_check.c` runs the core without a frontend (`tests/run_tests.sh` builds and runs it): a second
of sound and pictures, a key, PLAY, a save kept for the next start, the layout.

| Core option | |
|---|---|
| Layout | `wide` (the screen left, the panel right, 520 x 240), `tall` (the panel under the screen, 272 x 500), `none` (the screen alone) |

**The flash** is `felucca_flash.bin` in the frontend's save folder (in RetroArch, `saves/Felucca/`), read at start,
written a second after a change and when the core is closed. It is the desktop build's file: copy it between them.
(RetroArch does not keep the save RAM of a core without content, so the core keeps its own file.)

**Plugins**: the plugin libraries in `Felucca/plugins` in the frontend's system folder (in RetroArch,
`system/Felucca/plugins`), loaded once, when the core first starts; the numbers given to plugins of their own in
`felucca_plugins.txt` beside the flash file ([docs/PLUGINS.md](../../docs/PLUGINS.md)).

## Playing

**The RetroPad**: the D-pad moves the cursor between the panel's controls, the left stick an arrow (the one moved last
is used); B (the bottom button) holds what the cursor is on (or clicks where the arrow is), Y latches it, A lets every
latch go, L / R turn the knob it is on (SELECT elsewhere), START is PLAY, SELECT HOME.

**The mouse** (or touch) on the panel, as on the desktop: a key or a button is held while pressed, a knob turns by a
drag up or down or by the wheel; the right button latches a key or a button.

**The keyboard** as the desktop build's (see [platforms/sdl](../sdl/README.md)): Z .. O the keys, the number row
the buttons, the arrows and F1 .. F12 the knobs, Tab the cursor. In RetroArch, turn on Game Focus (Scroll Lock) so its
hotkeys let the keys through.

**On the DRUM grid** (SEQ > STEP on a DRUM track) the keys are drawn as the grid's pads, as the FM-1's keys work there:
the 16 steps of the page in two rows, and above them the lanes, ACC and the page down / up. The keys light as the
FM-1's do.

**MIDI in** comes through the frontend (in RetroArch: Settings > Audio > MIDI, the input device).
