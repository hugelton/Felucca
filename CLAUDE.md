# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Felucca is multi-engine synthesizer firmware for the M-VAVE FM-1 (JieLi AC79 / pi32v2 CPU), in C with no OS and no
external libc. The project is in maintenance mode (see README). BUILDING.md is the reference for the toolchain, the
build flags and the tests.

## Build

```
git submodule update --init          # firmware/engines (Felucca-Engines, MIT) must not be empty
./build.sh                           # -> build/felucca.bin, build/loader/ota.bin, build/felucca.fwsc
./build.sh --release 1.6.1           # release build: identity FM-1_9XY, build/release-X.Y/
FELUCCA_CPU2=0 ./build.sh            # build options FELUCCA_<NAME>=0|1; table in BUILDING.md
```

Prerequisites: Python 3 with Pillow and fontTools, the JieLi toolchain (`tools/get_toolchain.sh`, `~/.jieli/toolchain`),
the AC79 SDK checkout (`~/fw-AC79_AIoT_SDK`; `AC79_SDK` / `JIELI_TOOLCHAIN` override), and Docker on macOS (the
toolchain is Linux x86-64 only; `tools/build.py` runs each tool in a `linux/amd64` container).

`tools/build.py` first generates headers into `build/gen/` (fonts, icons, keycaps, palettes, `felucca_tables.h`,
FM6 patches, `felucca_samples.h` from `assets/samples-cc0/`), then compiles, links and runs post-link checks that fail
the build:
- `.ram_text` (code that runs with the flash off) must contain no `call`; `.c1_text` (second core) may only call
  through a register; nothing may reference chip ROM addresses.
- RAM headroom ≥ 4 KiB of `.bss`, pool headroom ≥ 8 KiB, image fits the app slot (`firmware/app.ld`).
- **Float only in the 303 engine.** The app is built for the FPU (`build.py` `FPU`, which changes no integer code)
  because `eng_303.c` wraps X0X's float Open303 (`firmware/src/x0x/`, kept byte for byte as X0X has it:
  `tests/t303_test.c` checks their hashes). `float` / `double` anywhere else fails the build (`float_check`), and so does
  an FPU instruction outside the 303's functions. TB-3PO (`tb3po.c`) is an integer port with X0X's exact lines.
- **All hardware register access lives in `firmware/hal/`.** In `src/`, `engines/src/` and `loader/`: no volatile
  pointer casts (except `*(volatile T *)&obj`), no literal register/window addresses, no inline asm (except
  `RING_PUBLISH()`), no HAL register macros. Add a `hal/` helper instead.

## Tests

```
tests/run_tests.sh                   # everything; run after ./build.sh (needs build/ and build/gen/)
SANITIZE=0 tests/run_tests.sh        # skip the ASan/UBSan reruns and fuzzers
TSAN=1 tests/run_tests.sh            # adds the dual-core test under ThreadSanitizer (~6 min)
DAISYSP=/path/to/DaisySP tests/run_tests.sh   # also compares PHYS with DaisySP's float originals
```

Each test is a standalone C program; to run one, copy its line from `tests/run_tests.sh`, e.g.:

```
cc -O1 -Wall -Wno-unused-function -w -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test
```

Some tests take arguments (output dirs for demo WAVs, `build/felucca.fwsc`, golden files) and some are built several
times with different `-D` flags (`-DFELUCCA_FM4=1`, `-DFELUCCA_CPU2=0`, `-DC1_HOST -pthread`, the USB descriptor
variants); the script is the source of truth for both. Its header comment describes what every suite covers.

Baselines:
- `tests/regress.c` hashes a render of every engine × preset against `tests/golden.txt` (bit-exact), and checks CPU
  cost against `tests/cpu_baseline.txt` (instruction counts via macOS `proc_pid_rusage`, ±25 %) and
  `tests/target_budget.txt` (static loop instruction counts from `build/felucca.dis`, +10 %).
- `GOLDEN_UPDATE=1` / `BUDGET_UPDATE=1 sh tests/run_tests.sh` rewrite them. Only do this for a reviewed, intended
  change of sound or cost, and keep every unaffected entry. Many features are tested as "OFF / 0 is bit for bit as
  before": a new parameter at its default must not change any golden render.
- `tests/union_golden.txt` (engine switches) and `firmware/engines/tests/golden.txt` (the submodule's engines, also
  rendered with Felucca's generated tables) are golden files too.
- UI renders write PNGs and lint reports to `build/ui_new/` (clipped/overlapping text, alignment off by ≥ 1 px fails).

Web tests (`node web/test_web.mjs`, `node web/test_backup.mjs`) and the browser emulator (`web/emu/build.sh`, needs
`emcc`) run from `run_tests.sh` when Node / Emscripten are installed.

## Architecture

**Single compilation unit.** `firmware/src/felucca.c` holds the build-option defaults and `#include`s every other
`.c` file in dependency order (each file uses what the files above it define), ending with `main.c`. Several
files include sub-files themselves (e.g. `engines.c` → `eng_*.c`, `seq.c` → `chord.c` / `motion.c` / `midi_*.c`,
`editor.c` → `editor_*.c`, `ui.c` / `ui_draw.c` / `ui_graph.c` → `ui_*.c`, `fx.c` → `perform.c` / `click.c`,
`project.c` → `settings_persist.c` / `slice_store.c`). A new source file is added with an `#include` at the right
place, not to a build list. The HAL (`firmware/hal/fm1_*.h`) is header-only plus a few `.S` files. Functions are
mostly `static`; there is no `#include` of system headers beyond `<stdint.h>` (`libc.c` provides `memset` etc.).

**Host tests include the same sources.** Tests `#include "../firmware/src/*.c"` directly against stubs for the
hardware (`tests/hostsim.c` is the base for the DSP / sequencer; `tests/ui_test.c` and `tests/ui_render.c` add the
UI files; `web/emu/felucca_web.c` does the same for WebAssembly). So a change in include order or a new global
in `felucca.c` may need the matching change in the test harnesses and the emulator.

**Runtime.** `main.c` boots and runs the main loop (UI, storage, editor, USB/MIDI). Two ISRs: ALNK0 (`audio.c`)
renders each I2S half buffer (`HALF_FRAMES` = 128) via `fx.c` `mix_block` (events → each part's engine/voices →
INSERT / SLICER → sends → buses → master → USB audio tap → metronome click); TIMER5 (10 kHz, outranks ALNK0) scans
keys/LEDs and services USB audio while nested in the audio ISR. With `FELUCCA_CPU2` and MENU > SYSTEM > 2ND CORE ON,
a second core renders some parts (`hal/fm1_cpu1.h`, `C1_*` in `core.h`); output must be bit-identical to one core.

**Model** (`core.h`): 4 tracks, each a synth part with its own engine, preset, parameters and 64-step pattern,
sharing a budget of 8 voices (`voice.c`). Engines implement the interface in
`firmware/engines/src/felucca_engine.h`. ANALOG, LOFI, TRIO, WHEEL, NOISE and the shared `dsp.c` live in the
**Felucca-Engines submodule (MIT)**: changes to them belong in that repository. The others (`eng_*.c` here) are
GPL. DIGITAL (`FELUCCA_FM4`) is retired: engine 1 is reserved and its sounds load as FM6 via `fm4_convert.c`.
Engine numbers are append-only (SLICE 13, DCO 14, 303 15); `NENGINES` is 16 in every build: a build without SLICE,
with `FELUCCA_303=0` or with `FELUCCA_FM4=1` reserves its slot instead (`engines.c` `eng_ok` / `eng_stored_ok`), and
the EDIT layer has keys for at most 15 engines plus INIT (`ui_layer.c` `LY_INIT < 16`; CLEAN INIT is the INIT dialog's
KNOB 1). On a 303 track ARP opens TB-3PO (`ui_tb3po.c`: GENERATE / SCALE, OCT+ a new line, OCT- a MUTATE) instead of
the arpeggiator (`seq.c` `arp_mode`); a project keeps its settings in that track's FM6 area (`tb3po.c` `tb3_pack`).

**Stored formats and compatibility.** Projects (FUN1..FUN10, `project.c`), user presets (`upreset.c`), settings,
the FM6 voice bank and the full backup live in NOR flash (`storage.c`, A/B copies, tested against torn writes and
power cuts). Every older format must keep loading exactly; tests check this with old-format fixtures
(`tests/old_pack.h`). New common per-track parameters go just before `P_E0` in the `P_*` enum: stores map
parameters by count (first `np - 8` common, last 8 engine). Flash regions are fixed (the loader never writes
`[0x97000, 0xE0000)`, user data).

**Editor protocol.** `firmware/src/editor.c` answers SysEx over USB-MIDI, documented in `web/EDITOR_PROTOCOL.md`
(versioned, earlier reply bytes never move). The web test's mock device must equal the firmware's tables:
`tests/descdump.c` → `build/host/desc.json`, `tests/editor_test.c` → `build/host/menu.json`. The web editor and
installer themselves are in the separate Felucca-WebApp repo; `web/` here holds the protocol libraries, tests,
`make_site.py` and the emulator.

**Update path.** `firmware/loader/` is a separate RAM-resident update loader (its own unit, `loader.c`), packed with
the app into a `.fwsc` by `tools/fm1pkg_make.py`. `tools/fm1_install.py` installs from the command line.

## Conventions

- Every new file carries the GPL-3.0-only SPDX header and the copyright line used in existing files. Ported files
  keep their own licence (`fm6_core.c` Apache-2.0 from msfa, `phys_dsp.c` / `phys_symp.c` MIT from DaisySP); see
  LICENSING.md.
- Comments are dense and terse and reference issue numbers and the version a behavior arrived in (`1.5.5 (#209)`);
  match that style.
- `FELUCCA_VERSION` is defined in `firmware/src/felucca.c`; tests and the emulator read it from there.
- PRs should be small, run `tests/run_tests.sh`, and be tested on a real FM-1 (CONTRIBUTING.md).
