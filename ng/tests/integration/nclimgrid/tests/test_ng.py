#!/usr/bin/env python3
"""NG arm: grads-ng CLI on the converted fixture vs goldens (goal §6, §9-14).

Covers open/q/set/dimensions, time selection, grid-index spatial windows,
expression evaluation, derived fields, reductions, math functions, missing
values, and honest errors. Every numeric expectation comes from expected/.
"""
import os
import re
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import (CONV, NG_BIN, NeedsConv, load_golden)  # noqa: E402

STAT_RE = re.compile(
    r"Displaying (.*): (\d+) x (\d+) \(t=(\d+), z=(\d+)\)\n"
    r"(?:  all (\d+) values missing\n"
    r"|  Min: (\S+)  Max: (\S+)  Mean: (\S+) over (\d+) values"
    r"(?: \((\d+) missing\))?\n)")


def run_ng(cmds):
    p = subprocess.run([NG_BIN], input="\n".join(cmds) + "\nquit\n",
                       capture_output=True, text=True, timeout=600)
    return p.returncode, p.stdout, p.stderr


def parse_displays(out):
    found = {}
    for m in STAT_RE.finditer(out):
        label, nx, ny, t, z = m.group(1, 2, 3, 4, 5)
        if m.group(6) is not None:
            found[label] = {"nx": int(nx), "ny": int(ny), "t": int(t),
                            "all_missing": int(m.group(6))}
        else:
            found[label] = {"nx": int(nx), "ny": int(ny), "t": int(t),
                            "min": float(m.group(7)), "max": float(m.group(8)),
                            "mean": float(m.group(9)), "n": int(m.group(10)),
                            "nmissing": int(m.group(11) or 0)}
    return found


def close(a, b, tol=1e-2, rel=1e-4):
    return abs(a - b) <= tol + rel * max(abs(a), abs(b))


class OpenTest(NeedsConv):
    def test_open_qfile_dims(self):
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, _ = run_ng(["open %s" % ctl, "q file", "q dims"])
        self.assertEqual(rc, 0, out)
        self.assertIn("Dimensions: 1385 x 596 x 1 x 31", out)
        for v in ("tmax", "tmin", "tavg", "prcp"):
            self.assertIn(v, out)
        self.assertIn("X: 1..1385 of 1385  Y: 1..596 of 596", out)

    def test_open_missing_is_hard_error(self):
        # Interactive sessions exit 0; the contract is a loud error and
        # no file summary.
        rc, out, err = run_ng(["open /nonexistent/nclimgrid.ctl"])
        self.assertIn("could not open", err)
        self.assertNotIn("Dimensions:", out)


class TimeSelectTest(NeedsConv):
    @classmethod
    def setUpClass(cls):
        NeedsConv.setUpClass()
        cls.fields = load_golden("fields.json")
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng([
            "open %s" % ctl,
            "set t 1", "d tmax",
            "set t 31", "d tmax",
            "set t 15", "q dims",
        ])
        assert rc == 0, err
        cls.d = parse_displays(out)
        cls.out = out

    def blocks_for(self, label, t):
        return [m for m in STAT_RE.finditer(self.out)
                if m.group(1) == label and m.group(4) == str(t)]

    def check_block(self, label, t, golden):
        blocks = self.blocks_for(label, t)
        self.assertEqual(len(blocks), 1, (label, t))
        m = blocks[0]
        got = {"nx": int(m.group(2)), "ny": int(m.group(3)),
               "min": float(m.group(7)), "max": float(m.group(8)),
               "mean": float(m.group(9)),
               "nmissing": int(m.group(11) or 0)}
        g = self.fields[golden]
        self.assertEqual((got["nx"], got["ny"]), (1385, 596))
        self.assertEqual(got["nmissing"], g["nundef"])
        self.assertTrue(close(got["min"], g["min"]), (got["min"], g["min"]))
        self.assertTrue(close(got["max"], g["max"]), (got["max"], g["max"]))
        self.assertTrue(close(got["mean"], g["mean"], tol=5e-2),
                        (got["mean"], g["mean"]))

    def test_t1_matches_reference(self):
        self.check_block("tmax", 1, "ref_tmax_t01")

    def test_t31_matches_reference(self):
        self.check_block("tmax", 31, "ref_tmax_t31")

    def test_qdims_reports_selection(self):
        self.assertIn("T: 15 of 31", self.out)


class SpatialWindowTest(NeedsConv):
    @classmethod
    def setUpClass(cls):
        NeedsConv.setUpClass()
        cls.fields = load_golden("fields.json")
        cls.cities = {r["city"]: r for r in load_golden("cities.csv")}
        cls.oceans = {r["point"]: r for r in load_golden("oceans.csv")}
        ctl = os.path.join(CONV, "julall.ctl")
        # Great Plains window (1-based): x 593..834, y 251..492, t=15.
        rc, out, err = run_ng([
            "open %s" % ctl,
            "set t 15", "set x 593 834", "set y 251 492",
            "d tmax", "q dims",
        ])
        assert rc == 0, err
        cls.d = parse_displays(out)
        cls.out = out

    def test_region_window_stats(self):
        g = self.fields["ref_region_gp_tmax"]
        d = self.d["tmax"]
        self.assertEqual((d["nx"], d["ny"]), (242, 242))
        self.assertEqual(d["nmissing"], g["nundef"])
        self.assertTrue(close(d["min"], g["min"]), (d["min"], g["min"]))
        self.assertTrue(close(d["max"], g["max"]), (d["max"], g["max"]))
        self.assertTrue(close(d["mean"], g["mean"], tol=5e-2))

    def test_region_window_reported(self):
        self.assertIn("X: 593..834 of 1385", self.out)
        self.assertIn("Y: 251..492 of 596", self.out)

    def test_world_coord_window(self):
        # `set lon/lat` converts world coordinates to grid indices with a
        # nearest-grid rule (ties to the lower cell). Reference GrADS
        # instead snaps the window outward to enclose the request, so the
        # two agree on some bounds and differ by one cell on others:
        # lon -99.51..-90.49 -> x 605..822 here AND in reference;
        # lat 35.51..44.49  -> y 264..479 here vs 263..480 in reference.
        # Pinned exactly; see docs/testing/NCLIMGRID_TEST_SUITE.md.
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng([
            "open %s" % ctl,
            "set lon -99.51 -90.49", "set lat 35.51 44.49", "q dims",
        ])
        self.assertEqual(rc, 0, err)
        self.assertIn("X: 605..822 of 1385", out)
        self.assertIn("Y: 264..479 of 596", out)

    def test_city_points(self):
        ctl = os.path.join(CONV, "julall.ctl")
        cmds = ["open %s" % ctl]
        for city, r in self.cities.items():
            cmds += ["set x %d" % (int(r["x_0based"]) + 1),
                     "set y %d" % (int(r["y_0based"]) + 1),
                     "set t 1", "d tmax"]
        rc, out, err = run_ng(cmds)
        self.assertEqual(rc, 0, err)
        blocks = [m for m in STAT_RE.finditer(out) if m.group(1) == "tmax"]
        self.assertEqual(len(blocks), len(self.cities))
        for (city, r), m in zip(self.cities.items(), blocks):
            self.assertEqual((int(m.group(2)), int(m.group(3))), (1, 1))
            want = float(r["tmax_t1"])
            got = float(m.group(9))
            self.assertTrue(close(got, want, tol=2e-2),
                            (city, got, want))

    def test_ocean_points_are_all_missing(self):
        ctl = os.path.join(CONV, "julall.ctl")
        cmds = ["open %s" % ctl]
        for point, r in self.oceans.items():
            cmds += ["set x %d" % (int(r["x_0based"]) + 1),
                     "set y %d" % (int(r["y_0based"]) + 1),
                     "set t 15", "d tmax"]
        rc, out, err = run_ng(cmds)
        self.assertEqual(rc, 0, err)
        self.assertEqual(out.count("all 1 values missing"), len(self.oceans))

    def test_boundary_windows(self):
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng([
            "open %s" % ctl, "set t 1",
            "set x 1", "set y 1 596", "d tmax",      # western edge column
            "set x 1385", "set y 1 596", "d tmax",   # eastern edge column
            "set x 1 1385", "set y 596", "d tmax",   # northern edge row
        ])
        self.assertEqual(rc, 0, err)
        d = parse_displays(out)
        self.assertEqual((d["tmax"]["nx"], d["tmax"]["ny"]), (1385, 1))


class ExprTest(NeedsConv):
    @classmethod
    def setUpClass(cls):
        NeedsConv.setUpClass()
        cls.fields = load_golden("fields.json")
        cls.monthly = load_golden("monthly.json")
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng([
            "open %s" % ctl, "set t 15",
            "d tmax-tmin",
            "d (tmax+tmin)/2",
            "d ave(tmax,t=1,t=31)",
            "d min(tmax,t=1,t=31)",
            "d max(tmax,t=1,t=31)",
            "d ave(prcp,t=1,t=31)*31",
            "d sum(prcp,t=1,t=31)",
            "d tmax/2+1",
            "d abs(tmin)",
        ])
        assert rc == 0, err
        cls.d = parse_displays(out)

    def test_monthly_mean(self):
        g = self.fields["ref_tmax_julmean"]
        d = self.d["ave(tmax,t=1,t=31)"]
        self.assertEqual(d["nmissing"], g["nundef"])
        self.assertTrue(close(d["min"], g["min"]), (d["min"], g["min"]))
        self.assertTrue(close(d["max"], g["max"]), (d["max"], g["max"]))
        self.assertTrue(close(d["mean"], g["mean"], tol=5e-2))

    def test_monthly_min_max(self):
        for expr, golden in (("min(tmax,t=1,t=31)", "ref_tmax_julmin"),
                             ("max(tmax,t=1,t=31)", "ref_tmax_julmax")):
            g = self.fields[golden]
            d = self.d[expr]
            self.assertEqual(d["nmissing"], g["nundef"])
            self.assertTrue(close(d["min"], g["min"]), (expr, d["min"], g["min"]))
            self.assertTrue(close(d["max"], g["max"]), (expr, d["max"], g["max"]))

    def test_monthly_prcp_total(self):
        # Masked monthly total: all-missing stays missing (NG's sum and
        # ave both skip; the golden generator uses a masked nansum after
        # catching nansum's silent 0.0 at all-missing cells).
        g = self.monthly["prcp"]
        for expr in ("ave(prcp,t=1,t=31)*31", "sum(prcp,t=1,t=31)"):
            with self.subTest(expr=expr):
                d = self.d[expr]
                self.assertEqual(
                    d["nmissing"], self.fields["ref_prcp_julsum"]["nundef"])
                self.assertTrue(close(d["min"], g["min"]), (d["min"], g["min"]))
                self.assertTrue(close(d["max"], g["max"], tol=5e-1),
                                (d["max"], g["max"]))
                self.assertTrue(close(d["mean"], g["mean"], tol=5e-1),
                                (d["mean"], g["mean"]))

    def test_ng_sum_matches_reference(self):
        g = self.fields["ref_prcp_julsum"]
        d = self.d["sum(prcp,t=1,t=31)"]
        self.assertEqual(d["nmissing"], g["nundef"])
        self.assertTrue(close(d["min"], g["min"]), (d["min"], g["min"]))
        self.assertTrue(close(d["max"], g["max"]), (d["max"], g["max"]))
        self.assertTrue(close(d["mean"], g["mean"], tol=5e-1))

    def test_temp_range_matches_netcdf(self):
        g = self.fields["nc_trange_t15"]
        d = self.d["tmax-tmin"]
        self.assertGreaterEqual(d["min"], 0.0)
        self.assertEqual(d["nmissing"], g["nundef"])
        self.assertTrue(close(d["min"], g["min"]), (d["min"], g["min"]))
        self.assertTrue(close(d["max"], g["max"]), (d["max"], g["max"]))
        self.assertTrue(close(d["mean"], g["mean"], tol=5e-2))

    def test_midpoint_reconstruction(self):
        # The files are built so tavg == (tmax+tmin)/2 to packing rounding;
        # the suite pins that internal consistency, not a physical claim.
        g = self.fields["nc_tavgdiff_t15"]
        self.assertLess(abs(g["mean"]), 1e-3)
        self.assertLess(g["max"], 0.01)
        self.assertGreater(g["min"], -0.01)
        d = self.d["(tmax+tmin)/2"]
        self.assertEqual(d["nmissing"], g["nundef"])

    def test_affine_transform_exact(self):
        g = self.fields["nc_tmax_t15"]
        t = self.d["tmax/2+1"]
        self.assertEqual(t["nmissing"], g["nundef"])
        self.assertTrue(close(t["min"], g["min"] / 2 + 1), (t["min"], g["min"]))
        self.assertTrue(close(t["max"], g["max"] / 2 + 1), (t["max"], g["max"]))
        self.assertTrue(close(t["mean"], g["mean"] / 2 + 1, tol=5e-2))

    def test_abs_nonneg(self):
        d = self.d["abs(tmin)"]
        self.assertGreaterEqual(d["min"], 0.0)
        self.assertEqual(d["nmissing"], 355702)


class ErrorTest(NeedsConv):
    def test_unknown_var_names_it(self):
        # Regression: the unknown-name path once printed freed memory
        # instead of the name (use-after-free in the `d` dispatch).
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng(["open %s" % ctl, "d frobnicate"])
        self.assertIn('variable "frobnicate" is not defined', err)
        self.assertNotIn("Displaying", out)

    def test_sdfopen_future_message(self):
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng(["open %s" % ctl, "sdfopen foo.nc"])
        self.assertIn("not implemented yet", err)
        self.assertNotIn("Displaying", out)

    def test_set_time_out_of_range(self):
        # A rejected `set` must not move the selection (still T: 1).
        ctl = os.path.join(CONV, "julall.ctl")
        rc, out, err = run_ng(["open %s" % ctl, "set t 32", "q dims"])
        self.assertIn("t=32 out of range", err)
        self.assertIn("T: 1 of 31", out)


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
