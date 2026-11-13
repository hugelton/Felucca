#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package flash boundaries with a synthetic SDK, no device or vendor files.

Run from the repo root with Python normally and with -O; unittest checks remain
active when the packer's assertions are disabled.
"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1pkg_make as P  # noqa: E402


class FlashImageTest(unittest.TestCase):
    def setUp(self):
        """Create a temporary synthetic SDK and restore the SDK path after each test."""
        (ROOT / "build").mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="fm1pkg-", dir=ROOT / "build")
        self.addCleanup(self.tmp.cleanup)
        old_sdk = P.SDK
        self.addCleanup(setattr, P, "SDK", old_sdk)
        P.SDK = Path(self.tmp.name)
        self.tools = P.SDK / "cpu" / "wl82" / "tools"
        (self.tools / "cfg").mkdir(parents=True)
        (self.tools / "uboot.boot").write_bytes(bytes(range(32)))
        self.sdk(0, 0)

    def sdk(self, cfg_size, eq_size):
        """Write synthetic configuration and EQ files with the given byte sizes."""
        self.cfg = b"C" * cfg_size
        self.eq = b"E" * eq_size
        (self.tools / "cfg_tool.bin").write_bytes(self.cfg)
        (self.tools / "cfg" / "eq_cfg_hw.bin").write_bytes(self.eq)

    def check_image(self, app):
        """Verify image size, decrypted app data, padding, and SDK configuration."""
        image = P.flash_image(app, P.KEY)
        self.assertEqual(len(image), P.FLASH_SIZE)
        region = bytearray(image[0x4000:])
        P.sfc(region, 0, len(region), 0, P.KEY)
        blk = 0x120 + P.APP_SLOT + len(self.cfg)
        self.assertEqual(struct.unpack_from("<I", region, 8)[0], blk)
        self.assertEqual(region[0x120:0x120 + len(app)], app)
        self.assertEqual(region[0x120 + len(app):0x120 + P.APP_SLOT],
                         b"\xFF" * (P.APP_SLOT - len(app)))
        self.assertEqual(region[0x120 + P.APP_SLOT:blk], self.cfg)
        self.assertEqual(region[blk + 16:blk + 19], b"cfg")
        self.assertEqual(region[blk + 48:blk + 62], b"eq_cfg_hw.bin\0")
        self.assertEqual(region[blk + 64:blk + 64 + len(self.eq)], self.eq)

    def test_small_sdk(self):
        """Accept a small app with empty SDK configuration and EQ files."""
        self.check_image(b"APP!")

    def test_exact_fit(self):
        """Accept configuration and EQ combinations that exactly fill flash."""
        # 32 B SPL, 4 B app: 3812 B cfg + empty EQ fills flash exactly.
        self.assertEqual(P.FLASH_SIZE, 602112)
        self.assertEqual(P.FLASH_SIZE - 0x4000 - 0x120 - P.APP_SLOT - 64, 3812)
        for cfg_size, eq_size in ((3812, 0), (0, 3812), (1906, 1906)):
            with self.subTest(cfg=cfg_size, eq=eq_size):
                self.sdk(cfg_size, eq_size)
                self.check_image(b"APP!")

    def test_overflow(self):
        """Reject configuration or EQ overflow with size details in the error."""
        for cfg_size, eq_size in ((3813, 0), (7908, 0), (0, 3813), (0, 7908), (3812, 1)):
            with self.subTest(cfg=cfg_size, eq=eq_size):
                self.sdk(cfg_size, eq_size)
                extent = 0x120 + P.APP_SLOT + cfg_size + 64 + eq_size
                with self.assertRaises(SystemExit) as raised:
                    P.flash_image(b"APP!", P.KEY)
                message = str(raised.exception)
                self.assertIn(f"{extent} B", message)
                self.assertIn(f"{P.FLASH_SIZE - 0x4000} B", message)
                self.assertIn(f"{extent - (P.FLASH_SIZE - 0x4000)} B too much", message)
                self.assertIn(f"cfg_tool.bin {cfg_size} B", message)
                self.assertIn(f"cfg/eq_cfg_hw.bin {eq_size} B", message)

    def test_app_slot_limit(self):
        """Accept a full app slot and reject an app exceeding it by one byte."""
        self.sdk(3812, 0)
        self.check_image(b"A" * P.APP_SLOT)
        with self.assertRaisesRegex(SystemExit, f"app is {P.APP_SLOT + 1} B, the slot is {P.APP_SLOT} B"):
            P.flash_image(b"A" * (P.APP_SLOT + 1), P.KEY)


if __name__ == "__main__":
    unittest.main()
