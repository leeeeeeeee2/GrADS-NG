* dumps.gs - reference data extraction for the nClimGrid-Daily suite.
* Run with cwd set to the extracted fixture dir; writes *.gr fwrite files
* plus this log into the current directory.
* Files open as: 1=tmax 2=tmin 3=tavg 4=prcp (all July 1962, 1385x596x31).
*
* Conventions recorded here (also asserted by the suite):
* - 'set t N' selects July N (t=1 -> 1962-07-01, t=31 -> 1962-07-31).
* - city files hold 12 float32 values: t in (1,15,31), vars (tmax,tmin,tavg,prcp).
* - ocean files hold 6 float32 values at fixed t=15:
*     tmax, tmax+1, tmax*2, tmax/2, ave(tmax,t=1,t=31), tmax-tmin.

sdfopen tmax-196207-grd-scaled.nc
sdfopen tmin-196207-grd-scaled.nc
sdfopen tavg-196207-grd-scaled.nc
sdfopen prcp-196207-grd-scaled.nc
q file
q dims

set gxout fwrite

* --- full fields (1385x596 float32) ---
set t 1
set fwrite ref_tmax_t01.gr
d tmax.1
disable fwrite

set t 31
set fwrite ref_tmax_t31.gr
d tmax.1
disable fwrite

set t 1
set fwrite ref_tmax_julmean.gr
d ave(tmax.1,t=1,t=31)
disable fwrite

set t 1
set fwrite ref_tmax_julmin.gr
d min(tmax.1,t=1,t=31)
disable fwrite

set t 1
set fwrite ref_tmax_julmax.gr
d max(tmax.1,t=1,t=31)
disable fwrite

set t 15
set fwrite ref_prcp_t15.gr
d prcp.4
disable fwrite

set t 1
set fwrite ref_prcp_julsum.gr
d sum(prcp.4,t=1,t=31)
disable fwrite

* --- Great Plains subset lon -100..-90 lat 35..45 at t=15 ---
set lon -100 -90
set lat 35 45
set t 15
set fwrite ref_region_gp_tmax.gr
d tmax.1
disable fwrite
set fwrite ref_region_gp_trange.gr
d tmax.1-tmin.2
disable fwrite
set lon -124.6875 -67.0208
set lat 24.5625 49.3542

* --- cities: NYC CHI MIA DEN LAX SEA ---
set lat 40.7
set lon -74.0
set fwrite ref_city_nyc.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

* NOTE: downtown Chicago (-87.6) is Lake Michigan water in the land mask;
* the CHI land point is set slightly west (-87.65). The lake cell is dumped
* separately as ref_city_chi_lake.gr (all-undef expected).
set lat 41.9
set lon -87.65
set fwrite ref_city_chi.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

set lat 25.8
set lon -80.2
set fwrite ref_city_mia.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

set lat 39.7
set lon -105.0
set fwrite ref_city_den.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

set lat 34.1
set lon -118.2
set fwrite ref_city_lax.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

set lat 47.6
set lon -122.3
set fwrite ref_city_sea.gr
set t 1
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 15
d tmax.1
d tmin.2
d tavg.3
d prcp.4
set t 31
d tmax.1
d tmin.2
d tavg.3
d prcp.4
disable fwrite

set lat 41.9
set lon -87.6
set fwrite ref_city_chi_lake.gr
set t 15
d tmax.1
d prcp.4
disable fwrite

* --- inside-grid missing points: Atlantic, Pacific, Gulf ---
set t 15
set lat 32.0
set lon -68.0
set fwrite ref_ocean_atl.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite

set lat 30.0
set lon -124.0
set fwrite ref_ocean_pac.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite

set lat 26.0
set lon -90.0
set fwrite ref_ocean_gulf.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite

quit
