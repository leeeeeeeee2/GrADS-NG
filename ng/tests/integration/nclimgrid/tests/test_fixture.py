#!/usr/bin/env python3
"""Fixture validation: discovery, extraction, integrity (goal §4).

Fails clearly on corrupt/incomplete state; SKIPs (via CTest) only when the
fixture was never acquired.
"""
import hashlib
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import CACHE, PAYLOAD, SUITE, load_conf  # noqa: E402


class FixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.conf = load_conf()
        cls.archive = os.path.join(CACHE, cls.conf["NCLIMGRID_ARCHIVE_NAME"])
        if not os.path.isfile(cls.archive):
            raise unittest.SkipTest(
                "SKIP: archive not cached (run "
                "download-nclimgrid-test-data.sh)")

    def test_archive_size(self):
        want = int(self.conf["NCLIMGRID_ARCHIVE_SIZE"])
        got = os.path.getsize(self.archive)
        self.assertEqual(got, want,
                         "archive size %d != %d: incomplete download?" % (got, want))

    def test_archive_sha256(self):
        h = hashlib.sha256()
        with open(self.archive, "rb") as f:
            for blk in iter(lambda: f.read(1 << 20), b""):
                h.update(blk)
        self.assertEqual(h.hexdigest(), self.conf["NCLIMGRID_ARCHIVE_SHA256"],
                         "archive sha256 mismatch: corrupt download")

    def test_archive_is_gzip_tar(self):
        with open(self.archive, "rb") as f:
            magic = f.read(2)
        self.assertEqual(magic, b"\x1f\x8b", "archive is not gzip")

    def test_payload_files(self):
        for key in ("NCLIMGRID_NC_TMAX", "NCLIMGRID_NC_TMIN",
                    "NCLIMGRID_NC_TAVG", "NCLIMGRID_NC_PRCP",
                    "NCLIMGRID_NC_NCDD", "NCLIMGRID_VERSION_TXT"):
            name, size = self.conf[key].split(":")
            path = os.path.join(PAYLOAD, name)
            self.assertTrue(os.path.isfile(path), "extracted file missing: " + name)
            self.assertEqual(os.path.getsize(path), int(size),
                             "size mismatch for %s: re-extract?" % name)

    def test_netcdf_magic(self):
        for key in ("NCLIMGRID_NC_TMAX", "NCLIMGRID_NC_TMIN",
                    "NCLIMGRID_NC_TAVG", "NCLIMGRID_NC_PRCP",
                    "NCLIMGRID_NC_NCDD"):
            name = self.conf[key].split(":")[0]
            with open(os.path.join(PAYLOAD, name), "rb") as f:
                magic = f.read(4)
            self.assertEqual(magic, b"\x89HDF",
                             "%s is not NetCDF-4 (bad magic)" % name)

    def test_version_txt(self):
        name = self.conf["NCLIMGRID_VERSION_TXT"].split(":")[0]
        with open(os.path.join(PAYLOAD, name)) as f:
            text = f.read()
        self.assertIn("1962-07-01", text)
        self.assertIn("1962-07-31", text)

    def test_conv_manifest(self):
        man_path = os.path.join(CACHE, "conv", "manifest.json")
        if not os.path.isfile(man_path):
            raise unittest.SkipTest(
                "SKIP: converted fixture not built (needs python netCDF4)")
        man = json.load(open(man_path))
        dat = os.path.join(CACHE, "conv", "julall.dat")
        self.assertTrue(os.path.isfile(dat))
        self.assertEqual(os.path.getsize(dat), man["julall_dat_size"])
        for var, rec in man["sources"].items():
            src = os.path.join(PAYLOAD, "%s-196207-grd-scaled.nc" % var)
            self.assertEqual(os.path.getsize(src), rec["size"],
                             "source changed since conversion: %s" % var)


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
