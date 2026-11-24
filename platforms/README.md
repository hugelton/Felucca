# Platforms (research)

Felucca 1.8.x on platforms other than the FM-1. Nothing of `firmware/` changes for them: `host/felucca_host.c` runs the
browser build's simulated FM-1 (`web/emu/felucca_web.c`) behind one API (`host/felucca_host.h`), and every frontend
here builds on it.

| Folder | What |
| --- | --- |
| [`host/`](host/) | The host API on the simulated FM-1 |
| [`common/`](common/) | What the frontends share: the panel beside the screen, the keyboard and gamepad map, the flash file, MIDI in |
| [`sdl/`](sdl/README.md) | A desktop with SDL2 (macOS, Linux) |
| [`libretro/`](libretro/README.md) | A libretro core (RetroArch and the other frontends) |
| [`portmaster/`](portmaster/README.md) | The SDL2 build for aarch64 Linux handhelds, as a PortMaster port |
| [`m5stack/`](m5stack/README.md) | The M5Stack CoreS3 (ESP-IDF 5.5) |

Every platform needs the generated headers in `build/gen` (the tables, fonts and samples). Make them without the
FM-1's toolchain (Python 3 with Pillow and fontTools):

```
platforms/gen.sh
```

Then build a platform with its own script (`platforms/sdl/build.sh`, `platforms/libretro/build.sh`,
`platforms/portmaster/build.sh`) or, for the CoreS3, `idf.py build` in `platforms/m5stack`.

1.8 has no plugin engines, so these builds have none either.
