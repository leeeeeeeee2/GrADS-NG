* complete_workflow.gs - end-to-end smoke (goal §21):
* open -> inspect -> select July day + region -> derive -> reduce -> write.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
sdfopen __PAYLOAD__/tmin-196207-grd-scaled.nc
q file
q dims
set t 15
set lon -100 -90
set lat 35 45
q dims
set gxout fwrite
set fwrite workflow_trange.gr
d tmax.1-tmin.2
disable fwrite
set fwrite workflow_trange_mean.gr
d ave(tmax.1-tmin.2,t=1,t=31)
disable fwrite
quit
