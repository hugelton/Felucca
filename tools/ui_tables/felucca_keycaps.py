# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Felucca's keycaps for tools/gen_aa_keycaps.py (its --table): the labels of the pills and the knob sizes. Another UI
on the same pipeline passes a table of its own with these names:
  LABELS   (C name, label) of each pill: KC_<C name>, in this order
  KNOBS    (C name, outer radius) of each knob arc: KNOB_<C name>_*
"""

# (C name, label): the controls the hints name (panel.c B_NAME / E_NAME, short), then the state badges
LABELS = [("PRESETS", "PRESETS"), ("SELECT", "SELECT"), ("ALGO", "ALGO"), ("OCTDN", "OCT-"), ("OCTUP", "OCT+"),
          ("PLAY", "PLAY"), ("REC", "REC"), ("SAVE", "SAVE"), ("EDIT", "EDIT"), ("SEQ", "SEQ"), ("GLO", "GLO"),
          ("HOME", "HOME"), ("ARP", "ARP"), ("KEYS", "KEYS"), ("K1", "K1"), ("K2", "K2"), ("K3", "K3"), ("K4", "K4"),
          ("MUTE", "MUTE"), ("ARM", "ARM"), ("HOLD", "HOLD"), ("ON", "ON"), ("OFF", "OFF"),
          ("FX", "FX"), ("K14", "K1-4"), ("SCL", "SCL"), ("OCTPM", "OCT-/+"),
          ("LFO", "LFO"), ("ENV", "ENV")]   # (1.5: HELP's hints, src/ui_help.c)

KNOBS = [("BIG", 17), ("SMALL", 10)]   # outer radius; the ring is 2 px
