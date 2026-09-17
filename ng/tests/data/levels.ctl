DSET ^run/%y4%m2%d2%h2.dat
TITLE Levels/options/ensemble fixture
UNDEF -999.0
* wrapped comment lines are skipped
XDEF 3 LEVELS 10.5 20.0 30.25
YDEF 2 LINEAR -45.0 90.0
ZDEF 3 LEVELS 1000 850 500
TDEF 4 LINEAR 12Z01JAN2000 6HR
EDEF 2 NAMES memA memB
DTYPE netcdf
OPTIONS byteswapped sequential template
VARS 3
ps   0 99 Surface Pressure
tair 3 11 Air Temperature (K)
q    2 51 Specific Humidity
ENDVARS
