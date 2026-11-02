#!/usr/bin/env python3
"""
Build graphics_engine/VGA9.F16 — the 8x16 VGA text-mode font used by gedi-gui.

The engine is single-byte (256 glyphs), so we keep the classic CP437 layout
(ASCII, box-drawing, blocks, shades — everything the editor frame relies on) and
repurpose the slots CP437 wastes on currency / Greek / math symbols (which the
editor never draws) for international Latin letters, giving the widest language
coverage that fits in 256 glyphs.

Source glyphs come from the system console font Uni2-VGA16 (kbd package), which
ships a full Unicode table. The base CP437 glyphs come from VGA9.F16.cp437.bak.

The (codepoint -> byte slot) assignment below MUST stay in sync with
uni_to_cp437() in ../curses_compat.cpp.

Usage:  python3 make_intl_font.py   (run from graphics_engine/)
"""
import gzip, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE_CP437 = os.path.join(HERE, "VGA9.F16.cp437.bak")
OUT        = os.path.join(HERE, "VGA9.F16")
SRC_PSF    = "/usr/share/consolefonts/Uni2-VGA16.psf.gz"

# Repurposable CP437 slots (currency / Greek / math — unused by the editor).
SLOTS = [0x9B,0x9C,0x9D,0x9E,0x9F, 0xA6,0xA7,0xA9,0xAA,0xAB,0xAC,
         0xE0,0xE2,0xE3,0xE4,0xE5,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
         0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFB,0xFC,0xFD,0xFE]

# Priority-ordered code points to add (most languages first).
TARGETS = [
 0x00F8,0x00D8,0x00E3,0x00F5,0x00C3,0x00D5,0x00F0,0x00D0,0x00FE,0x00DE,
 0x00FD,0x00DD,0x0142,0x0141,0x0105,0x0119,0x0107,0x0144,0x015B,0x017A,
 0x017C,0x010D,0x0161,0x017E,0x011B,0x0159,0x016F,0x0151,0x0171,0x0103,
 0x0219,0x021B,0x011F,0x0131,0x0130,0x015F,0x0111,0x0110,0x20AC,
]


def load_psf1(path):
    d = gzip.open(path, "rb").read()
    assert d[:2] == b"\x36\x04", "not a PSF1 font"
    mode, h = d[2], d[3]
    ng = 512 if (mode & 1) else 256
    glyphs = [d[4 + i * h: 4 + (i + 1) * h] for i in range(ng)]
    uni, tbl, gi, i = {}, d[4 + ng * h:], 0, 0
    while i + 1 < len(tbl) and gi < ng:
        cp = tbl[i] | (tbl[i + 1] << 8); i += 2
        if cp == 0xFFFF: gi += 1; continue
        if cp == 0xFFFE: continue
        uni.setdefault(cp, gi)
    return glyphs, uni


def main():
    src, uni = load_psf1(SRC_PSF)
    font = bytearray(open(BASE_CP437, "rb").read())
    assert len(font) == 4096
    assert len(TARGETS) <= len(SLOTS)
    for cp, slot in zip(TARGETS, SLOTS):
        gi = uni.get(cp)
        if gi is None:
            print("missing U+%04X in source font" % cp, file=sys.stderr); continue
        g = src[gi].ljust(16, b"\x00")[:16]
        font[slot * 16: slot * 16 + 16] = g
    open(OUT, "wb").write(bytes(font))
    print("wrote %s (%d glyphs patched)" % (OUT, len(TARGETS)))


if __name__ == "__main__":
    main()
