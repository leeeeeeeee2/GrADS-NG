#!/usr/bin/env python3
"""Direct-NetCDF arm: schema, coords, time, vars, units, missing (goal §5-11,
§14-16). Compares fresh reads against committed goldens in expected/.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _common import (PAYLOAD, NeedsNetCDF, NeedsPayload,  # noqa: E402
                     load_conf, load_golden)

import netCDF4
import numpy as np


class SchemaTest(NeedsNetCDF, NeedsPayload):
    @classmethod
    def setUpClass(cls):
        NeedsNetCDF.setUpClass()
        NeedsPayload.setUpClass()
        cls.conf = load_conf()
        cls.gold = load_golden("schema.json")
        cls.ds = netCDF4.Dataset(
            os.path.join(PAYLOAD, "tmax-196207-grd-scaled.nc"))

    @classmethod
    def tearDownClass(cls):
        cls.ds.close()

    def test_dims(self):
        for dim, rec in self.gold["dims"].items():
            self.assertIn(dim, self.ds.dimensions)
            self.assertEqual(len(self.ds.dimensions[dim]), rec["len"])
            self.assertEqual(self.ds.dimensions[dim].isunlimited(),
                             rec["unlimited"])

    def test_title(self):
        self.assertEqual(self.ds.getncattr("title"), self.gold["title"])

    def test_time_encoding_read_from_metadata(self):
        t = self.ds.variables["time"]
        g = self.gold["time"]
        self.assertEqual(t.units, g["units"])
        self.assertEqual(t.getncattr("calendar"), g["calendar"])
        self.assertEqual(np.asarray(t[:]).tolist(), g["values"])
        dates = [d.strftime("%Y-%m-%d") for d in
                 netCDF4.num2date(np.asarray(t[:]), t.units,
                                  calendar=t.getncattr("calendar"))]
        self.assertEqual(dates, g["dates"])
        self.assertEqual(dates[0], "1962-07-01")
        self.assertEqual(dates[-1], "1962-07-31")
        self.assertEqual(len(dates), 31)

    def test_time_steps_daily(self):
        t = self.ds.variables["time"]
        vals = np.asarray(t[:])
        self.assertTrue((np.diff(vals) == 1).all(), "time steps are not 1 day")

    def test_key_dates(self):
        t = self.ds.variables["time"]
        dates = netCDF4.num2date(np.asarray(t[[0, 1, 14, 30]]), t.units,
                                 calendar=t.getncattr("calendar"))
        got = [d.strftime("%Y-%m-%d") for d in dates]
        self.assertEqual(got, ["1962-07-01", "1962-07-02",
                               "1962-07-15", "1962-07-31"])


class CoordTest(NeedsNetCDF, NeedsPayload):
    @classmethod
    def setUpClass(cls):
        NeedsNetCDF.setUpClass()
        NeedsPayload.setUpClass()
        cls.gold = load_golden("schema.json")["coords"]
        ds = netCDF4.Dataset(os.path.join(PAYLOAD, "tmax-196207-grd-scaled.nc"))
        cls.lon = np.asarray(ds.variables["lon"][:]).astype("float64")
        cls.lat = np.asarray(ds.variables["lat"][:]).astype("float64")
        cls.lon_units = ds.variables["lon"].getncattr("units")
        cls.lat_units = ds.variables["lat"].getncattr("units")
        ds.close()
        cls.exp_lon = np.loadtxt(os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "expected", "coords_lon.csv"))
        cls.exp_lat = np.loadtxt(os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "expected", "coords_lat.csv"))

    def test_lon(self):
        g = self.gold["lon"]
        self.assertEqual(len(self.lon), g["n"])
        self.assertAlmostEqual(self.lon.min(), g["min"], places=4)
        self.assertAlmostEqual(self.lon.max(), g["max"], places=4)
        self.assertTrue((np.diff(self.lon) > 0).all(), "lon not ascending")
        self.assertEqual(self.lon_units, g["units"])
        np.testing.assert_allclose(self.lon, self.exp_lon, rtol=0, atol=0)

    def test_lat(self):
        g = self.gold["lat"]
        self.assertEqual(len(self.lat), g["n"])
        self.assertAlmostEqual(self.lat.min(), g["min"], places=4)
        self.assertAlmostEqual(self.lat.max(), g["max"], places=4)
        self.assertTrue((np.diff(self.lat) > 0).all(), "lat not ascending")
        self.assertEqual(self.lat_units, g["units"])
        np.testing.assert_allclose(self.lat, self.exp_lat, rtol=0, atol=0)

    def test_resolution_is_1_24_degree(self):
        for arr in (self.lon, self.lat):
            step = (arr[-1] - arr[0]) / (len(arr) - 1)
            self.assertAlmostEqual(step, 1.0 / 24.0, places=4)


class VarTest(NeedsNetCDF, NeedsPayload):
    FILES = (("tmax-196207-grd-scaled.nc", "tmax"),
             ("tmin-196207-grd-scaled.nc", "tmin"),
             ("tavg-196207-grd-scaled.nc", "tavg"),
             ("prcp-196207-grd-scaled.nc", "prcp"))

    @classmethod
    def setUpClass(cls):
        NeedsNetCDF.setUpClass()
        NeedsPayload.setUpClass()
        cls.gold = load_golden("schema.json")["files"]

    def test_each_var(self):
        for fn, var in self.FILES:
            with self.subTest(var=var):
                ds = netCDF4.Dataset(os.path.join(PAYLOAD, fn))
                try:
                    self.assertIn(var, ds.variables)
                    v = ds.variables[var]
                    g = self.gold[fn]["vars"][var]
                    self.assertEqual(str(v.dtype), g["dtype"])
                    self.assertEqual(list(v.dimensions), g["dims"])
                    self.assertEqual(list(v.shape), g["shape"])
                    self.assertEqual(v.getncattr("units"), g["units"])
                    self.assertEqual(v.getncattr("long_name"), g["long_name"])
                    self.assertEqual(v.getncattr("standard_name"),
                                     g["standard_name"])
                    self.assertTrue(np.isnan(float(v.getncattr("_FillValue"))))
                    self.assertNotIn("missing_value", v.ncattrs())
                    self.assertNotIn("scale_factor", v.ncattrs())
                    self.assertNotIn("add_offset", v.ncattrs())
                finally:
                    ds.close()

    def test_no_vector_fields(self):
        # The dataset carries no vector (U/V pair) grids, so vector
        # overlays are not applicable; the suite pins that, not pixels.
        ds = netCDF4.Dataset(
            os.path.join(PAYLOAD, "ncdd-196207-grd-scaled.nc"))
        try:
            got = {n for n, v in ds.variables.items() if v.ndim == 3}
            self.assertEqual(got, {"tmax", "tmin", "tavg", "prcp"})
        finally:
            ds.close()

    def test_valid_ranges_sensible(self):
        files = self.gold
        for fn, var in self.FILES:
            with self.subTest(var=var):
                valid = files[fn]["vars"][var]["valid"]
                if var == "prcp":
                    self.assertEqual(valid, [0.0, 2000.0])
                else:
                    self.assertEqual(valid, [-100.0, 100.0])

    def test_data_readable_and_masked(self):
        # Static land mask: identical missing count every day, 43% of cells.
        ds = netCDF4.Dataset(
            os.path.join(PAYLOAD, "tmax-196207-grd-scaled.nc"))
        try:
            v = ds.variables["tmax"]
            day0 = np.asarray(v[0]).astype("float64")
            day30 = np.asarray(v[30]).astype("float64")
            m0 = np.isnan(day0)
            self.assertEqual(int(m0.sum()), 355702)
            np.testing.assert_array_equal(m0, np.isnan(day30))
            self.assertLessEqual(np.nanmin(day0), 0.0)   # high terrain
            self.assertGreater(np.nanmax(day0), 40.0)    # July heat
        finally:
            ds.close()


if __name__ == "__main__":
    from _common import main as _cmain
    _cmain()
