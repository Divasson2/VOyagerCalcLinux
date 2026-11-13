#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
"""Self-contained tests for convert_16c_memory.py (synthetic images only; standard library)."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import convert_16c_memory as cv  # noqa: E402


def blank():
    b = bytearray(2048)
    b[0:4] = cv.HEADER
    b[0x7F0:0x7F8] = cv.MAGIC
    b[0x7F8:] = bytes(range(0xF8, 0x100))       # keyboard tail stand-in: must survive untouched
    b[0x100:0x110] = bytes(range(16))           # stack / CPU stand-in
    return b


def set_lines(b, lines):
    ptr = 0 if not lines else ((0xFC - (lines - 1) // 7) | (((lines - 1) % 7 + 1) << 8))
    b[0x7E0:0x7E2] = ptr.to_bytes(2, "little")
    b[0x7E8:0x7EA] = ptr.to_bytes(2, "little")


def image(layout, regs_bits, lines, ws=16):
    """Registers 1..N bit-packed from R0 and program byte j = (j*7+3)&255 (hwtest scheme)."""
    base, slots = cv.LAYOUTS[layout]
    b = blank()
    for i in range(regs_bits):
        v = i + 1
        for j in range(ws):
            bit = i * ws + j
            off = base + (bit // 56) * 8 + (bit % 56) // 8
            b[off] |= ((v >> j) & 1) << (bit % 8)
    for j in range(lines):
        b[base + (slots - 1 - j // 7) * 8 + j % 7] = (j * 7 + 3) & 0xFF or 1
    set_lines(b, lines)
    return bytes(b)


def verify206(b, regs, lines, ws=16):
    """The 206-slot read-back rule of the pool-206 hardware test (verify_readback.py)."""
    for i in range(regs):
        v = 0
        for j in range(ws):
            bit = i * ws + j
            off = 0x168 + (bit // 56) * 8 + (bit % 56) // 8
            v |= ((b[off] >> (bit % 8)) & 1) << j
        if v != i + 1:
            return "R%d" % i
    for j in range(lines):
        if b[0x168 + (205 - j // 7) * 8 + j % 7] != ((j * 7 + 3) & 0xFF or 1):
            return "line %d" % (j + 1)
    return None


class Convert(unittest.TestCase):
    def test_176_every_program_length_round_trip(self):
        for lines in range(1000):
            with self.subTest(lines=lines):
                a = bytearray(blank())
                n = (lines + 6) // 7
                for j in range((176 - n) * 8):          # free slots full of data
                    a[0x168 + j] = (j * 31 + lines) & 0xFF
                for j in range(lines):
                    a[0x168 + (175 - j // 7) * 8 + j % 7] = (j * 13 + 1) & 0xFF
                set_lines(a, lines)
                a = bytes(a)
                b = cv.convert(a, "176", "206")
                if n:
                    self.assertEqual(b[0x168 + (206 - n) * 8:0x7D8], a[0x168 + (176 - n) * 8:0x6E8])
                self.assertEqual(a[:0x168], b[:0x168])
                self.assertEqual(a[0x7D8:], b[0x7D8:])
                self.assertEqual(a, cv.convert(b, "206", "176"))

    def test_readback_rule_after_176_to_206(self):
        a = image("176", 300, 600)                       # 86 + 86 slots: fits 176
        self.assertEqual(cv.detect(a)[0], "176")
        b = cv.convert(a, "176", "206")
        self.assertIsNone(verify206(b, 300, 600))
        self.assertEqual(cv.detect(b)[0], "206")

    def test_e3e3_equals_176(self):
        a = image("176", 120, 210)
        e = cv.convert(a, "176", "e3e3")
        self.assertEqual(cv.detect(e)[0], "e3e3")
        self.assertEqual(cv.convert(e, "e3e3", "206"), cv.convert(a, "176", "206"))

    def test_stock_to_206(self):
        a = image("stock", 20, 70)                        # 6 + 10 slots of 29
        self.assertIsNone(cv.detect(a)[0])                # stock vs sparse 206: needs --from
        b = cv.convert(a, "stock", "206")
        self.assertIsNone(verify206(b, 20, 70))
        self.assertEqual(cv.convert(b, "206", "stock"), a)

    def test_long_program_at_top_is_206(self):
        a = image("206", 0, 400)
        self.assertEqual(cv.detect(a)[0], "206")

    def test_refusals(self):
        good = image("176", 10, 20)
        bad = [good[:-1], bytes(2048), good[:0x4E] + b"\x80" + good[0x4F:],
               good[:0x7E0] + bytes.fromhex("ff0f") + good[0x7E2:]]
        for raw in bad:
            with self.assertRaises(cv.ConvertError):
                cv.convert(raw, "176", "206")
        full = image("206", 721, 0)
        with self.assertRaises(cv.ConvertError):          # data in the top 30 slots does not fit 176
            cv.convert(full, "206", "176")
        with self.assertRaises(cv.ConvertError):          # not a stock file
            cv.convert(good, "stock", "206")

    def test_cli_never_overwrites(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d, "old.16c")
            src.write_bytes(image("176", 50, 117))
            run = lambda *a: subprocess.run([sys.executable, str(HERE / "convert_16c_memory.py")] + list(a),
                                            capture_output=True, text=True)
            r = run(str(src))
            self.assertEqual(r.returncode, 0, r.stderr)
            out = Path(d, "old-206.16c")
            self.assertEqual(out.read_bytes(), cv.convert(src.read_bytes(), "176", "206"))
            self.assertEqual(run(str(src)).returncode, 1)          # exists: refused
            self.assertEqual(run("--check", str(out)).returncode, 0)
            stock = Path(d, "stock.16c")
            stock.write_bytes(image("stock", 5, 10))
            self.assertEqual(run(str(stock)).returncode, 3)       # not certain without --from
            self.assertEqual(run("--from", "stock", str(stock)).returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=1)
