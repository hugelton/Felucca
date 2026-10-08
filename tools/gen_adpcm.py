#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ugotworms
"""The ADPCM engine's waves and presets (firmware/src/eng_adpcm.c), in its 4-bit BRR format.

  tools/gen_adpcm.py OUT.h

No samples: each wave is a cycle or two made here, a few dozen bytes once encoded.

BRR: blocks of 9 bytes, a header (shift << 4 | filter << 2 | loop << 1 | end) and 16 four-bit samples
decoded through one of four predictive filters. The encoder tries every filter and shift of each block
against the decoder below, which is adpcm_dsp.c's bit for bit (after decode_brr of snes_spc's SPC_DSP.cpp,
by Shay Green, LGPL-2.1-or-later; with its 16-bit wrap), so the history it tracks is the one the engine will
rebuild. The first block uses filter 0 (no history): a key-on starts from the last note's history, and the
loop (the whole wave) must decode the same on every pass. The root: the note that plays at pitch 0x1000.
"""
import math
import sys
from pathlib import Path

FS = 32000                                   # the engine's rate: pitch 0x1000 plays a wave as stored
PEAK = 0.85                                  # of full scale, in the decoder's (x2) domain


# ------------------------------------------------------------------ BRR ---
def wrap16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def dec1(n, shift, filt, p1, p2s):
    """one sample as the engine decodes it: nibble n (-8..7), the stored history p1, p2s (x2 domain)"""
    s = (n << shift) >> 1
    if shift >= 13:
        s = -2048 if s < 0 else 0
    p2 = p2s >> 1
    if filt >= 2:
        s += p1
        s -= p2
        if filt == 2:
            s += p2 >> 4
            s += (p1 * -3) >> 6
        else:
            s += (p1 * -13) >> 7
            s += (p2 * 3) >> 4
    elif filt == 1:
        s += p1 >> 1
        s += (-p1) >> 5
    s = max(-32768, min(32767, s))
    return wrap16(s * 2)


def enc_block(target, p1, p2, filters):
    """best (error, header bits, nibbles, p1, p2) of 16 target samples (x2 domain) from history p1, p2"""
    best = None
    for filt in filters:
        for shift in range(13):
            err, nibs, a, b = 0, [], p1, p2
            for t in target:
                base = dec1(0, shift, filt, a, b)
                n0 = int(round((t - base) / float(1 << shift)))
                bn, bv, be = 0, 0, None
                for n in (n0 - 1, n0, n0 + 1):
                    if -8 <= n <= 7:
                        v = dec1(n, shift, filt, a, b)
                        e = (v - t) * (v - t)
                        if be is None or e < be:
                            bn, bv, be = n, v, e
                if be is None:                  # all candidates outside -8..7: the nearest end
                    bn = -8 if n0 < -8 else 7
                    bv = dec1(bn, shift, filt, a, b)
                    be = (bv - t) * (bv - t)
                err += be
                if best and err >= best[0]:
                    break
                nibs.append(bn)
                a, b = bv, a
            else:
                if not best or err < best[0]:
                    best = (err, shift << 4 | filt << 2, nibs, a, b)
    return best


def brr_encode(x):
    """x: floats -1..1, a multiple of 16 long: one wave, looped from its start"""
    assert len(x) % 16 == 0
    tgt = [max(-32000, min(32000, int(round(v * 32767)))) for v in x]
    out, p1, p2, nb = bytearray(), 0, 0, len(x) // 16
    for k in range(nb):
        err, hdr, nibs, p1, p2 = enc_block(tgt[16 * k:16 * k + 16], p1, p2, range(4) if k else (0,))
        if k == nb - 1:
            hdr |= 3                            # end, loop
        out.append(hdr)
        for i in range(0, 16, 2):
            out.append((nibs[i] & 15) << 4 | (nibs[i + 1] & 15))
    return bytes(out)


# ---------------------------------------------------------------- waves ---
def norm(x):
    m = max(1e-9, max(abs(v) for v in x))
    return [v * PEAK / m for v in x]


def additive(n, partials):
    return [sum(a * math.sin(2 * math.pi * k * i / n) for k, a in partials) for i in range(n)]


def root_of_cycle(cycle):
    """the note of a cycle of `cycle` samples at 32 kHz, in 1/16 semitones"""
    return int(round((69 + 12 * math.log2(FS / cycle / 440.0)) * 16))


# name: (samples, the length of one cycle)
WAVES = {
    "SQUARE": ([0.75 if i < 16 else -0.75 for i in range(32)] * 2, 32),    # 0.75: exact in a filter-0 block
    "PULSE": ([0.75 if i < 8 else -0.75 for i in range(32)] * 2, 32),
    "SAW": (norm(additive(64, [(h, (-1) ** (h + 1) / h) for h in range(1, 32)]) * 2), 64),
    "SINE": (norm(additive(64, [(1, 1.0), (2, 0.18), (3, 0.06)])), 64),
    "SUBSAW": (norm(additive(128, [(2 * h, 1.0 / h / (1 + (h / 5.0) ** 2)) for h in range(1, 32)] + [(1, 0.6)])),
               128),                                                         # a low-passed saw and its sub-octave
    "ORGAN": (norm(additive(128, [(1, 0.8), (2, 1.0), (3, 0.6), (4, 0.5), (8, 0.3)])), 128),   # 16' 8' 5 1/3' 4' 2'
}


# presets: (name, wave, TUNE, SR, NOISE, (ECHO, DELAY, FDBK, FIR), (ATK, DEC, SUS, REL), mono, fx sends
# (DIST CHOR DLY REV), pattern)
PRESETS = [
    ("SQUARE LEAD", "SQUARE", 0, 0, 0, (40, 9, 22, 1), (5, 40, 110, 30), 1, (0, 0, 0, 20), 4),
    ("PULSE LEAD", "PULSE", 0, 0, 0, (40, 9, 22, 1), (5, 40, 110, 30), 1, (0, 0, 0, 20), 4),
    ("SAW BASS", "SAW", -12, 0, 0, (0, 4, 0, 1), (0, 60, 80, 0), 1, (0, 0, 0, 0), 2),
    ("SUB BASS", "SUBSAW", -12, 0, 0, (0, 4, 0, 1), (0, 30, 110, 20), 1, (0, 0, 0, 10), 2),
    ("SOFT SINE", "SINE", 0, 0, 0, (45, 6, 30, 1), (40, 30, 110, 40), 0, (0, 0, 0, 30), 3),
    ("ORGAN", "ORGAN", 0, 0, 0, (25, 6, 15, 1), (0, 0, 127, 15), 0, (0, 40, 0, 30), 6),
    ("ECHO PAD", "PULSE", 0, 0, 0, (45, 10, 35, 1), (60, 30, 120, 70), 0, (0, 30, 0, 40), 5),
    ("NOISE HAT", "SQUARE", 0, 24, 29, (0, 4, 0, 1), (0, 0, 0, 0), 0, (0, 0, 0, 20), 12),
]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    names = list(WAVES)
    data, rows = bytearray(), []
    for name, (x, cycle) in WAVES.items():
        rows.append((name, len(data), root_of_cycle(cycle)))
        data.extend(brr_encode(x))
    out = ["/* generated by tools/gen_adpcm.py: the ADPCM engine's waves and presets (eng_adpcm.c) */",
           "#ifndef FELUCCA_ADPCM_H", "#define FELUCCA_ADPCM_H",
           "typedef struct { uint16_t start; int16_t root16; } adp_inst_t;   /* a byte offset in ADP_BRR (it loops "
           "from there); root: the note of pitch 0x1000, 1/16 st */",
           f"#define ADP_NINST {len(rows)}u",
           f"#define ADP_BRR_LEN {len(data)}u",
           "static const char *const ADP_INST_NAMES[ADP_NINST] = {" + ", ".join(f'"{n}"' for n in names) + "};",
           "static const adp_inst_t ADP_INST[ADP_NINST] = {"]
    out += [f"    {{{start}u, {root}}},   /* {name} */" for name, start, root in rows]
    out += ["};", "#ifdef ADP_PRESETS",
            "/* {INST, TUNE, SR, NOISE, ECHO, DELAY, FDBK, FIR}, {ATK, DEC, SUS, REL} */",
            "static const preset_t ADP_PRESET_TABLE[] = {"]
    for name, wave, tune, sr, noise, echo, env, mono, fx, pat in PRESETS:
        e = ", ".join(str(v) for v in (names.index(wave), tune, sr, noise, *echo))
        out.append(f'    {{"{name}", {{{e}}}, {{{", ".join(map(str, env))}}}, 0, {mono}, '
                   f'FX({", ".join(map(str, fx))}), PAT({pat})}},')
    out += ["};", "#endif", "static const uint8_t ADP_BRR[ADP_BRR_LEN] = {"]
    for i in range(0, len(data), 18):
        out.append("    " + " ".join(f"0x{v:02X}," for v in data[i:i + 18]))
    out += ["};", "#endif", ""]
    Path(sys.argv[1]).write_text("\n".join(out), newline="\n")   # (the same bytes on every OS)
    print(f"adpcm: {len(rows)} waves, {len(PRESETS)} presets, {len(data)} B -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
