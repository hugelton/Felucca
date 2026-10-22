# Felucca

This is a fork of [Felucca Firmware for the M-VAVE FM-1](https://github.com/hugelton/Felucca).
[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

## Overview

Multi-engine synthesizer firmware for the M-VAVE FM-1, version 1.6.1. Connect the FM-1 to a computer by USB, open the
[web installer](https://hugelton.github.io/Felucca/) in Chrome or Edge and press Install; no extra hardware is needed.
Installing is at your own risk: M-VAVE's updater or the installer's **Return to official V15** takes you back.
[Try it in your browser](https://hugelton.github.io/Felucca/webapp/try/), no FM-1 needed.

- Install: [web installer](https://hugelton.github.io/Felucca/)
- Edit: [web editor](https://hugelton.github.io/Felucca/webapp/editor/) ([Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp))
- Read: the manual and the release notes are in the [wiki](https://github.com/hugelton/Felucca/wiki/Home)
- Build: [BUILDING.md](BUILDING.md)
- Related: [Felucca-Engines](https://github.com/hugelton/Felucca-Engines) (the ANALOG, LOFI, TRIO, WHEEL and NOISE engines, MIT; still maintained) ·
  [felucca-bsp](https://github.com/hugelton/felucca-bsp) (the FM-1 hardware layer)

## New since 1.6.1

Not released yet (the version string is still 1.6.1); build it from this branch ([BUILDING.md](BUILDING.md)).

- **DCO engine (14): a Juno-106 voice.** Up to 4 voices a track. EDIT 1 has WAVE, PW, SUB, NOIS, CUT, RES, KEY and HPF; RANGE
  (16' 8' 4') has a page of its own; the track's LFO / ENV -> SHP move the pulse width (PWM). 20 presets from the Juno-106
  factory patches. After Ultramaster KR-106 (Kayrock Screenprinting) through
  [fm-106](https://github.com/PatttF/fm-106) (PatttF), both GPL-3.0.
- **BBD chorus.** FX > CHORUS, KNOB 3 (TYPE): STD as before, or the Juno's stereo BBD chorus I, II or I+II.
- **303 engine (15): a TB-303 bass voice** (Open303 by Robin Schmidt, with the Devilfish ranges and the SOFT / RAT drive of
  schwung-303), as [X0X](https://github.com/charlesvestal/fm1-x0x) ported it to the FM-1. EDIT 1: CUT, RES, ENV, DCY; EDIT 2:
  ACC, WAVE (SAW / SQR), DRV, DTYP (SOFT / RAT). One voice; a step's SLIDE, or overlapping keys in LEGATO, glide with the
  track's GLIDE as the slide time; an ACC step (velocity 127) accents. It is the only float code in the firmware (the app is
  built for the FPU; the build fails on float anywhere else).
- **TB-3PO, a MIDI/pattern generator for the 303.** On a 303 track ARP opens TB-3PO instead of the arpeggiator. GENERATE:
  DENS, ACCENT, SLIDE, OCTS; SCALE: ROOT, SCALE, OCTAVE, MUTATE (every 1..16 passes of the line while playing, or OFF).
  OCT+ writes a new line into the track's 64 steps (notes, accents, slides, rests), OCT- mutates about 25 % of the steps.
  The steps stay normal, editable steps. An integer port of X0X's generator that makes the same line for the same seed
  and settings (from the Phazerville Hemisphere Suite's TB_3PO applet).

Compatibility: the engine order is append-only, so existing projects and sounds load unchanged and every earlier golden
render is bit for bit as before. Firmware up to 1.6.1 resets the new DCO favourites setting (PER5) and shows projects that use
the DCO engine as EMPTY.

## AI disclaimer

Felucca is developed with the assistance of AI coding agents (code, tests, documentation, translation, maintenance).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE); the Hügelton Sample Pack is CC0 1.0; the engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)); the bundled fonts and ported DSP keep their own licences
(Open303 for the 303 engine is MIT; [LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. Felucca is not affiliated with or endorsed by them.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
