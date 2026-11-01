# Architecture

Felucca is one program built for several platforms. The FM-1 is the reference: every release is tested on it. The
other platforms build the same sources against a simulated FM-1. Sound engines can be built into the firmware or
loaded at run time as plugins.

```
firmware/src           the application: engines' host, voices, FX, sequencer, UI, storage, MIDI, editor protocol
 ├─ firmware/engines   Felucca-Engines (MIT): six engines, felucca_engine.h, the plugin ABI felucca_plugin.h
 │    └─ plugins       engines built on their own, loaded at run time (any licence)
 ├─ the FM-1           firmware/hal + firmware/bsp (felucca-bsp, MPL-2.0): the hardware behind the fm1_* calls
 └─ platforms/host     a simulated FM-1 behind the same calls, and its API felucca_host.h
      ├─ platforms/web        WebAssembly in an AudioWorklet
      ├─ platforms/sdl        a desktop window (SDL2)
      ├─ platforms/libretro   a libretro core
      └─ platforms/common     what the frontends share: the panel, input, the flash file, MIDI, the plugin loader
```

## The layers

| Layer | Where | What it is | Licence |
| --- | --- | --- | --- |
| Application | `firmware/src/` | Everything Felucca does: the engines' host (`engines.c`, `voice.c`), the FX, the sequencer, the UI, storage and projects, MIDI, the web editor's protocol. C with no register access: the FM-1's glue (`main.c`, `lcd.c`, the `*_hw.c` files) calls the HAL, which the host simulates. | GPL-3.0-only |
| Engines | `firmware/engines/` ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)) | Six engines, their shared DSP and tables, the engine interface (`felucca_engine.h`) and the plugin interface (`felucca_plugin.h`). | MIT |
| FM-1 hardware | `firmware/bsp/` ([felucca-bsp](https://github.com/hugelton/felucca-bsp)), `firmware/hal/` | The chip (clock, interrupts, flash, USB, the second core, the boot entry, libc) and the FM-1's board (keys, LEDs, audio, display, ADC). The only place with register access (`tools/build.py` enforces it). | MPL-2.0 (bsp), GPL-3.0-only (hal) |
| Update loader | `firmware/loader/` | The small program that installs a package. | GPL-3.0-only |
| Host | `platforms/host/` | A simulated FM-1 (its clock, panel, LEDs, screen and flash) for every other platform, with one API, `felucca_host.h`. | GPL-3.0-only |
| Frontends | `platforms/web`, `platforms/sdl`, `platforms/libretro`, `platforms/common` | A window, sound, input and files for each platform; the panel drawn beside the screen, the keyboard and gamepad map, the flash file and the plugin loader they share. | GPL-3.0-only |

## One translation unit

Each build compiles one C file that `#include`s the others in a fixed order. For the FM-1 this is
`firmware/src/felucca.c`. For the host it is `platforms/host/felucca_host.c`, which a frontend includes. The order
matters: a file uses what the files before it define. Building this way lets the compiler see the whole program
(small and fast code on a 360 MHz DSP with 1 MiB of flash), and it keeps the HAL header-only.

The sound side (libc, engines, voices, FX, MIDI parsing, sequencer) is listed once for the host, in
`platforms/host/firmware.c`. The host tests (`tests/`) and the host build both use that list.

## Platforms

A platform is either a port of the HAL or a host frontend:

- **A HAL port** (the FM-1 today; other boards in felucca-bsp) gives the `fm1_*` functions that the application
  calls: time, the audio DMA and its interrupt, the key and encoder scan, the LEDs, the LCD, the flash, the ADC. It
  builds `felucca.c`. The FM-1's set is in `firmware/bsp/chip` and `firmware/bsp/boards/fm1`.
- **A host frontend** builds `platforms/host/felucca_host.c`, which already provides the HAL as a simulation, and talks
  to it through `felucca_host.h`: boot, render audio, send the controls and MIDI, read the screen and the LEDs, keep
  the flash. This is the easy way to bring Felucca to a computer, a handheld or an app.

[PORTING.md](PORTING.md) explains both.

The application knows nothing about the platform except through a few build flags:

| Flag | Set by | Meaning |
| --- | --- | --- |
| `FELUCCA_HOST` | `platforms/host/felucca_host.c` | A host build: no FM-1 plugin slots, no hardware crash counter. |
| `FELUCCA_PLUGINS` | the host (1), `firmware/src/felucca.c` (1 from 1.9) | Plugin engines are loaded at run time. |
| `FELUCCA_FLASH`, `FELUCCA_OTA`, `FELUCCA_CDC`, `FELUCCA_UAC`, `FELUCCA_UART`, … | `felucca.c`, `tools/build.py` | Optional parts of the FM-1 build ([BUILDING.md](../BUILDING.md)). |

## Engines and plugins

An engine is an `engine_t` (`felucca_engine.h`): its name, eight parameters with ranges and names, presets, the four
HOME knobs, and the functions the host calls per voice (`note_on`, `render`) and per block (`block`, `amp`, `post`).
The UI draws every engine from this description, so an engine needs no UI code of its own.

`firmware/src/engines.c` holds the table of engines, `ENGINES[]`. An engine's number is its index there. Saved sounds
store the number, so a number never moves.

A **plugin** is an engine built on its own against `felucca_plugin.h` (ABI 1) and loaded at run time:

- on a desktop, a shared library in a folder (`platforms/common/plugins.c`);
- on the FM-1, a blob in a flash slot outside the firmware's area (`firmware/src/plugin_fm1.c`), installed over USB.
  It runs in place from the flash.

The host (`firmware/src/plugin.c`) checks a plugin and gives it a slot. Each slot has its own `engine_t`, whose
functions call the plugin with a view of the part (`fe_track_t`) instead of Felucca's own track. A plugin either
takes the place of one of Felucca's engines (the same number: saved sounds play through it) or gets a new number of
its own (32 and up, kept for its id). A number that no plugin plays any more plays a sine and shows NO PLUGIN.

[PLUGINS.md](PLUGINS.md) is the guide; the ABI and its stability rules are in Felucca-Engines' `felucca_plugin.h`.

## Storage

The FM-1's 1 MiB flash holds the firmware, the plugin slots, and the store (settings, user presets, four projects,
user samples) in sectors written by `storage.c` with A/B copies. On a host the same bytes live in a file
(`platforms/common/flashfile.c`) or in the browser. The formats and their compatibility rules are in
[COMPATIBILITY.md](COMPATIBILITY.md).

## Tests

`tests/run_tests.sh` runs everything that can run on a computer: the engines' golden renders, every format's
loading, the UI drawn on every screen in every palette, MIDI, USB descriptors, the update path, fuzzers under ASan and
UBSan, the browser emulator, the libretro core and the plugins. A sound change fails the golden hashes until they are
updated on purpose. Every release is also tested on the FM-1 itself.
