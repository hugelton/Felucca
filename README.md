# Felucca

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

![Felucca 1.0](docs/felucca-1.0.png)

Multi-engine synthesizer firmware for the M-VAVE FM-1, **version 1.6.1**. Connect the FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge and press Install; no extra hardware is needed.
Installing is at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.
Want to look around first? [Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

- Install: [web installer](https://hugelton.github.io/Felucca/), or `tools/fm1_install.py` from a terminal
- Edit: [web editor](https://hugelton.github.io/Felucca/webapp/editor/) (developed in [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp))
- Read: the manual is in the [wiki](https://github.com/hugelton/Felucca/wiki/Home): [Installation](https://github.com/hugelton/Felucca/wiki/Installation) ·
  [Controls](https://github.com/hugelton/Felucca/wiki/Controls) · [Menu](https://github.com/hugelton/Felucca/wiki/Menu) ·
  [Engines](https://github.com/hugelton/Felucca/wiki/Engines) · [Release notes](https://github.com/hugelton/Felucca/wiki/Release-Notes) ·
  [Troubleshooting](https://github.com/hugelton/Felucca/wiki/Troubleshooting)
- Build: [BUILDING.md](BUILDING.md); clone with the engines' submodule: `git clone --recurse-submodules https://github.com/hugelton/Felucca.git`

<a href="https://hugelton.itch.io/felucca"><img src="https://static.itch.io/images/badge-color.svg" alt="Available on itch.io" width="74"></a>

## What it is

Thirteen engines with factory presets, four tracks (8 voices shared), a 64-step sequencer (chords, ties, chance, conditions,
ratchets, nudge, locks, automation, live and step recording), SEQ TOOLS, songs, effects, a MOD matrix, MIDI (USB and TRS,
per-track channels, MIDI LEARN) and USB audio. More: [Features](https://github.com/hugelton/Felucca/wiki/Features).

## Unreleased

- **External MIDI clock phase:** keep sequencers, ARPs and the metronome in musical time without accumulating integer rounding error. Straight, triplet and long divisions retain fractional timing, and ARPs retain step overshoot. Internal-clock rendering and saved formats stay unchanged.

## New in 1.6.1

- **MENU > LARGE keeps one size per knob** (#251): a value no longer jumps between sizes as you turn the knob
- **Updates check the package's size on the FM-1**: a package made for another device is refused before anything is erased
- **Second core (beta), off by default:** MENU > SYSTEM > 2ND CORE > ON (BETA), then restart: the FM-1's second core
  renders some of the parts, same sound, more headroom (#247)
- **Felucca-Engines:** ANALOG, LOFI, TRIO, WHEEL and NOISE now live in [their own repository](https://github.com/hugelton/Felucca-Engines)
  under the MIT licence (a git submodule here); Felucca itself stays GPL-3.0-only

## New in 1.6

- **Your own UI colours:** COLOR has an eleventh value, CUSTOM, set in the web editor with a live preview on the FM-1
- **The SEQ piano roll follows the play position** when the pattern is longer than one screen
- **Conditional steps** (1:2 … 4:4, FILL), **EUCLID, DOUBLE and HALF**, **a MIDI channel per track**, PHASE FOLD, three more drum kits and humanize in the MOD matrix (1.5.5 and 1.5.6 were never released: 1.6 carries them)
- ⚠ A project with conditional steps does not open on 1.5 or 1.5.1; projects saved by 1.4 or later do not open on 1.1.5.x

All changes and what to know before going back: [Release notes](https://github.com/hugelton/Felucca/wiki/Release-Notes) · [Upgrading from 1.1.5](https://github.com/hugelton/Felucca/wiki/Upgrading-from-1.1.5).

## Support and contributing

Please report what you find in [Issues](https://github.com/hugelton/Felucca/issues) (reproducible bugs, one per issue). Ideas and
requests go to [Discussions](https://github.com/hugelton/Felucca/discussions). Pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).
Felucca is GPLv3 and you are free to fork it; before starting yet another fork, please consider whether a PR would do.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance). The
design, features, sound design and direction are the maintainer's or the community's, and no generative AI is used for music,
icons or artwork. See [On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included; the bundled fonts and ported DSP keep their own licences,
and five engines ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines), `firmware/engines/`) are MIT
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
