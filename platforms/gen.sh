#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# The generated headers (fonts, icons, palettes, tables, FM6 patches, samples) into build/gen, by the firmware's own
# generator (tools/build.py), without the FM-1's toolchain. Every platform here needs them. Python 3 with Pillow and
# fontTools.
set -e
cd "$(dirname "$0")/.."
python3 -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()"
