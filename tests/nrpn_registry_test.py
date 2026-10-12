#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Protect published NRPN addresses independently of the parameter enum order."""
import hashlib
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def entries(text):
    result = []
    for page, section in enumerate(("COMMON", "ENGINE")):
        match = re.search(r"#define MIDI_NRPN_" + section + r"\(X\)(.*?)(?:\n\n)", text, re.S)
        if not match:
            raise ValueError("missing registry page: " + section)
        result.extend((page, int(lsb), symbol, name) for lsb, symbol, name in
                      re.findall(r'X\((\d+), (P_\w+), "([^"]+)"\)', match[1]))
    return result


class NRPNRegistryTest(unittest.TestCase):
    def setUp(self):
        self.entries = entries((ROOT / "firmware/src/midi_nrpn_map.h").read_text())

    def test_frozen_addresses(self):
        payload = "\n".join(f"{p}:{l}:{s}" for p, l, s, _ in self.entries).encode()
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         "84c597d473ffb074b7ab26e6534375fecd2bf180f7712a85bb5288a6125ac462")

    def test_reserved_addresses_and_unique_destinations(self):
        expected = {(0, i) for i in range(103) if i != 8 and not 61 <= i <= 80}
        expected |= {(1, i) for i in range(8)}
        self.assertEqual({(p, l) for p, l, _, _ in self.entries}, expected)
        self.assertEqual(len(self.entries), 90)
        self.assertEqual(len({s for _, _, s, _ in self.entries}), 90)

    def test_documented_map(self):
        rows = re.findall(r'^\| (\d+):(\d+) \| ([^|]+?) \| `(P_\w+)` \|',
                          (ROOT / "docs/midi-nrpn.md").read_text(), re.M)
        self.assertEqual([(int(p), int(l), s, n.strip()) for p, l, n, s in rows],
                         self.entries)


if __name__ == "__main__":
    unittest.main()
