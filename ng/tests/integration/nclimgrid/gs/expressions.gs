* expressions.gs - derived fields over the Great Plains window (goal §12).
* Writes one small fwrite file; the driver compares it with the golden.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
sdfopen __PAYLOAD__/tmin-196207-grd-scaled.nc
sdfopen __PAYLOAD__/tavg-196207-grd-scaled.nc
set lon -100 -90
set lat 35 45
set t 15
set gxout fwrite
set fwrite expr_trange.gr
d tmax.1-tmin.2
disable fwrite
set fwrite expr_tavgdiff.gr
d tavg.3-(tmax.1+tmin.2)/2
disable fwrite
quit
