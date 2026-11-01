#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The FM-1's plugin engines over USB-MIDI (the editor's PLUGIN commands, firmware/src/editor_plugin.c): a firmware
built with FELUCCA_PLUGINS=1.

  fm1_plugin.py info                     the slots and the numbers given
  fm1_plugin.py engines                  the plugin engines listed (their numbers and names)
  fm1_plugin.py install PLUGIN.sK.fep    into the slot it is linked for (Felucca-Engines tools/build_plugin_fm1.sh),
                                         its number registered if it has none of Felucca's; then the FM-1 restarts
  fm1_plugin.py erase SLOT               the slot emptied (its number plays what it did before); then a restart
  fm1_plugin.py restart
  [--port NAME] [--no-restart]

Needs mido with python-rtmidi.
"""
import argparse
import queue
import re
import struct
import sys
import time

HDR = b"\xF0\x7D\x46\x4C"
INFO, ERASE, WRITE, REG, RESTART, ENGINES = 82, 83, 84, 85, 86, 87
STATES = ["empty", "playing", "not a good blob", "refused", "skipped after a crash", "no number"]
RC = ["done", "bad arguments", "the flash failed", "the slot is playing", "the list is full", "an update record's mark"]
FM1_MAGIC, HDR_SIZE, FE_NEW, ENGI_PLUG0 = 0x31504546, 96, 0xFF, 32
PORT_RE = re.compile(r"felucca", re.I)


def u32(v):
    return [(v >> (7 * i)) & 0x7F for i in range(5)]


def pack7(data):
    out = []
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum(((b >> 7) & 1) << j for j, b in enumerate(g)))
        out += [b & 0x7F for b in g]
    return out


class Dev:
    def __init__(self, port):
        import mido
        names = mido.get_input_names()
        ins = [n for n in names if (port or "").lower() in n.lower()] if port else [n for n in names if PORT_RE.search(n)]
        outs = [n for n in mido.get_output_names() if n in ins or (ins and ins[0].split(":")[0] in n)]
        if not ins or not outs:
            sys.exit(f"fm1_plugin: no Felucca port (inputs: {names})")
        self.q = queue.Queue()
        self.inp = mido.open_input(ins[0], callback=lambda m: m.type == "sysex" and self.q.put(bytes(m.data)))
        self.out = mido.open_output(outs[0])
        self.mido = mido

    def ask(self, cmd, args=(), timeout=3.0):
        while not self.q.empty():
            self.q.get_nowait()
        self.out.send(self.mido.Message("sysex", data=list(HDR[1:]) + [cmd] + list(args)))
        end = time.time() + timeout
        while time.time() < end:
            try:
                d = self.q.get(timeout=end - time.time())
            except queue.Empty:
                break
            if d[:4] == HDR[1:] + bytes([cmd]):
                return d[4:]
        sys.exit(f"fm1_plugin: no answer to command {cmd} (a firmware without FELUCCA_PLUGINS?)")


def g32(b, i):
    return sum(b[i + k] << (7 * k) for k in range(5))


def info(dev):
    r = dev.ask(INFO)
    if len(r) < 3:
        sys.exit(f"fm1_plugin: the flash is not available ({list(r)})")
    abi, ns, i = r[0], r[1], 2
    print(f"plugin ABI {abi}, {ns} slots")
    for k in range(ns):
        st, pid, ver, no = r[i], g32(r, i + 1), g32(r, i + 6), r[i + 11]
        i += 12
        j = r.index(0, i)
        name = r[i:j].decode()
        i = j + 1
        what = STATES[st] if st < len(STATES) else f"state {st}"
        if st == 0:
            print(f"  slot {k}: empty")
        else:
            print(f"  slot {k}: {name or '?'} id {pid:08x} version {ver}, "
                  f"{'engine ' + str(no) if no != 127 else 'its own number'}: {what}")
    n = r[i]
    i += 1
    regs = []
    for _ in range(n):
        regs.append((r[i] + ENGI_PLUG0, g32(r, i + 1)))
        i += 6
    print("  numbers given: " + (", ".join(f"{no} = {pid:08x}" for no, pid in regs) if regs else "none"))


def engines(dev):
    r = dev.ask(ENGINES)
    n, i = r[0], 1
    print(f"{n} plugin engines" + (":" if n else ""))
    for _ in range(n):
        no = r[i]
        j = r.index(0, i + 1)
        print(f"  {no}: {r[i + 1:j].decode()}")
        i = j + 1


def check(rc, what):
    if rc:
        sys.exit(f"fm1_plugin: {what}: {RC[rc] if rc < len(RC) else rc}")


def install(dev, path, restart):
    blob = open(path, "rb").read()
    if len(blob) < HDR_SIZE or struct.unpack_from("<I", blob)[0] != FM1_MAGIC:
        sys.exit(f"fm1_plugin: {path} is not an FM-1 plugin (.fep)")
    pid, ver, engine = struct.unpack_from("<III", blob, 16)
    name = blob[64:80].split(b"\0")[0].decode()
    slot = struct.unpack_from("<I", blob, 80)[0]
    print(f"{path}: {name} id {pid:08x} version {ver}, slot {slot}, {len(blob)} B")
    if engine == FE_NEW:
        r = dev.ask(REG, u32(pid))
        check(r[1], "the number")
        print(f"  plays as engine {r[0] + ENGI_PLUG0}")
    r = dev.ask(ERASE, [slot], timeout=5.0)
    check(r[1], "erase")
    for off in range(0, len(blob), 256):
        r = dev.ask(WRITE, [slot, off & 0x7F, (off >> 7) & 0x7F, (off >> 14) & 0x7F] + pack7(blob[off:off + 256]))
        check(r[4], f"write at {off:#x}")
    print("  written")
    if restart:
        reboot(dev)


def reboot(dev):
    dev.ask(RESTART)
    print("  restarting")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["info", "engines", "install", "erase", "restart"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--port")
    ap.add_argument("--no-restart", action="store_true")
    a = ap.parse_args()
    dev = Dev(a.port)
    if a.what == "info":
        info(dev)
    elif a.what == "engines":
        engines(dev)
    elif a.what == "install":
        if not a.arg:
            ap.error("install needs a .fep")
        install(dev, a.arg, not a.no_restart)
    elif a.what == "erase":
        if a.arg not in ("0", "1"):
            ap.error("erase needs a slot, 0 or 1")
        r = dev.ask(ERASE, [int(a.arg)], timeout=5.0)
        check(r[1], "erase")
        print(f"slot {a.arg} erased")
        if not a.no_restart:
            reboot(dev)
    else:
        reboot(dev)


if __name__ == "__main__":
    main()
