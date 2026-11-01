# Porting Felucca

There are two ways to bring Felucca to a new platform. Pick by what the platform is:

| The platform | The way | Example |
| --- | --- | --- |
| A computer, a phone, a handheld, an app, a plugin host: anything with an OS, a screen and sound | **A host frontend.** Build the host (a simulated FM-1) and give it a window, sound and input. | `platforms/sdl`, `platforms/libretro`, `platforms/web` |
| A microcontroller board with keys, encoders, a display and an audio codec | **A HAL port.** Give the firmware the hardware functions that the FM-1 gives it. | the FM-1 (`firmware/bsp`, `firmware/hal`) |

Read [ARCHITECTURE.md](ARCHITECTURE.md) first. Open an issue or a discussion before a large port, so that the work can
be shared.

## A host frontend

The host, `platforms/host/felucca_host.c`, is the whole firmware running against a simulated FM-1. Its API is
`platforms/host/felucca_host.h`, and a frontend needs nothing else. The frontend's job:

1. **Before boot**, give back the flash: call `fh_nor_erase()`, then copy the saved bytes into `fh_nor()`. A first
   start has nothing to copy. Load plugins if the platform can (`fh_plugin_host`, `fh_plugin_add`;
   `platforms/common/plugins.c` does it for shared libraries). Then call `fh_boot()`.
2. **Sound.** Call `fh_render(n)` for every block of audio the platform asks for. It runs the device until `n` frames
   exist in `fh_out_l()` and `fh_out_r()`, at `fh_sample_rate()` (44.1 kHz). The device's clock moves only inside
   `fh_render`. Without sound, call it by your own clock and drop the samples.
3. **Controls.** Pass the buttons and keys held as bit masks (`fh_buttons`, `fh_keys`), the encoder detents
   (`fh_enc`), MASTER (`fh_master`) and MIDI channel messages (`fh_midi`).
4. **Picture.** `fh_screen()` is the 240 × 240 RGB565 screen; redraw it when `fh_screen_draws()` changes. The LEDs are
   `fh_lit_*`, `fh_dim_*`, `fh_breath_*`, `fh_mid_keys` and, during the power-on sweep, `fh_anim_levels`.
5. **Saving.** When `fh_flash_writes()` changes, save the bytes of `fh_nor()` (all of them or the changed sectors), a
   little later rather than at once. `platforms/common/flashfile.c` does this with a file.

Call everything from one thread.

A platform without the FM-1's controls draws them: `platforms/common/panel.c` draws the panel beside the screen, with
the firmware's own drawing code, and handles a pointer, a cursor and a stick. `platforms/common/input.c` maps a
keyboard and a gamepad onto it, and `platforms/common/midi_parse.c` parses MIDI bytes.

### Build

Every platform builds one C file: its frontend, which `#include`s `felucca_host.c`, then the `platforms/common`
files it uses, then its own code. The include path and flags are the same everywhere:

```
-Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1
-std=gnu99 -ffp-contract=off -DFELUCCA_VERSION="vX.Y.Z"
```

`-ffp-contract=off` keeps the few floating-point operations of the host identical on every compiler, so every
platform renders the same audio bit for bit. `build/gen` comes from `./build.sh gen`, which needs only Python 3 with
Pillow and fontTools.

### Check it

- `platforms/web/native_check.c` renders a test song natively. `platforms/web/emu_test.mjs` compares the browser
  build with it.
- The SDL2 build has `--headless MS`, `--engines-check` and `--plugins-check`, and `platforms/libretro/lr_check.c`
  runs the core without a frontend. Copy what fits your platform.
- Add your platform's check to `tests/run_tests.sh` when it can run on a CI machine.

## A HAL port

The application calls the FM-1's hardware through small static functions declared in the HAL headers (`fm1_*.h`):

| Area | What the firmware needs |
| --- | --- |
| Time | a free-running tick counter (`fm1_ticks`, `FM1_TICKS_PER_US`), delays |
| Audio | a stereo I2S DMA in two halves of `HALF_FRAMES` (128) frames at 44.1 kHz, its interrupt calling `fm1_alnk0_irq` |
| Input | the key and button matrix (`fm1_in`, edge reports), seven encoders (`fm1_enc_take`), the ADC for MASTER and the battery |
| LEDs | a bit per key or button: lit, dim, breathing, mid (`fm1_led*` arrays); the power-on sweep |
| Display | `lcd_fill`, `lcd_blit` of a 240 × 240 RGB565 panel, `lcd_power` |
| Flash | read, 4 KiB erase and program (`st_read`, `st_erase`, `st_prog`) for the store; code running in place |
| System | the watchdog, interrupts on and off, reset, the crash record |
| Optional | USB device (MIDI, audio, serial), UART MIDI in, the update entry, a second core |

The FM-1's implementation is felucca-bsp's `chip/` and `boards/fm1/`, and Felucca's own `firmware/hal/`. The simplest
reference for the whole set is the simulated one in `platforms/host/felucca_host.c`, where every function is a few
lines.

A new board belongs in felucca-bsp (`boards/<name>/`). Felucca then builds `firmware/src/felucca.c` against it.
Boards whose controls differ from the FM-1's (fewer keys, no encoders, another screen size) need changes in the UI
as well; open an issue first.

## Engines on a new platform

Engines need nothing from the platform: they are plain C on the host's tables. A platform with run-time code loading
(dlopen, an ELF loader, executable memory) can load plugin engines, as the desktop builds do. A platform without it
builds the engines in.
