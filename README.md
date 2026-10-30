# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## Overview

Felucca is an open-source synthesizer firmware project that began as custom firmware for the M-VAVE FM-1.

Originally developed for a single instrument, Felucca is evolving toward a multi-platform architecture, separating its synthesis engines from hardware-specific implementations. The aim is to make its instruments and sound engines adaptable to different hardware platforms without committing to specific ports or release schedules.

### M-VAVE FM-1

The FM-1 firmware is currently at version 1.7.0. Connect the FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge and press Install; no extra hardware is needed.
Installing is at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.
[Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

- Install: [web installer](https://hugelton.github.io/Felucca/)
- Edit: [web editor](https://hugelton.github.io/Felucca/webapp/editor/) ([Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp))
- Read: the manual and the release notes are in the [wiki](https://github.com/hugelton/Felucca/wiki/Home)
- Build: [BUILDING.md](BUILDING.md)
- Platforms: the FM-1 (the reference; every release is tested on it) · the browser (`web/emu`) · a desktop with SDL2
  (beta, run on macOS so far; Linux next: [platforms/linux](platforms/linux/README.md)). The same sources on each: what a platform does not
  have (the FM-1's panel, USB, its flash) is given by its own files under `platforms/`.
- Related: [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (the ANALOG, LOFI, TRIO, WHEEL, NOISE and PHASE engines, MIT; still maintained) ·
  [felucca-bsp](https://github.com/hugelton/felucca-bsp) (the FM-1 hardware layer, MPL-2.0; Felucca builds on it from 1.7.0)

## Platform Support

| Platform | Status | Notes |
| --- | --- | --- |
| M-VAVE FM-1 | Supported | The original platform; firmware and web installer available. |
| Other hardware | Not yet supported | Ports are welcome! |

Felucca began on the FM-1, but it is not intended to stay there. Contributions and ports to other platforms are very welcome. If you are interested in bringing Felucca to another device, feel free to open an issue or pull request to discuss it. No specific future ports or release dates are promised.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance). The
design, features, sound design and direction are the maintainer's or the community's, and no generative AI is used for music,
icons or artwork. See [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the hardware layer in `firmware/bsp` is MPL-2.0
([felucca-bsp](https://github.com/hugelton/felucca-bsp)); the bundled fonts and ported DSP keep their own licences
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
