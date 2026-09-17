* missing_values.gs - inside-grid missing points (goal §14).
* Atlantic/Pacific/Gulf water cells: raw, arithmetic, and reductions must
* all come back UNDEF. Writes one 6-value file per point.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
sdfopen __PAYLOAD__/tmin-196207-grd-scaled.nc
set t 15
set gxout fwrite
set lat 32.0
set lon -68.0
set fwrite miss_atl.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite
set lat 30.0
set lon -124.0
set fwrite miss_pac.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite
set lat 26.0
set lon -90.0
set fwrite miss_gulf.gr
d tmax.1
d tmax.1+1
d tmax.1*2
d tmax.1/2
d ave(tmax.1,t=1,t=31)
d tmax.1-tmin.2
disable fwrite
quit
