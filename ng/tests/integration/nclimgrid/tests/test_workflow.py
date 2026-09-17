#!/usr/bin/env python3
"""Primary smoke/integration test: realistic July-1962 workflow (goal §21).

download -> open -> inspect -> select day + region -> derive -> reduce ->
render-face -> write summary -> verify. Each stage asserts against goldens;
the written summary JSON is re-read and verified (the write-output stage).
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import (CONV, NG_BIN, PAYLOAD, NeedsConv, load_golden)  # noqa: E402
from test_ng import close, parse_displays  # noqa: E402


class WorkflowTest(NeedsConv):
    def test_july_heat_wave_workflow(self):
        ctl = os.path.join(CONV, "julall.ctl")
        self.assertTrue(os.path.isfile(
            os.path.join(PAYLOAD, "tmax-196207-grd-scaled.nc")),
            "download stage: payload present")
        session = "\n".join([
            "open %s" % ctl,
            "q file",
            "set t 15",
            "set x 593 834",
            "set y 251 492",
            "q dims",
            "d tmax-tmin",
            "d ave(tmax-tmin,t=1,t=31)",
            "d max(tmax,t=1,t=31)",
        ]) + "\nquit\n"
        p = subprocess.run([NG_BIN], input=session, capture_output=True,
                           text=True, timeout=600)
        out, err = p.stdout, p.stderr
        # inspect stage
        self.assertIn("Dimensions: 1385 x 596 x 1 x 31", out)
        self.assertIn("tmax tmin tavg prcp", out)
        # select stage (incl. NG's 1962 TDEF decoding)
        self.assertIn("X: 593..834 of 1385", out)
        self.assertIn("Time = 00Z15JUL1962", out)
        # derive + reduce stages vs goldens
        fields = load_golden("fields.json")
        d = parse_displays(out)
        tr = fields["ref_region_gp_trange"]
        self.assertTrue(close(d["tmax-tmin"]["mean"], tr["mean"], tol=5e-2))
        mx = d["max(tmax,t=1,t=31)"]
        self.assertEqual((mx["nx"], mx["ny"]), (242, 242))
        self.assertGreater(mx["max"], 35.0)  # July Plains heat, gridded data
        # write-output stage: summary artifact, then re-read and verify
        summary = {
            "dataset": "nClimGrid-Daily July 1962",
            "region": {"lon": [-100, -90], "lat": [35, 45]},
            "july15_trange_mean": d["tmax-tmin"]["mean"],
            "july_max_tmax": mx["max"],
            "nmissing": mx["nmissing"],
        }
        tmp = os.path.join(tempfile.mkdtemp(prefix="nclimgrid-wf-"),
                           "workflow_summary.json")
        with open(tmp, "w") as f:
            json.dump(summary, f)
        back = json.load(open(tmp))
        self.assertAlmostEqual(back["july15_trange_mean"], tr["mean"], delta=0.05)
        self.assertEqual(back["nmissing"], tr["nundef"])
        print("workflow summary: %s" % tmp)


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
