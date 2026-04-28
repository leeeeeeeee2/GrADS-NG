#ifndef NG_CTL_H
#define NG_CTL_H

/*
 * ng/src/io/ctl.h
 * GrADS descriptor (.ctl) parser — 100% compatible with GrADS 2.2.1.
 *
 * The .ctl file is GrADS's "soul": it describes the binary layout,
 * dimension definitions, variable list, and template rules for data files.
 * Every parsing quirk of the original gaddes.c must be preserved here.
 */

#include <stddef.h>

/* Maximum sizes matching original GrADS limits */
#define NG_CTL_MAXVARS   2000
#define NG_CTL_MAXLEVS   500
#define NG_CTL_NAMELEN   64
#define NG_CTL_PATHLEN   4096

typedef enum {
    NG_DTYPE_BINARY  = 0,
    NG_DTYPE_NETCDF  = 1,
    NG_DTYPE_HDF4    = 2,
    NG_DTYPE_HDF5    = 3,
    NG_DTYPE_GRIB    = 4,
    NG_DTYPE_GRIB2   = 5,
    NG_DTYPE_BUFR    = 6,
} ng_dtype_t;

typedef struct {
    char     name[NG_CTL_NAMELEN];
    char     units[NG_CTL_NAMELEN];
    char     longname[256];
    int      nlevels;       /* 0 = surface variable */
    int      code;          /* GRIB parameter code */
} ng_var_t;

typedef struct {
    /* Grid dimensions */
    int      nx, ny, nz, nt, ne;   /* x, y, z, time, ensemble */

    /* Coordinate arrays (allocated, caller frees via ng_ctl_free) */
    double  *xvals;     /* longitude values or linear start/incr */
    double  *yvals;     /* latitude values */
    double  *zvals;     /* level values */

    /* Flags */
    int      xlinear;   /* 1 = xdef levels linear */
    int      ylinear;
    int      zlinear;

    /* Missing value */
    double   undef;

    /* Data file template path */
    char     dset[NG_CTL_PATHLEN];
    int      template;      /* 1 = filename contains %y4/%m2/etc. tokens */

    /* Data type */
    ng_dtype_t dtype;
    int      byteswap;
    int      sequential;    /* Fortran sequential (record markers) */

    /* Variables */
    int      nvars;
    ng_var_t vars[NG_CTL_MAXVARS];

    /* Title */
    char     title[256];
} ng_ctl_t;

/* Parse a .ctl file from path.  Returns allocated ng_ctl_t or NULL on error.
 * err_out receives a static error string (do not free). */
ng_ctl_t   *ng_ctl_parse(const char *path, const char **err_out);
void        ng_ctl_free(ng_ctl_t *ctl);

/* Expand a template path for a given time step (yr, mo, dy, hr).
 * Returns heap-allocated string; caller frees. */
char       *ng_ctl_expand_template(const ng_ctl_t *ctl,
                                   int yr, int mo, int dy, int hr);

#endif /* NG_CTL_H */
