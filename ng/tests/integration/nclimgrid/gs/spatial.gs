* spatial.gs - regional windows across CONUS (goal §10).
* Northeast / Southeast / Midwest / Great Plains / Mountain West /
* Southwest / California / Pacific Northwest + full reset.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
set t 15
set lon -80 -70
set lat 35 45
q dims
set lon -90 -80
set lat 28 36
q dims
set lon -95 -85
set lat 40 48
q dims
set lon -105 -95
set lat 32 42
q dims
set lon -115 -105
set lat 36 46
q dims
set lon -122 -112
set lat 30 40
q dims
set lon -124 -114
set lat 32 42
q dims
set lon -125 -117
set lat 42 49
q dims
set lon -124.6875 -67.0208
set lat 24.5625 49.3542
q dims
quit
