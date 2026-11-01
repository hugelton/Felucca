# Building Felucca

Felucca builds for the FM-1 and for the host platforms (the browser, a desktop with SDL2, libretro). The host platforms
need only a C compiler and Python; the FM-1 also needs JieLi's toolchain and SDK. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
explains how the pieces fit.

| Command | What | Needs |
| --- | --- | --- |
| `./build.sh` | the FM-1 firmware: `build/felucca.fwsc` (and `build/felucca.bin`, `build/loader/ota.bin`) | everything below |
| `./build.sh --release X.Y.Z` | a release build of it (below) | the same |
| `./build.sh gen` | the generated headers only: `build/gen` (tables, fonts, icons, palettes, samples) | Python 3, Pillow, fontTools |
| `./build.sh web` | the browser build: `build/web` | `gen`, Emscripten |
| `./build.sh sdl` | the desktop build: `build/sdl/felucca` | `gen`, a C compiler, SDL2 |
| `./build.sh libretro` | the libretro core: `build/libretro` | `gen`, a C compiler |
| `./build.sh host` | `gen`, then every host platform whose tools are there | |
| `./build.sh test` | the tests (`tests/run_tests.sh`) | see [Tests](#tests) |

The FM-1 build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Get the source

Two parts are git submodules: six engines (`firmware/engines/`, [Felucca-Engines](https://github.com/hugelton/Felucca-Engines),
MIT) and the hardware layer (`firmware/bsp/`, [felucca-bsp](https://github.com/hugelton/felucca-bsp), MPL-2.0), so clone
with `--recurse-submodules`:

```
git clone --recurse-submodules https://github.com/hugelton/Felucca.git
```

In a clone made without it, run `git submodule update --init` once. GitHub's "Download ZIP" and the source archives
it adds to a release leave `firmware/engines/` and `firmware/bsp/` empty; each release also has
`felucca-X.Y.Z-source.tar.gz`, the whole source with the submodules' files in it. The build stops with
"firmware/engines is empty" or "firmware/bsp is empty" when they are missing.

## Prerequisites (macOS)

For every build:

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)

For the FM-1:

- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

On Linux x86-64 the toolchain runs natively and Docker is not needed.

For the host platforms and the tests (each optional):

- a C compiler (clang or gcc);
- SDL2 for the desktop build (`brew install sdl2`; Debian / Ubuntu: `libsdl2-dev`, and `libasound2-dev` for MIDI);
- Emscripten (`emcc`) for the browser build;
- Node.js for the web tests.

## Build the FM-1 firmware

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.9.0` makes a release build: the package identity becomes `FM-1_919`
and the version string `v1.9.0`; the package is `build/felucca-1.9.0.fwsc`, and
`build/release-1.9.0/` holds what a release ships: the package, the app
(`felucca-1.9.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it). The identity changes with
the first two numbers only: 1.9.0 and 1.9.1 are both `FM-1_919`.

The build checks the image before it writes the package: the entry, the code in RAM, the sizes against the memory map
(`firmware/app.ld`), and that no file outside `firmware/hal` (and felucca-bsp) touches a hardware register.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/bsp/chip/usb.c`) |
| `FELUCCA_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/bsp/chip/usb.c`) |
| `FELUCCA_CPU2` | 1 | the second core is built in (MENU > SYSTEM > 2ND CORE, OFF by default); `0`: none of it, 1.6's one-core code and RAM (`firmware/bsp/chip/fm1_cpu1.h`, `firmware/src/fx.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 1 | the SLICE engine |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |
| `FELUCCA_PLUGINS` | 1 | plugin engines from the flash's two slots ([docs/PLUGINS.md](docs/PLUGINS.md); `firmware/src/plugin.c`, `plugin_fm1.c`); `0`: none, 1.8's engines and RAM |
| `FELUCCA_ADPCM` | 0 | the ADPCM engine (engine 14, `firmware/src/eng_adpcm.c`): kept in the tree, not built by default. On the FM-1 one of its voices costs about three ANALOG voices (8 voices on one part: 54 % CPU, against 17 % for ANALOG; on all four parts the audio drops out); the figures are in `eng_adpcm.c` |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/bsp/chip/usb.c`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Host platforms

Each host platform builds the firmware's sources against a simulated FM-1 (`platforms/host`), so they need only
`build/gen`, not the FM-1's toolchain:

```
./build.sh gen
./build.sh web                                  # build/web: open with a local server
python3 -m http.server -d build/web 8790        # http://localhost:8790/
./build.sh sdl && build/sdl/felucca             # the desktop build
./build.sh libretro                             # build/libretro/felucca_libretro.{so,dylib,dll}
```

- The browser build is WebAssembly in an AudioWorklet. It keeps the flash's storage sectors (projects, user presets,
  settings, user samples) in the browser. A build of it is on the site: <https://hugelton.github.io/Felucca/webapp/try/>.
- [platforms/sdl](platforms/sdl/README.md) and [platforms/libretro](platforms/libretro/README.md) describe the
  desktop build and the core: the keys, a gamepad, MIDI in, the flash file, plugins.
- Not on any host platform: USB (MIDI out, USB audio, the web editor, updates), TRS MIDI and the serial console.

[platforms/README.md](platforms/README.md) lists the platforms, and [docs/PORTING.md](docs/PORTING.md) explains how to add one.

## Plugin engines

Plugins are built in [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (`firmware/engines` here), against
its MIT headers only. Its `docs/PLUGINS.md` is the guide:

```
sh firmware/engines/tools/build_plugin.sh firmware/engines/examples/fold/fold.c       # a desktop library
sh firmware/engines/tools/build_plugin_fm1.sh firmware/engines/examples/fold/fold.c 0 # an FM-1 blob for slot 0
python3 tools/fm1_plugin.py install firmware/engines/build/fm1/fold.s0.fep              # onto the FM-1
build/sdl/felucca --plugins firmware/engines/build/plugins                              # on the desktop
```

## Tests

```
tests/run_tests.sh
```

Runs everything that runs on a computer. Run it after `./build.sh` (it uses `build/` and needs `AC79_SDK` set as for the
build); with only `./build.sh gen` it runs the tests that need no FM-1 package and skips the rest. The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped. Likewise with `SNES_SPC` pointing at a
snes_spc 0.9.0 checkout, the ADPCM engine's voice is compared sample for sample with its `SPC_DSP.cpp`
(`tests/adpcm_ref.cpp`, needs `c++`); without it the rest of the ADPCM test runs. Felucca-Engines' own test
(`firmware/engines/tests/run.sh`) runs too, and its standalone host is built once more with Felucca's tables
against the same golden renders.

The platforms are tested too: the browser build (with Emscripten and Node.js: it boots, plays, saves, and renders the
same samples as the native build), the libretro core (`platforms/libretro/lr_check.c`, without a frontend), and the
desktop build's checks: the panel's layouts, the six MIT engines as plugins against the engines built in (hash for
hash), a plugin of its own (Felucca-Engines' example) getting its number, and a project keeping its plugin's id.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
