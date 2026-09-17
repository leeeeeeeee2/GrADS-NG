#!/usr/bin/env python3
"""Performance baseline: wall times for each pipeline stage (goal §22).

Records (not optimizes): startup, open, first read, full-month reduction,
subset, expression. Bounds are generous anti-hang guards, not targets.
"""
import os
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import (CONV, NG_BIN, PAYLOAD, NeedsConv,  # noqa: E402
                     try_import_netcdf)


def time_ng(cmds):
    start = time.perf_counter()
    p = subprocess.run([NG_BIN], input="\n".join(cmds) + "\nquit\n",
                       capture_output=True, text=True, timeout=900)
    return time.perf_counter() - start, p.stdout


class PerfTest(NeedsConv):
    STAGES = {}

    @classmethod
    def setUpClass(cls):
        NeedsConv.setUpClass()
        cls.ctl = os.path.join(CONV, "julall.ctl")

    def run_stage(self, name, cmds, bound):
        dt, out = time_ng(cmds)
        type(self).STAGES[name] = round(dt, 3)
        self.assertIn("Displaying", out, name)
        self.assertLess(dt, bound, "%s took %.1fs" % (name, dt))

    def test_stages(self):
        self.run_stage("open+q", ["open %s" % self.ctl, "q file", "d tmax"], 120)
        self.run_stage("first_read_t15",
                       ["open %s" % self.ctl, "set t 15", "d tmax"], 120)
        self.run_stage("monthly_reduction",
                       ["open %s" % self.ctl, "d ave(tmax,t=1,t=31)"], 300)
        self.run_stage("subset_expr",
                       ["open %s" % self.ctl, "set t 15",
                        "set x 593 834", "set y 251 492",
                        "d tmax-tmin"], 120)
        print("perf stages (s): %s" % type(self).STAGES)
        print("data volume: 4 vars x 31 days x 596 x 1385 float32 = %.1f MB"
              % (4 * 31 * 596 * 1385 * 4 / 1e6))

    def test_netcdf_full_read_baseline(self):
        if not try_import_netcdf():
            raise unittest.SkipTest("SKIP: netCDF4 unavailable for baseline")
        import netCDF4
        import numpy as np
        start = time.perf_counter()
        ds = netCDF4.Dataset(os.path.join(PAYLOAD, "tmax-196207-grd-scaled.nc"))
        a = np.asarray(ds.variables["tmax"][:])
        ds.close()
        dt = time.perf_counter() - start
        self.assertEqual(a.shape, (31, 596, 1385))
        print("netcdf full-month tmax read: %.1fs (%d MB)" % (dt, a.nbytes // 10**6))


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
