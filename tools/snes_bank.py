# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ugotworms
"""The SNES engine's instrument bank (firmware/src/eng_snes.c), built by tools/gen_brr.py. An SNES game kept its
whole set of instruments in the sound chip's 64 KiB: short attacks, short loops, low sample rates. This bank is
built the same way: synthesized waves, and Felucca's generated drums (tools/gen_waves.py) for a kit.

Each instrument:
  name    shown by INST and as its preset
  kind    "loop": a sustained note, an attack then a loop of whole cycles
          "decay": a struck note, its attack then a loop of its tail (the envelope's SR decays it)
          "oneshot": a hit played to its end (drums)
  src     ("genwav", name): build/genwav/<name>.wav (tools/gen_waves.py, Felucca's own drums)
          ("synth", what): made by gen_brr.py
  rate    the sample rate it is stored at (the chip plays 32 kHz at pitch 0x1000)
  top     the highest note it should reach (the chip's pitch goes two octaves above a sample's root at most)
  att     seconds of attack before the loop; loop: seconds the loop should last at least
  preset  {INST is the instrument} TUNE, SR, NOISE, ECHO, DELAY, FDBK, FIR; env ATK DEC SUS REL; mono; fx sends
"""


def I(name, kind, src, rate=24000, top=96, att=0.08, loop=0.035, gain=1.0, length=0.6, preset=None):
    return dict(name=name, kind=kind, src=src, rate=rate, top=top, att=att, loop=loop, gain=gain, length=length,
                preset=preset or {})


# preset helpers: env (ATK DEC SUS REL), SR, echo (ECHO DELAY FDBK FIR), mono, sends (DIST CHOR DLY REV), pattern
def P(env, sr=0, echo=(30, 6, 20, 1), mono=0, fx=(0, 0, 0, 30), pat=0, tune=0, noise=0):
    return dict(env=env, sr=sr, echo=echo, mono=mono, fx=fx, pat=pat, tune=tune, noise=noise)


PAD = P((60, 30, 120, 70), echo=(45, 10, 35, 1), fx=(0, 30, 0, 40), pat=5)
BASS = P((0, 30, 110, 20), echo=(0, 4, 0, 1), mono=1, fx=(0, 0, 0, 10), pat=2, tune=-12)
LEAD = P((5, 40, 110, 30), echo=(40, 9, 22, 1), mono=1, fx=(0, 0, 0, 20), pat=4)

INSTRUMENTS = [
    # synthesized, the chip's own kind of sound (32 kHz single cycles and short loops)
    I("SQUARE", "loop", ("synth", "square"), preset=LEAD),
    I("PULSE", "loop", ("synth", "pulse25"), preset=LEAD),
    I("SAW", "loop", ("synth", "saw"), preset=P((0, 60, 80, 0), mono=1, echo=(0, 4, 0, 1), pat=2, tune=-12)),
    I("SINE", "loop", ("synth", "sine"), preset=P((40, 30, 110, 40), echo=(45, 6, 30, 1), pat=3)),
    I("SYN STR", "loop", ("synth", "strings"), preset=PAD),
    I("SYNBASS", "loop", ("synth", "synbass"), top=72, preset=BASS),
    I("CHOIR", "loop", ("synth", "choir"), top=84, preset=P((70, 30, 120, 70), echo=(50, 12, 40, 1), fx=(0, 30, 0, 50), pat=5)),
    I("ORGAN", "loop", ("synth", "organ"), preset=P((0, 0, 127, 15), echo=(25, 6, 15, 1), fx=(0, 40, 0, 30), pat=6)),
    I("ORCHHIT", "oneshot", ("synth", "orchhit"), rate=16000, length=0.32,
      preset=P((0, 0, 127, 50), echo=(45, 8, 30, 1), fx=(0, 0, 0, 40), pat=1)),
]

# drum pieces: hits played to their end (cymbals: a looped tail the envelope decays), at the low rates and short
# lengths the games used
DRUMS = [
    I("KICK", "oneshot", ("genwav", "D KICK"), rate=12000, length=0.25),
    I("E.SNARE", "oneshot", ("genwav", "D SNARE"), rate=16000, length=0.18),
    I("RIM", "oneshot", ("genwav", "D RIM"), rate=16000, length=0.08),
    I("E.CLAP", "oneshot", ("genwav", "D CLAP"), rate=16000, length=0.20),
    I("E.CHAT", "oneshot", ("genwav", "D CHAT"), rate=26000, length=0.06),
    I("E.OHAT", "decay", ("genwav", "D OHAT"), rate=22050, att=0.06, loop=0.05),
    I("E.TOM", "oneshot", ("genwav", "D TOM LO"), rate=12000, length=0.28),
    I("E.CRASH", "decay", ("genwav", "D CRASH"), rate=22050, att=0.10, loop=0.07),
    I("RIDE", "decay", ("genwav", "D RIDE"), rate=22050, att=0.08, loop=0.06),
    I("E.COWBL", "oneshot", ("genwav", "D COWBELL"), rate=16000, length=0.15),
]

# the kits: General MIDI note -> (piece, semitones from its native pitch). Toms: one piece, tuned by key
_TOMS = {41: -5, 43: -3, 45: 0, 47: 3, 48: 5, 50: 8}
KITS = {
    "E.KIT": {35: ("KICK", -2), 36: ("KICK", 0), 37: ("RIM", 0), 38: ("E.SNARE", 0), 39: ("E.CLAP", 0),
              40: ("E.SNARE", 1), 42: ("E.CHAT", 0), 44: ("E.CHAT", -1), 46: ("E.OHAT", 0), 49: ("E.CRASH", 0),
              51: ("RIDE", 0), 52: ("E.CRASH", -2), 53: ("RIDE", 2), 56: ("E.COWBL", 0), 57: ("E.CRASH", 1),
              59: ("RIDE", -1), **{n: ("E.TOM", d) for n, d in _TOMS.items()}},
}
# a kit: its drums play to their end (a key-off does not stop them); SR decays the cymbals' looped tails
KIT_PRESET = P((0, 0, 127, 0), sr=15, echo=(20, 5, 10, 1), fx=(0, 0, 0, 20), pat=12)
