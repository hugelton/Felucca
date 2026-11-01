# Platforms

Felucca's sources on platforms other than the FM-1. The FM-1's own build is in `firmware/` (see
[BUILDING.md](../BUILDING.md)). How the pieces fit is in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md), and how
to bring Felucca to a new platform is in [docs/PORTING.md](../docs/PORTING.md).

| Folder | What | Status |
| --- | --- | --- |
| [`host/`](host/) | The host: the firmware against a simulated FM-1, and its API (`felucca_host.h`). Every platform below builds it. | — |
| [`common/`](common/) | What the frontends share: the panel drawn beside the screen, the keyboard and gamepad map, the flash file, MIDI in, the plugin loader. | — |
| [`web/`](web/) | The browser: WebAssembly in an AudioWorklet, and its page. | Supported ([try it](https://hugelton.github.io/Felucca/webapp/try/)) |
| [`sdl/`](sdl/README.md) | A desktop with SDL2 (macOS, Linux). | Beta, source only |
| [`libretro/`](libretro/README.md) | A libretro core (RetroArch and other frontends). | Beta, source only |

Every platform needs the generated headers in `build/gen` (the tables, fonts and samples). Make them without the
FM-1's toolchain:

```
./build.sh gen
```

Then build a platform with its script, or with `./build.sh web`, `./build.sh sdl` or `./build.sh libretro`.

## What each platform has

| | FM-1 | Browser | SDL2 | libretro |
| --- | --- | --- | --- | --- |
| Sound, screen, panel | yes | yes | yes | yes |
| MIDI in | USB, TRS | Web MIDI | ALSA, CoreMIDI | the frontend's |
| MIDI out, the web editor, updates | yes | — | — | — |
| Plugin engines | flash slots (2) | — | shared libraries | shared libraries |
| Where the flash is kept | its own flash | the browser | a file | a file |
