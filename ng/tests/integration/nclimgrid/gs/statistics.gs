* statistics.gs - monthly reductions over the full grid (goal §13).
* Full-field fwrite; the driver compares stats with the goldens.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
sdfopen __PAYLOAD__/prcp-196207-grd-scaled.nc
set t 1
set gxout fwrite
set fwrite stat_tmax_mean.gr
d ave(tmax.1,t=1,t=31)
disable fwrite
set fwrite stat_tmax_min.gr
d min(tmax.1,t=1,t=31)
disable fwrite
set fwrite stat_tmax_max.gr
d max(tmax.1,t=1,t=31)
disable fwrite
set fwrite stat_prcp_sum.gr
d sum(prcp.2,t=1,t=31)
disable fwrite
quit
