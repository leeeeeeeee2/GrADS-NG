#!/usr/bin/env python3
"""Render-plot arm: gxprint PNG/PPM on real instance data (goal M5 slice 2).

Renders July-15 tmax over the Great Plains window plus an all-missing
Atlantic window, then validates every byte independently (stdlib only):
PNG signature/chunks/CRCs, IHDR dims, inflate size, shade orientation
(south-hot-light vs north-cool-dark), contour presence, magenta missing,
and PPM/PNG raster identity. Also pins the .gif honest error.
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import CONV, NG_BIN, NeedsConv  # noqa: E402

W, H = 2420, 2420  # GP window 242x242 at 10 px per cell


def run_ng(cmds, cwd):
    p = subprocess.run([NG_BIN, "-o", cwd], input="\n".join(cmds) + "\nquit\n",
                       capture_output=True, text=True, timeout=600)
    return p.returncode, p.stdout, p.stderr


def read_png(path):
    """(ihdr_dims, rows): validated chunks, inflated scanlines with filter
    bytes verified zero and stripped, so rows are pure w*3 pixel bytes."""
    with open(path, "rb") as f:
        raw = f.read()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature"
    pos, idat, dims = 8, b"", None
    while pos < len(raw):
        ln, typ = struct.unpack(">I4s", raw[pos:pos + 8])
        data = raw[pos + 8:pos + 8 + ln]
        want = struct.unpack(">I", raw[pos + 8 + ln:pos + 12 + ln])[0]
        assert (zlib.crc32(typ + data) & 0xffffffff) == want, typ
        if typ == b"IHDR":
            dims = struct.unpack(">IIBBBBB", data)
        if typ == b"IDAT":
            idat += data
        pos += 12 + ln
        if typ == b"IEND":
            break
    w, h = dims[0], dims[1]
    blob = zlib.decompress(idat)
    stride = 1 + w * 3
    assert len(blob) == h * stride, len(blob)
    rows = []
    for i in range(h):
        assert blob[i * stride] == 0, "filter byte must be 0"
        rows.append(blob[i * stride + 1:(i + 1) * stride])
    return dims, rows


class RenderPlotTest(NeedsConv):
    @classmethod
    def setUpClass(cls):
        NeedsConv.setUpClass()
        cls.tmp = tempfile.mkdtemp(prefix="nclimgrid-plot-")
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng([
            "open %s" % ctl,
            "set t 15", "set x 593 834", "set y 251 492",
            "d tmax",
            "gxprint gp_tmax.png",
            "gxprint gp_tmax.ppm",
            "set x 1360 1385", "set y 100 120",
            "d tmax",
            "gxprint atl_tmax.png",
        ], cls.tmp)
        assert rc == 0, err
        cls.out = out

    def test_display_announces_contours(self):
        self.assertIn("9 contour levels", self.out)

    def test_png_valid_and_sized(self):
        dims, rows = read_png(os.path.join(self.tmp, "gp_tmax.png"))
        self.assertEqual(dims[:4], (W, H, 8, 2))
        self.assertEqual(len(rows), H)
        self.assertTrue(all(len(r) == W * 3 for r in rows))

    def test_shade_orientation_and_range(self):
        _, rows = read_png(os.path.join(self.tmp, "gp_tmax.png"))
        south = rows[2415][1210 * 3]
        north = rows[5][1210 * 3]
        # July Plains: hot bright south, cool dark north.
        self.assertGreater(south, 200, south)
        self.assertLess(north, 100, north)

    def test_contours_present_not_dominant(self):
        _, rows = read_png(os.path.join(self.tmp, "gp_tmax.png"))
        black = sum(1 for r in rows for i in range(0, len(r), 3)
                    if r[i:i + 3] == b"\x00\x00\x00")
        # Far above the single min-valued cell (100 px), far below a
        # filled frame: real 1px contour lines.
        self.assertGreater(black, 1000, black)
        self.assertLess(black, W * H // 2, black)

    def test_ppm_matches_png_raster(self):
        with open(os.path.join(self.tmp, "gp_tmax.ppm"), "rb") as f:
            ppm = f.read()
        self.assertTrue(ppm.startswith(b"P6\n"))
        body = ppm[ppm.index(b"255\n") + 4:]
        _, rows = read_png(os.path.join(self.tmp, "gp_tmax.png"))
        self.assertEqual(body, b"".join(rows))

    def test_all_missing_renders_magenta(self):
        dims, rows = read_png(os.path.join(self.tmp, "atl_tmax.png"))
        self.assertEqual(dims[:2], (260, 210))
        magenta = sum(1 for r in rows for i in range(0, len(r), 3)
                      if r[i:i + 3] == b"\xff\x00\xff")
        self.assertEqual(magenta, 260 * 210, magenta)

    def test_gif_honest_error(self):
        ctl = os.path.join(CONV, "julall.ctl")
        _, out, err = run_ng(["open %s" % ctl, "d tmax", "gxprint x.gif"],
                             self.tmp)
        self.assertIn("only .ppm/.png output is implemented", err)
        self.assertNotIn("Wrote", out)


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
