#!/usr/bin/env python3
"""Reference arm: run the .gs suite under reference OpenGrADS and check the
reference fwrite dumps against goldens (goal §6, §17, §20).

SKIPs when no reference grads is available (REF_GRADS_BIN) or the payload
dumps are absent. Regenerates dumps/gs/dumps.gs outputs into the payload
when a reference binary exists but dumps are missing.
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import (PAYLOAD, SUITE, NeedsPayload, load_golden,  # noqa: E402
                     ref_grads)

import numpy as np

GS = ("open", "metadata", "time", "spatial", "expressions", "statistics",
      "missing_values", "complete_workflow")
BAD_PATTERNS = ("Error occurred", "Unknown command", "Syntax error",
                "Data Request Error", "ga-> Error")


def log_errors(log):
    return [ln for ln in log.splitlines()
            if any(p in ln for p in BAD_PATTERNS)]


def run_gs(name, outdir):
    script = os.path.join(SUITE, "gs", name + ".gs")
    sub = os.path.join(outdir, name + ".sub.gs")
    with open(script) as f:
        lines = [ln for ln in f.read().replace("__PAYLOAD__", PAYLOAD)
                 .splitlines(keepends=True) if not ln.startswith("*")]
    with open(sub, "w") as f:
        f.writelines(lines)
    log = os.path.join(outdir, name + ".log")
    env = dict(os.environ)
    refbin = ref_grads()
    env["LD_LIBRARY_PATH"] = os.path.join(os.path.dirname(refbin), "gex") \
        + ":" + env.get("LD_LIBRARY_PATH", "")
    if not env.get("GAUDPT"):
        gex = os.path.join(os.path.dirname(refbin), "gex")
        dummy = None
        for cand in (os.path.join(gex, "libgxdummy.so"),
                     os.path.join(os.path.dirname(refbin), "..", "Contents",
                                  "Linux", "Versions", "2.2.1.oga.1", "x86_64",
                                  "gex", "libgxdummy.so")):
            if os.path.isfile(cand):
                dummy = cand
                break
        if dummy:
            udpt = os.path.join(outdir, "udpt")
            with open(udpt, "w") as f:
                f.write("gxdisplay  gxdummy  %s\n" % dummy)
                f.write("gxdisplay  Cairo    %s\n" % dummy)
                f.write("*\n")
                f.write("gxprint    gxdummy  %s\n" % dummy)
                f.write("gxprint    Cairo    %s\n" % dummy)
            env["GAUDPT"] = udpt
    with open(sub) as stdin, open(log, "w") as stdout:
        subprocess.run([refbin, "-bp"], stdin=stdin, stdout=stdout,
                       stderr=subprocess.STDOUT, cwd=outdir, env=env,
                       timeout=900)
    with open(log, errors="replace") as f:
        return f.read()


def fread(path):
    with open(path, "rb") as f:
        raw = f.read()
    return np.array(struct.unpack("<" + "f" * (len(raw) // 4), raw),
                    dtype="float64")


class NeedsReference(NeedsPayload):
    @classmethod
    def setUpClass(cls):
        NeedsPayload.setUpClass()
        if not ref_grads():
            raise unittest.SkipTest(
                "SKIP: no reference OpenGrADS (set REF_GRADS_BIN)")


class GsSuiteTest(NeedsReference):
    @classmethod
    def setUpClass(cls):
        NeedsReference.setUpClass()
        cls.outdir = tempfile.mkdtemp(prefix="nclimgrid-gs-")
        cls.logs = {}
        for name in GS:
            cls.logs[name] = run_gs(name, cls.outdir)

    # Log content each script must produce (proves it ran end to end).
    MARKERS = {
        "open": "is open as file 5",
        "metadata": "Xsize = 1385  Ysize = 596  Zsize = 1  Tsize = 31",
        "time": "Time = 00Z31JUL1962  T = 31",
        "spatial": "Lat = 42 to 49",
        "expressions": "LON set to -100 -90",
        "statistics": "SDF file",
        "missing_values": "LAT set to 26.0208 26.0208",
        "complete_workflow": "workflow_trange_mean",
    }

    def test_scripts_run_clean(self):
        for name in GS:
            with self.subTest(script=name):
                log = self.logs[name]
                bad = log_errors(log)
                self.assertEqual(bad, [], "errors in %s.gs:\n%s" % (name, log[-2000:]))
                self.assertIn(self.MARKERS[name], log)

    def test_expression_outputs(self):
        d = fread(os.path.join(self.outdir, "expr_trange.gr")).reshape(242, 242)
        g = load_golden("fields.json")["ref_region_gp_trange"]
        good = d > -1e8
        self.assertEqual(int((~good).sum()), g["nundef"])
        self.assertAlmostEqual(float(d[good].min()), g["min"], places=2)
        self.assertAlmostEqual(float(d[good].max()), g["max"], places=2)

    def test_statistic_outputs(self):
        fields = load_golden("fields.json")
        for stem, golden in (("stat_tmax_mean", "ref_tmax_julmean"),
                             ("stat_tmax_min", "ref_tmax_julmin"),
                             ("stat_tmax_max", "ref_tmax_julmax"),
                             ("stat_prcp_sum", "ref_prcp_julsum")):
            with self.subTest(dump=stem):
                d = fread(os.path.join(self.outdir, stem + ".gr"))
                g = fields[golden]
                self.assertEqual(d.size, g["n"] // 1)
                good = d > -1e8
                self.assertEqual(int((~good).sum()), g["nundef"])
                self.assertAlmostEqual(float(d[good].mean()), g["mean"], places=2)

    def test_missing_stays_missing(self):
        for stem in ("miss_atl", "miss_pac", "miss_gulf"):
            with self.subTest(point=stem):
                d = fread(os.path.join(self.outdir, stem + ".gr"))
                self.assertEqual(d.size, 6)
                self.assertTrue((d <= -1e8).all(), d)

    def test_workflow_outputs(self):
        d = fread(os.path.join(self.outdir, "workflow_trange.gr"))
        self.assertEqual(d.size, 242 * 242)
        m = fread(os.path.join(self.outdir, "workflow_trange_mean.gr"))
        self.assertEqual(m.size, 242 * 242)
        self.assertTrue((m[m > -1e8] >= 0).all())


class ContoursTest(NeedsReference):
    def test_render_or_documented_headless_failure(self):
        outdir = tempfile.mkdtemp(prefix="nclimgrid-contour-")
        log = run_gs("contours", outdir)
        png = os.path.join(outdir, "contour_tmax.png")
        if os.path.isfile(png) and os.path.getsize(png) > 1000:
            print("reference rendering produced %s" % png)
            return
        # Headless: the dummy GX computes contour levels ("Contouring:")
        # but writes no pixels. The fwrite companion proves the exact
        # field the renderer consumed. Two contour passes = overlay.
        self.assertEqual(log.count("Contouring:"), 2, log[-1500:])
        d = fread(os.path.join(outdir, "contour_tmax.gr"))
        self.assertEqual(d.size, 1385 * 596)
        good = d > -1e8
        self.assertEqual(int((~good).sum()), 355702)
        print("SKIP-IMAGE: headless GX (levels computed, no pixels); "
              "render input verified (%d values)" % d.size)


class DumpsTest(NeedsReference):
    def test_payload_dumps_match_goldens(self):
        # gs/dumps.gs outputs live in the payload (regenerate when missing).
        missing = [n for n in ("ref_tmax_t01.gr", "ref_city_nyc.gr",
                               "ref_ocean_atl.gr")
                   if not os.path.isfile(os.path.join(PAYLOAD, n))]
        if missing:
            run_gs("dumps", PAYLOAD)
        fields = load_golden("fields.json")
        for name, g in fields.items():
            if g.get("source", "reference-fwrite") != "reference-fwrite":
                continue
            with self.subTest(dump=name):
                d = fread(os.path.join(PAYLOAD, name + ".gr"))
                self.assertEqual(d.size * 4, g["size"])
                good = d > -1e8
                self.assertEqual(int((~good).sum()), g["nundef"])
                if good.any():
                    self.assertAlmostEqual(float(d[good].min()), g["min"], places=2)
                    self.assertAlmostEqual(float(d[good].max()), g["max"], places=2)
                    self.assertAlmostEqual(float(d[good].mean()), g["mean"], places=2)
        oceans = {r["point"]: r for r in load_golden("oceans.csv")}
        for oc in ("atl", "pac", "gulf"):
            d = fread(os.path.join(PAYLOAD, "ref_ocean_%s.gr" % oc))
            self.assertEqual(d.size, 6)
            self.assertTrue((d <= -1e8).all())
        cities = {r["city"]: r for r in load_golden("cities.csv")}
        order = [(v, t) for t in (1, 15, 31)
                 for v in ("tmax", "tmin", "tavg", "prcp")]
        for city, r in cities.items():
            with self.subTest(city=city):
                d = fread(os.path.join(PAYLOAD, "ref_city_%s.gr" % city))
                self.assertEqual(d.size, 12)
                for k, (var, day) in enumerate(order):
                    self.assertAlmostEqual(float(d[k]),
                                           float(r["%s_t%d" % (var, day)]),
                                           places=2)


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
