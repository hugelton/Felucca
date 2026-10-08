#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ugotworms
"""The SNES engine's instruments (firmware/src/eng_snes.c): the bank of tools/snes_bank.py in the S-DSP's
BRR format, its drum kits and presets.

  tools/gen_brr.py OUT.h

BRR: blocks of 9 bytes, a header (shift << 4 | filter << 2 | loop << 1 | end) and 16 four-bit samples
decoded through one of four predictive filters. The encoder tries every filter and shift of each block
against the decoder below, which is the hardware's bit for bit (after decode_brr of snes_spc's SPC_DSP.cpp,
by Shay Green, LGPL-2.1-or-later; with its 16-bit wrap), so the history it tracks is the one the S-DSP will
rebuild. The first block, and the loop's
first block, use filter 0 (no history): a key-on starts from the last note's history, and a loop must
decode the same on every pass.

A recording becomes an instrument the way SNES composers made them: its onset found, a sample rate chosen
near the bank's so that a whole number of cycles fills a whole number of BRR blocks (the loop then has no
seam in pitch), the loop placed after the attack where the wave best meets itself one loop later, its decay
(struck notes) flattened across the loop and its end crossfaded into its start; then encoded. The root: the
note that plays at pitch 0x1000 (the stored rate scaled to the chip's 32 kHz). Recordings: build/genwav
(tools/gen_waves.py's drums, made by gen_samples.py, which tools/build.py runs first).

Results are cached in build/brr_cache (keyed by the instrument and its recording): rebuilds are quick.
"""
import hashlib
import json
import math
import re
import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
import snes_bank as bank  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENWAV = SRC / "build" / "genwav"
CACHE = SRC / "build" / "brr_cache"
FS = 32000                                   # the S-DSP's rate: pitch 0x1000 plays a sample as stored
PEAK = 0.85                                  # of full scale, in the decoder's (x2) domain
VERSION = 4                                  # of the processing below (a change invalidates the cache)


# ------------------------------------------------------------------ BRR ---
def wrap16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def dec1(n, shift, filt, p1, p2s):
    """one sample as the S-DSP decodes it: nibble n (-8..7), the stored history p1, p2s (x2 domain)"""
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


def brr_encode(x, loop):
    """x: floats -1..1 (a multiple of 16 long), loop: the loop's first sample (a multiple of 16) or None"""
    assert len(x) % 16 == 0 and (loop is None or loop % 16 == 0)
    tgt = [max(-32000, min(32000, int(round(v * 32767)))) for v in x]
    out, p1, p2, nb, sq = bytearray(), 0, 0, len(x) // 16, 0
    for k in range(nb):
        free = k != 0 and (loop is None or k != loop // 16)
        err, hdr, nibs, p1, p2 = enc_block(tgt[16 * k:16 * k + 16], p1, p2, range(4) if free else (0,))
        sq += err
        if k == nb - 1:
            hdr |= 1 | (2 if loop is not None else 0)
        out.append(hdr)
        for i in range(0, 16, 2):
            out.append((nibs[i] & 15) << 4 | (nibs[i + 1] & 15))
    rms = math.sqrt(sq / max(1, len(x))) / 32767
    return bytes(out), rms


# ------------------------------------------------------------- signals ---
def resample(x, sr, to):
    """windowed-sinc (Blackman, 16 zero crossings a side), low-passed at 0.92 of the lower Nyquist"""
    if abs(sr - to) < 1e-9:
        return list(x)
    ratio = sr / to                                     # input samples per output sample
    fc = 0.92 * min(1.0, 1.0 / ratio)                   # cutoff, of the input's Nyquist
    half = int(math.ceil(16 / fc))
    n_out = int(len(x) / ratio)
    out = []
    for j in range(n_out):
        c = j * ratio
        i0 = int(c)
        acc = 0.0
        for i in range(max(0, i0 - half + 1), min(len(x), i0 + half + 1)):
            t = c - i
            u = t * fc
            s = fc if t == 0 else math.sin(math.pi * u) / (math.pi * t)
            w = 0.42 + 0.5 * math.cos(math.pi * t / half) + 0.08 * math.cos(2 * math.pi * t / half)
            acc += x[i] * s * w
        out.append(acc)
    return out


def norm(x, peak=PEAK):
    m = max(1e-9, max(abs(v) for v in x))
    return [v * peak / m for v in x]


def additive(n, partials):
    return [sum(a * math.sin(2 * math.pi * k * i / n) for k, a in partials) for i in range(n)]


def read_wav(path):
    sr, x = sio.read_any_wav(Path(path))
    return sr, x


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def plan_loop(f0, rate, loop_s):
    """(rate, loop length, cycles): a rate near `rate` at which `cycles` periods of f0 are a whole number of
    BRR blocks, the loop at least loop_s long"""
    best = None
    for m in range(1, 4096):
        length = 16 * m
        k = max(1, round(length * f0 / rate))
        r = length * f0 / k
        dev = abs(r / rate - 1)
        if length / rate >= loop_s and dev <= 0.02:
            return r, length, k
        if best is None or dev < best[0]:
            best = (dev, r, length, k)
        if length / rate > 4 * loop_s + 0.1:
            break
    return best[1], best[2], best[3]


def place_loop(y, att, length, search, period):
    """the loop's start (>= att): where y best meets itself one loop later, over two periods around it"""
    w = max(8, min(length // 2, int(2 * period)))
    best, bs = None, att
    end = min(att + search, len(y) - length - w - 1)
    for s in range(att, max(att + 1, end)):
        e = 0.0
        for j in range(-w, w):
            d = y[s + j] - y[s + length + j]
            e += d * d
        if best is None or e < best:
            best, bs = e, s
    return bs


def make_loop(y, s, att16, length, xfade, flatten):
    """the stored sample: att16 samples of attack ending at s, then the loop y[s:s+length] with its decay
    flattened (struck notes) and its end crossfaded into what precedes its start"""
    head = y[max(0, s - att16):s]
    head = [0.0] * (att16 - len(head)) + head
    loop = y[s:s + length]
    if flatten:                                         # the decay across the loop, undone (the envelope's SR
        h = length // 2                                 # decays the note instead)
        a = math.sqrt(sum(v * v for v in loop[:h]) / h + 1e-12)
        b = math.sqrt(sum(v * v for v in loop[h:]) / (length - h) + 1e-12)
        r = math.log(a / b) / h if b > 0 and a > b else 0.0
        loop = [v * math.exp(r * j) for j, v in enumerate(loop)]
    pre = y[s - xfade:s] if s >= xfade else [0.0] * xfade
    if flatten and pre:                                 # (its level brought to the loop's end)
        g = math.exp(r * length) if r else 1.0
        pre = [v * g for v in pre]
    for j in range(xfade):
        w = 0.5 - 0.5 * math.cos(math.pi * (j + 0.5) / xfade)
        loop[length - xfade + j] = (1 - w) * loop[length - xfade + j] + w * pre[j]
    return head + loop


# ----------------------------------------------------------------- synth ---
def synth(what, inst):
    """(samples, loop start or None, root16, rate) of a made wave"""
    if what == "square":
        x = [0.75 if i < 16 else -0.75 for i in range(32)] * 2      # 0.75: exact in a filter-0 block
        return x, 0, root_of_cycle(32), FS
    if what == "pulse25":
        x = [0.75 if i < 8 else -0.75 for i in range(32)] * 2
        return x, 0, root_of_cycle(32), FS
    if what == "saw":
        return norm(additive(64, [(h, (-1) ** (h + 1) / h) for h in range(1, 32)]) * 2), 0, root_of_cycle(64), FS
    if what == "sine":
        return norm(additive(64, [(1, 1.0), (2, 0.18), (3, 0.06)])), 0, root_of_cycle(64), FS
    if what == "strings":                               # three detuned saws in one 4096-sample loop
        n = 4096
        return norm(additive(n, [(k * h, 1.0 / h / (1 + (h / 9.0) ** 2))
                                 for k in (63, 64, 65) for h in range(1, 30) if k * h * FS / n < 15000])), 0, \
            root_of_cycle(64), FS
    if what == "synbass":                               # a low-passed saw and its sub-octave
        n = 128
        return norm(additive(n, [(2 * h, 1.0 / h / (1 + (h / 5.0) ** 2)) for h in range(1, 32)] + [(1, 0.6)])), 0, \
            root_of_cycle(n), FS
    if what == "organ":                                 # drawbars 16' 8' 5 1/3' 4' 2'
        n = 128
        return norm(additive(n, [(1, 0.8), (2, 1.0), (3, 0.6), (4, 0.5), (8, 0.3)])), 0, root_of_cycle(n), FS
    if what == "choir":                                 # "aah": harmonics under vowel formants, three voices
        n, f0 = 4096, 64 * FS / 4096
        formants = [(800, 80, 1.0), (1150, 90, 0.5), (2900, 120, 0.25), (3900, 130, 0.1)]

        def amp(f):
            return sum(g / (1 + ((f - fc) / bw) ** 2) for fc, bw, g in formants) + 0.02
        parts = [(k * h, amp(k * h * FS / n) / math.sqrt(h)) for k in (63, 64, 65) for h in range(1, 40)
                 if k * h * FS / n < 12000]
        return norm(additive(n, parts)), 0, root_of_cycle(64), FS
    if what == "orchhit":                               # an orchestra stab: a chord of bright saws, octaves
        rate = inst["rate"]                             # and a fifth over a timpani-like thump, falling fast
        n = int(inst["length"] * rate) // 16 * 16
        f0 = note_hz(60)
        ratios = [0.5, 1.0, 1.5, 2.0, 3.0, 4.0]
        x = []
        for i in range(n):
            t = i / rate
            env = math.exp(-t * 7.0) * min(1.0, t * 400)
            s = 0.0
            for r in ratios:
                for h in range(1, 12):
                    if f0 * r * h < rate * 0.45:
                        s += math.sin(2 * math.pi * f0 * r * h * t * (1 + 0.003 * h)) / h / r
            s += 1.5 * math.sin(2 * math.pi * 55 * t) * math.exp(-t * 12)
            x.append(s * env)
        x = norm(x)
        return x + [0.0] * 16, None, round(16 * (60 + 12 * math.log2(FS / rate))), rate
    raise SystemExit(f"gen_brr: no synth {what}")


def root_of_cycle(cycle):
    """the note of a cycle of `cycle` samples at 32 kHz, in 1/16 semitones"""
    return int(round((69 + 12 * math.log2(FS / cycle / 440.0)) * 16))


# ------------------------------------------------------------ recordings ---
def recording(inst):
    """(samples, rate, note, cents) of an instrument's recording, or None (missing)"""
    src = inst["src"]
    if src[0] == "genwav":
        p = GENWAV / (src[1] + ".wav")
        if not p.exists():                              # (made by gen_samples.py, which build.py runs first)
            raise SystemExit(f"gen_brr: {p} is missing: run tools/gen_samples.py first")
        sr, x = read_wav(p)
        return x, sr, 60, 0
    return None


def build(inst, drum):
    """(BRR bytes, loop byte offset or None, root16, info)"""
    if inst["src"][0] == "synth":
        x, loop, root, rate = synth(inst["src"][1], inst)
        b, rms = brr_encode(x, loop)
        return b, (loop // 16 * 9 if loop is not None else None), root, f"synth {inst['src'][1]}", rms
    rec = recording(inst)
    if rec is None:                                     # stand-in: a plucked or held tone
        stand = dict(inst, src=("synth", "sine"))
        b, lp, root, info, rms = build(stand, drum)
        return b, lp, root, "stand-in (no recording)", rms
    x, sr, note, cents = rec
    x = x[max(0, sio.onset(x) - int(0.002 * sr)):]
    kind, rate0 = inst["kind"], inst["rate"]
    if kind == "oneshot":
        rate = rate0
        n = min(len(x), int(inst["length"] * sr) + 64)
        y = resample(x[:n], sr, rate)[:int(inst["length"] * rate)]
        y = y[:len(y) // 16 * 16]
        fade = len(y) // 6
        y = [v * min(1.0, (len(y) - i) / fade) for i, v in enumerate(y)]
        y = norm(y, PEAK * inst["gain"]) + [0.0] * 16    # a silent last block (the chip mutes the end block)
        root = round(16 * (60 + 12 * math.log2(FS / rate)))   # key 60 plays it at its own rate
        b, rms = brr_encode(y, None)
        return b, None, root, f"{rate} Hz one-shot", rms
    if drum:                                            # a cymbal: no pitch, a loop of its tail
        rate, length = rate0, max(64, int(round(inst["loop"] * rate0 / 16)) * 16)
        period, root = 64, round(16 * (60 + 12 * math.log2(FS / rate)))
    else:
        f0 = note_hz(note) * 2 ** (cents / 1200)         # (the recording's, as given: a build measures nothing)
        rate, length, k = plan_loop(f0, rate0, inst["loop"])
        period = rate / f0
        root = round(16 * (69 + 12 * math.log2(f0 * FS / rate / 440.0)))
    att16 = int(math.ceil(inst["att"] * rate / 16)) * 16
    need = att16 + length * 2 + int(0.3 * rate) + 64
    y = resample(x[:int(need * sr / rate) + 64], sr, rate)
    s = place_loop(y, att16, length, int(0.25 * rate), period)
    xfade = length // 2 if drum else max(16, min(length // 2, int(3 * period)))
    z = make_loop(y, s, att16, length, xfade, kind == "decay")
    z = norm(z, PEAK * inst["gain"])
    b, rms = brr_encode(z, att16)
    info = f"{rate:.0f} Hz, loop {length} ({length / rate * 1000:.0f} ms)"
    if not drum:
        info += f", {k} cycles, root {root / 16:.2f}, reaches {root / 16 + 24:.0f}"
    return b, att16 // 16 * 9, root, info, rms


def cached(inst, drum):
    h = hashlib.sha256(json.dumps([VERSION, inst, drum], sort_keys=True, default=str).encode())
    rec_path = GENWAV / (inst["src"][1] + ".wav") if inst["src"][0] == "genwav" else None
    if rec_path is not None:
        h.update(rec_path.read_bytes() if rec_path.exists() else b"missing")
    f = CACHE / (h.hexdigest()[:24] + ".json")
    if f.exists():
        d = json.loads(f.read_text())
        return bytes.fromhex(d["brr"]), d["loop"], d["root"], d["info"], d["rms"]
    b, lp, root, info, rms = build(inst, drum)
    CACHE.mkdir(parents=True, exist_ok=True)
    f.write_text(json.dumps({"brr": b.hex(), "loop": lp, "root": root, "info": info, "rms": rms}))
    return b, lp, root, info, rms


# ------------------------------------------------------------------ main ---
def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    data, rows, info = bytearray(), [], []

    def add(name, b, lp, root, what, rms):
        start = len(data)
        data.extend(b)
        rows.append((name, start, start + (lp if lp is not None else 0), root))
        err = f", BRR error {20 * math.log10(max(rms, 1e-9)):.1f} dBFS" if rms is not None else ""
        info.append(f"{name:8} {len(b):6} B  {what}{err}")

    for inst in bank.INSTRUMENTS:
        add(inst["name"], *cached(inst, False))
    nmel = len(rows)
    for inst in bank.DRUMS:
        add(inst["name"], *cached(inst, True))
    index = {r[0]: i for i, r in enumerate(rows)}
    kits = list(bank.KITS.items())
    sel_names = [r[0] for r in rows[:nmel]] + [k for k, _ in kits]

    def pr(name, e, p):
        env = ", ".join(str(v) for v in p["env"])
        echo = p["echo"]
        return (f'    {{"{name}", {{{", ".join(str(v) for v in [e, p["tune"], p["sr"], p["noise"], *echo])}}}, '
                f'{{{env}}}, 0, {p["mono"]}, FX({", ".join(str(v) for v in p["fx"])}), PAT({p["pat"]})}},')
    presets = [pr(inst["name"], i, inst["preset"]) for i, inst in enumerate(bank.INSTRUMENTS)]
    presets += [pr(name, nmel + k, bank.KIT_PRESET) for k, (name, _) in enumerate(kits)]

    out = ["/* generated by tools/gen_brr.py: the SNES engine's instruments (eng_snes.c), tools/snes_bank.py */",
           "#ifndef FELUCCA_BRR_H", "#define FELUCCA_BRR_H",
           "typedef struct { uint32_t start, loop; int16_t root16; } snes_inst_t;   /* byte offsets in SNES_BRR; "
           "root: the note of pitch 0x1000, 1/16 st */",
           f"#define SNES_NINST {len(rows)}u            /* the instruments, then the drum pieces */",
           f"#define SNES_NMEL {nmel}u             /* INST 0 .. SNES_NMEL - 1: an instrument; then the kits */",
           f"#define SNES_NBANK {len(bank.INSTRUMENTS)}u            /* the bank's instruments (all of SNES_NMEL here) */",
           f"#define SNES_NKIT {len(kits)}u",
           f"#define SNES_NSEL {len(sel_names)}u             /* the values of INST */",
           f"#define SNES_BRR_LEN {len(data)}u",
           "static const char *const SNES_INST_NAMES[SNES_NSEL] = {" + ", ".join(f'"{n}"' for n in sel_names) + "};",
           "static const char *const SNES_ALL_NAMES[SNES_NINST] = {" + ", ".join(f'"{r[0]}"' for r in rows) + "};",
           "static const snes_inst_t SNES_INST[SNES_NINST] = {"]
    out += [f"    {{{r[1]}u, {r[2]}u, {r[3]}}},   /* {r[0]} */" for r in rows]
    out += ["};", "/* the kits: General MIDI note -> {piece (SNES_INST) + 1, semitones from its own pitch}; 0: no piece */",
            "typedef struct { uint8_t inst; int8_t tune; } snes_kit_t;",
            "static const snes_kit_t SNES_KITS[SNES_NKIT][128] = {"]
    for name, mp in kits:                               # every note written out (C and C++ both take it)
        cells = [f"{{{index[mp[n][0]] + 1}, {mp[n][1]}}}" if n in mp else "{0, 0}" for n in range(128)]
        out.append(f"    {{   /* {name}: notes 0 .. 127 */")
        for i in range(0, 128, 16):
            out.append("        " + ", ".join(cells[i:i + 16]) + ",")
        out.append("    },")
    out += ["};",
            "#ifdef SNES_PRESETS",
            "/* a preset per instrument and kit: {INST, TUNE, SR, NOISE, ECHO, DELAY, FDBK, FIR}, {ATK, DEC, SUS, REL} */",
            "static const preset_t SNES_PRESET_TABLE[] = {"] + presets + ["};", "#endif",
                                                                         "static const uint8_t SNES_BRR[SNES_BRR_LEN] = {"]
    for i in range(0, len(data), 18):
        out.append("    " + " ".join(f"0x{v:02X}," for v in data[i:i + 18]))
    out += ["};", "#endif", ""]
    text = "\n".join(out)
    Path(sys.argv[1]).write_text(text, newline="\n")   # (the same bytes on every OS)
    for ln in info:
        print("brr:", ln)
    print(f"brr: {nmel} instruments, {len(bank.DRUMS)} drum pieces, {len(kits)} kits, "
          f"{len(data)} B -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
