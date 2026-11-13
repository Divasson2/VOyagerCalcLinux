#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
"""Convert old HP-16C memory files to the 206-slot layout of VoyagerCalc 0.7.7.

VoyagerCalc 0.7.7 supports two HP-16C memory layouts: stock (29 slots) and the
extended 206-slot layout of the EAEA, C9C9 and ECEC firmware (R0-R720 at word
size 16). Memory files saved on the retired intermediate extended builds
(176 slots: B4B4, B5B5, B7B7, E9E9, E8E8; and the early E3E3 multi, the same
pool 8 bytes higher) no longer load. This tool moves their registers and program
into the 206-slot layout. A stock (29-slot) file can be converted too.

Only the 16C pool moves (data registers up from R0, the program down from the
top). The stack, flags, word size, program pointers, CPU header, the fixed
Continuous Memory constant and the keyboard bytes are not touched. The input
file is never changed; the output is a new file.

A raw 2048-byte memory file has no layout tag, so the source layout is
inferred from where the data sits. If that is not certain (a stock file and a
sparse 206-slot file can look the same) the tool stops and asks for --from.

Python 3.8 or newer, standard library only. Own code, MIT licence (a port of
VoyagerCalc's C converter). Never use it on 12C / 15C files, on a file saved
while a program was running, or on files from the B3B3 test build (its pool
was aliased, not linear; the file cannot tell, so the tool cannot detect it).
"""
import argparse
import sys
from pathlib import Path

SIZE = 2048
HEADER = bytes.fromhex("00080000")
MAGIC = bytes.fromhex("af5439fed967bc12")      # fixed Continuous Memory constant at +7F0 (not a checksum)
POOL_LOW, POOL_END = 0x168, 0x7D8              # every 16C pool lies inside 0x168..0x7D7 (8-byte slots)

# name: (file offset of R0, number of 8-byte slots); the Nut floor register is 0xFD - slots
LAYOUTS = {
    "206":   (0x168, 206),   # EAEA, C9C9, ECEC (VoyagerCalc 0.7.7)
    "176":   (0x168, 176),   # retired B4B4, B5B5, B7B7, E9E9, E8E8
    "e3e3":  (0x170, 176),   # retired early multi E3E3 (the 176-slot pool, 8 bytes higher)
    "stock": (0x6F0, 29),    # HP-16C CE stock, multi E6E6 (203 bytes)
}
DESCRIPTION = {
    "206": "206 slots (EAEA / C9C9 / ECEC)",
    "176": "176 slots (retired B4B4 / B5B5 / B7B7 / E9E9 / E8E8)",
    "e3e3": "176 slots, early E3E3 placement (retired)",
    "stock": "stock 29 slots (HP-16C CE / multi E6E6)",
}


class ConvertError(ValueError):
    pass


def _nz(b, start, end):
    return any(b[start:end])


def _pointer_lines(ptr):
    """Program length (lines) for a 12-bit FE/FF pointer, or None when invalid."""
    if not ptr:
        return 0
    addr, pos = ptr & 0xFF, ptr >> 8
    if not (1 <= pos <= 7 and 0x2F <= addr <= 0xFC):
        return None
    return (0xFC - addr) * 7 + pos


def _check_image(raw):
    if len(raw) != SIZE:
        raise ConvertError("not a 2048-byte calculator memory file (%d bytes)" % len(raw))
    if raw[:4] != HEADER or raw[0x7F0:0x7F8] != MAGIC:
        raise ConvertError("not a VoyagerCalc / HP CE Continuous Memory image (wrong header or constant)")
    if raw[0x4E] & 0x80:
        raise ConvertError("saved while extended RAM was selected (a program was running): "
                           "save the memory again with the calculator idle")


def program_info(raw):
    fe = int.from_bytes(raw[0x7E0:0x7E2], "little") & 0xFFF
    ff = int.from_bytes(raw[0x7E8:0x7EA], "little") & 0xFFF
    lines, current = _pointer_lines(fe), _pointer_lines(ff)
    if lines is None or current is None or lines > 999 or current > lines:
        raise ConvertError("invalid or inconsistent program pointers (not a 16C memory file?)")
    return fe, lines


def fits(raw, layout):
    """True when the image is structurally valid in this layout."""
    base, slots = LAYOUTS[layout]
    fe, lines = program_info(raw)
    if fe and (fe & 0xFF) < 0xFD - slots:
        return False
    if (lines + 6) // 7 > slots:
        return False
    return not (_nz(raw, POOL_LOW, base) or _nz(raw, base + slots * 8, POOL_END))


def detect(raw):
    """(layout or None, explanation). layout None means: cannot tell, use --from."""
    _check_image(raw)
    _, lines = program_info(raw)
    first = _nz(raw, 0x168, 0x170)      # R0 of the 176 / 206 layouts
    low = _nz(raw, 0x168, 0x6E8)        # inside the 176-slot pool
    gap = _nz(raw, 0x6E8, 0x6F0)        # top slot of the E3E3 pool
    high = _nz(raw, 0x6F0, POOL_END)    # the stock pool (top of the 206 pool)
    if not (low or gap or high):
        return "empty", "no registers or program are stored: the file fits every layout as it is"
    if not high and not gap:
        if lines == 0:
            return "176", ("only low registers and no program: 176 and 206 slots give identical bytes "
                           "(converting changes nothing)")
        return "176", "program at the top of the 176-slot pool and nothing above it"
    if not high and gap and not first:
        return "e3e3", "registers start 8 bytes later and the program sits at 0x6E8: early E3E3 layout"
    if high and not low and not gap:
        fe, _ = program_info(raw)
        if (fe and (fe & 0xFF) < 0xE0) or not fits(raw, "stock"):
            return "206", "the program is longer than a stock pool holds: already 206 slots"
        return None, ("data only at the top of the pool: this is either a stock (29-slot) file or a "
                      "sparse 206-slot file. Use --from stock to convert a stock file; a 206 file "
                      "needs no conversion")
    if high and (low or gap):
        if fits(raw, "206"):
            return "206", "registers low in the pool and the program at the very top: already 206 slots"
    raise ConvertError("data in places that match no 16C layout (a 12C / 15C file or a damaged file?)")


def convert(raw, source, target="206"):
    """Return the converted 2048-byte image. Raises ConvertError when data would be lost."""
    if source not in LAYOUTS or target not in LAYOUTS:
        raise ConvertError("unknown layout")
    _check_image(raw)
    if source == target:
        return bytes(raw)
    sb, ns = LAYOUTS[source]
    tb, nt = LAYOUTS[target]
    fe, lines = program_info(raw)
    if fe and ((fe & 0xFF) < 0xFD - ns or (fe & 0xFF) < 0xFD - nt):
        raise ConvertError("the program pointer lies outside the %s pool" % source)
    prog = (lines + 6) // 7
    if prog > ns or prog > nt:
        raise ConvertError("the program (%d lines) does not fit the %s pool" % (lines, target))
    if _nz(raw, POOL_LOW, sb) or _nz(raw, sb + ns * 8, POOL_END):
        raise ConvertError("data outside the %s pool: the file is not in that layout" % source)
    sf, tf = ns - prog, nt - prog                     # free (data) slots below the program
    if tf < sf and _nz(raw, sb + tf * 8, sb + sf * 8):
        raise ConvertError("high registers would not fit the smaller %s pool; clear them first" % target)
    out = bytearray(raw)
    out[POOL_LOW:POOL_END] = bytes(POOL_END - POOL_LOW)
    n = min(sf, tf) * 8
    out[tb:tb + n] = raw[sb:sb + n]                   # data registers, R0 upwards
    if prog:
        out[tb + tf * 8:tb + nt * 8] = raw[sb + sf * 8:sb + ns * 8]   # program slots, top down
    assert out[:POOL_LOW] == raw[:POOL_LOW] and out[POOL_END:] == raw[POOL_END:]
    return bytes(out)


def default_output(path, target):
    suffix = path.suffix or ".16c"
    return path.with_name("%s-%s%s" % (path.stem, target, suffix))


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="convert_16c_memory.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__.split("\n\n")[0] + "\n\n" + "\n\n".join(__doc__.split("\n\n")[1:4]),
        epilog="""examples:
  python3 convert_16c_memory.py old-B7B7-program.16c
        writes old-B7B7-program-206.16c next to it (layout detected)
  python3 convert_16c_memory.py --from stock my-stock.16c -o my-206.16c
  python3 convert_16c_memory.py --check *.16c
        only report the detected layout of each file

layouts: 206 (EAEA / C9C9 / ECEC), 176 (B4B4 / B5B5 / B7B7 / E9E9 / E8E8),
         e3e3 (early E3E3 multi), stock (29 slots: HP-16C CE, multi E6E6)
not supported: 12C / 15C files, files saved while a program ran, B3B3 files

Load the result in VoyagerCalc with an EAEA, C9C9 or ECEC firmware in 16C mode
(File > Open Memory File...). Exit status: 0 done, 1 error, 2 bad arguments,
3 layout not certain (use --from).""")
    p.add_argument("inputs", nargs="+", type=Path, metavar="FILE", help="old .16c memory file(s)")
    p.add_argument("-o", "--output", type=Path, help="output file (only with one input; default FILE-206.16c)")
    p.add_argument("--from", dest="source", choices=sorted(LAYOUTS), help="source layout (default: detect)")
    p.add_argument("--to", dest="target", choices=sorted(LAYOUTS), default="206",
                   help="target layout (default 206; 176 / stock only for going back)")
    p.add_argument("--check", action="store_true", help="only report the detected layout; write nothing")
    p.add_argument("--force", action="store_true", help="overwrite an existing output file")
    a = p.parse_args(argv)
    if a.output and len(a.inputs) != 1:
        p.error("--output needs exactly one input file")
    status = 0
    for path in a.inputs:
        try:
            raw = path.read_bytes()
            if a.source:
                _check_image(raw)
                source, why = a.source, "given with --from"
            else:
                source, why = detect(raw)
            if a.check:
                print("%s: %s (%s)" % (path, DESCRIPTION.get(source, source or "not certain"), why))
                if source is None:
                    status = max(status, 3)
                continue
            if source is None:
                print("%s: layout not certain: %s" % (path, why), file=sys.stderr)
                status = max(status, 3)
                continue
            if source in ("empty", a.target):
                print("%s: nothing to convert (%s)" % (path, why))
                continue
            out = convert(raw, source, a.target)
            dst = a.output or default_output(path, a.target)
            if dst.exists() and not a.force:
                raise ConvertError("%s exists; use --force to overwrite" % dst)
            dst.write_bytes(out)
            print("%s: %s -> %s: wrote %s (keep the original)" % (path, DESCRIPTION[source], DESCRIPTION[a.target], dst))
        except (OSError, ConvertError) as e:
            print("%s: error: %s" % (path, e), file=sys.stderr)
            status = 1 if status != 3 else 3
    return status


if __name__ == "__main__":
    sys.exit(main())
