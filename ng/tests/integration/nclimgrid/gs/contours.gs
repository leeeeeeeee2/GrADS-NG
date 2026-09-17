* contours.gs - rendering smoke (goal §18/§19).
* Headless batch GrADS has no display: `printim` against the dummy GX
* prints a message but writes no file. The driver accepts exactly two
* outcomes: a real PNG via printim when a display GX exists, or the
* contour computation ("Contouring: ...") plus the fwrite companion when
* headless. Either way the data path into the renderer is verified.
sdfopen __PAYLOAD__/tmax-196207-grd-scaled.nc
sdfopen __PAYLOAD__/tmin-196207-grd-scaled.nc
set t 15
set gxout fwrite
set fwrite contour_tmax.gr
d tmax.1
disable fwrite
set gxout shaded
d tmax.1
d tmin.2
printim contour_tmax.png
quit
