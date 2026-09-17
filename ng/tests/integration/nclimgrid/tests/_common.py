"""Shared helpers for the nClimGrid-Daily CTest drivers (stdlib only)."""
import csv
import json
import os
import sys
import unittest


def parse_args():
    out = {}
    for a in sys.argv[1:]:
        if a.startswith("--") and "=" in a:
            k, v = a[2:].split("=", 1)
            out[k] = v
    return out


ARGS = parse_args()
SUITE = ARGS.get("suite", os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
CACHE = ARGS.get("cache", os.path.expanduser("~/.cache/grads-ng-test-data"))
NG_BIN = ARGS.get("ngbin", "")
PAYLOAD = os.path.join(CACHE, "nclimgrid-jul1962")
CONV = os.path.join(CACHE, "conv")
EXPECTED = os.path.join(SUITE, "expected")


def load_conf():
    conf = {}
    with open(os.path.join(SUITE, "fixture.conf")) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                conf[k] = v
    return conf


def load_golden(name):
    with open(os.path.join(EXPECTED, name)) as f:
        if name.endswith(".json"):
            return json.load(f)
        return list(csv.DictReader(f))


def try_import_netcdf():
    try:
        import netCDF4  # noqa: F401
        import numpy  # noqa: F401
        return True
    except ImportError:
        return False


class NeedsNetCDF(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not try_import_netcdf():
            raise unittest.SkipTest(
                "SKIP: python netCDF4/numpy unavailable "
                "(see docs/testing/NCLIMGRID_TEST_SUITE.md)")


class NeedsPayload(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        conf = load_conf()
        missing = []
        for key in ("NCLIMGRID_NC_TMAX", "NCLIMGRID_NC_TMIN",
                    "NCLIMGRID_NC_TAVG", "NCLIMGRID_NC_PRCP",
                    "NCLIMGRID_NC_NCDD"):
            name = conf[key].split(":")[0]
            if not os.path.isfile(os.path.join(PAYLOAD, name)):
                missing.append(name)
        if missing:
            raise unittest.SkipTest(
                "SKIP: external fixture unavailable (missing %s); "
                "run download-nclimgrid-test-data.sh" % ", ".join(missing))


class NeedsConv(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(os.path.join(CONV, "julall.ctl")):
            raise unittest.SkipTest(
                "SKIP: converted fixture unavailable (conv/julall.ctl); "
                "run download-nclimgrid-test-data.sh")
        if not NG_BIN or not os.path.isfile(NG_BIN):
            raise unittest.SkipTest("SKIP: grads-ng binary not provided")


def main():
    """unittest entry point that ignores our --key=value CTest arguments."""
    unittest.main(argv=[sys.argv[0]], verbosity=2)


def ref_grads():
    """Path to a reference OpenGrADS binary, or '' when unavailable."""
    cand = os.environ.get("REF_GRADS_BIN", "")
    if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
        return cand
    return ""
