/*
 * GrADS-NG Core Implementation
 * 
 * Platform abstraction layer for cross-platform compatibility.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include "grads_ng.h"
#include "backend/platform.h"
#include "../io/ctl.h"
#include "../io/grid.h"
#include "ng_time.h"

/* Session structure */
struct grads_ng_session {
    grads_ng_config_t config;
    grads_ng_file_t* files[16];
    int num_files;
    void* python_state;
    char last_err[512];     /* reason the most recent operation failed */
};

/* File structure */
struct grads_ng_file {
    grads_ng_session_t* session;
    char ctl_path[512];
    ng_ctl_t* desc;         /* owned descriptor */
    ng_grid_t* grid;        /* data file, opened lazily (NULL until read) */
    int type;               /* 0=grid, 1=station, 2=bufr */
    int nx, ny, nz, nt, ne;
    int vnum;
    grads_ng_var_t* vars;   /* owned variable handles */
    int sel_t;              /* selected time step, 0-based (`set t`) */
    int sel_e;              /* selected ensemble, 0-based (`set e`) */
    ng_time_t tax_start;    /* TDEF start (absolute time axis) */
    int tax_count;          /* TDEF increment count (>= 1) */
    ng_time_unit_t tax_unit;
    int tax_ok;             /* time axis parsed */
    int sel_z;              /* selected level index, 0-based (`set z`) */
    int sel_x1, sel_x2;     /* X window, 0-based inclusive (`set x`) */
    int sel_y1, sel_y2;     /* Y window, 0-based inclusive (`set y`) */
};

/* Variable structure (borrowed from the file; valid until close) */
struct grads_ng_var {
    grads_ng_file_t* file;
    int index;              /* index into desc->vars */
    double undef;
};

/* ASCII case-insensitive comparison (portable across MSVC/POSIX). */
static int var_name_eq(const char* a, const char* b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Canvas structure */
struct grads_ng_canvas {
    int width;
    int height;
    rgba_t* pixels;         /* RGBA buffer */
    double* zbuffer;
    int linewidth;
    int linecolor;
    int fillcolor;
};

/* Initialize GrADS-NG session */
grads_ng_session_t* grads_ng_init(grads_ng_config_t* config) {
    grads_ng_session_t* session;
    
    session = calloc(1, sizeof(grads_ng_session_t));
    if (!session) {
        return NULL;
    }
    
    /* Apply configuration */
    if (config) {
        memcpy(&session->config, config, sizeof(grads_ng_config_t));
    } else {
        /* Default config: headless */
        session->config.headless = true;
        session->config.batch_mode = 1;
    }
    
    /* Initialize platform layer */
    platform_init();
    
    return session;
}

/* Destroy GrADS-NG session */
void grads_ng_destroy(grads_ng_session_t* session) {
    if (!session) return;
    
    /* Close all open files (close mutates the list, so always take slot 0). */
    while (session->num_files > 0 && session->files[0]) {
        grads_ng_close(session->files[0]);
    }
    
    /* Cleanup platform layer */
    platform_cleanup();
    
    free(session);
}

/* Open a GrADS descriptor file.
 * The descriptor parses eagerly (a malformed .ctl fails the open); the data
 * file itself opens lazily on the first variable read so that `open` + `q`
 * work without touching the data file. */
grads_ng_file_t* grads_ng_open(grads_ng_session_t* session, const char* ctl_path) {
    grads_ng_file_t* file;
    FILE* probe;
    char resolved[1024];
    const char* perr = NULL;
    ng_ctl_t* desc;
    int i;

    if (!session || !ctl_path) return NULL;

    /* Try the path as given, then with a .ctl suffix. */
    snprintf(resolved, sizeof(resolved), "%s", ctl_path);
    probe = fopen(resolved, "r");
    if (!probe) {
        platform_join_path(ctl_path, resolved, ".ctl");
        probe = fopen(resolved, "r");
    }
    if (!probe) {
        snprintf(session->last_err, sizeof(session->last_err),
                 "no such descriptor file \"%s\"", ctl_path);
        return NULL;
    }
    fclose(probe);

    desc = ng_ctl_parse(resolved, &perr);
    if (!desc) {
        snprintf(session->last_err, sizeof(session->last_err),
                 "could not parse descriptor \"%.100s\": %.300s", resolved,
                 perr ? perr : "unknown error");
        return NULL;
    }
    session->last_err[0] = '\0';

    file = calloc(1, sizeof(grads_ng_file_t));
    if (!file) {
        ng_ctl_free(desc);
        return NULL;
    }
    file->vars = calloc((size_t)desc->nvars, sizeof(grads_ng_var_t));
    if (!file->vars) {
        ng_ctl_free(desc);
        free(file);
        return NULL;
    }

    file->session = session;
    file->desc = desc;
    file->grid = NULL;
    platform_basename(ctl_path, file->ctl_path);
    file->type = (desc->dtype == NG_DTYPE_BUFR) ? 2 : 0;
    file->nx = desc->nx;
    file->ny = desc->ny;
    file->nz = desc->nz;
    file->nt = desc->nt;
    /* A missing EDEF card means a single ensemble (reference `q dims`
     * still reports the E axis on such files). sel_* default to step 1. */
    file->ne = desc->ne > 0 ? desc->ne : 1;
    file->sel_e = 0;
    file->vnum = desc->nvars;
    /* Absolute time axis (validated again defensively; the descriptor
     * parser already rejected malformed TDEF cards). */
    file->tax_ok =
        (ng_time_parse(desc->tdef_start, &file->tax_start) == 0 &&
         ng_incr_parse(desc->tdef_incr, &file->tax_count,
                       &file->tax_unit) == 0);
    if (!file->tax_ok) {
        snprintf(session->last_err, sizeof(session->last_err),
                 "cannot build the file time axis");
        ng_ctl_free(desc);
        free(file->vars);
        free(file);
        return NULL;
    }
    /* X/Y default to the full varying grid (reference `q dims` shows X/Y
     * varying over 1..nx / 1..ny on a fresh open). */
    file->sel_x1 = 0;
    file->sel_x2 = desc->nx - 1;
    file->sel_y1 = 0;
    file->sel_y2 = desc->ny - 1;
    for (i = 0; i < desc->nvars; i++) {
        file->vars[i].file = file;
        file->vars[i].index = i;
        file->vars[i].undef = desc->undef;
    }

    /* Add to session's file list (GrADS caps open files; fail, don't leak). */
    if (session->num_files < 16) {
        session->files[session->num_files++] = file;
    } else {
        snprintf(session->last_err, sizeof(session->last_err),
                 "too many open files (max 16)");
        ng_ctl_free(desc);
        free(file->vars);
        free(file);
        return NULL;
    }

    return file;
}

/* Last session error (NULL when the previous call succeeded). */
const char* grads_ng_session_error(const grads_ng_session_t* session) {
    if (!session || !session->last_err[0]) return NULL;
    return session->last_err;
}

/* File metadata accessors */
int grads_ng_file_dims(const grads_ng_file_t* file,
                       int* nx, int* ny, int* nz, int* nt) {
    if (!file || !nx || !ny || !nz || !nt) return -1;
    *nx = file->nx;
    *ny = file->ny;
    *nz = file->nz;
    *nt = file->nt;
    return 0;
}

int grads_ng_file_nvars(const grads_ng_file_t* file) {
    if (!file) return -1;
    return file->vnum;
}

int grads_ng_file_type(const grads_ng_file_t* file) {
    if (!file) return -1;
    return file->type;
}

/* Dimension selection (`set t` / `set z`), 0-based, validated. */
int grads_ng_file_select(grads_ng_file_t* file, int t, int z,
                         char* err, size_t errlen) {
#define SEL_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)

    if (!file) SEL_FAIL("no file is open");
    if (t < 0 || t >= file->nt)
        SEL_FAIL("t=%d out of range (file holds 1..%d)", t + 1, file->nt);
    if (z < 0 || z >= file->nz)
        SEL_FAIL("z=%d out of range (file holds 1..%d)", z + 1, file->nz);
    file->sel_t = t;
    file->sel_z = z;
    return 0;
#undef SEL_FAIL
}

int grads_ng_file_selected(const grads_ng_file_t* file, int* t, int* z) {
    if (!file || !t || !z) return -1;
    *t = file->sel_t;
    *z = file->sel_z;
    return 0;
}

/* Ensemble selection (`set e`), 0-based. The reference never range-checks
 * (any integer sticks, even 0 or past the last member), so neither does
 * this: out-of-range reads degrade to missing data at read time. */
int grads_ng_file_select_e(grads_ng_file_t* file, int e,
                           char* err, size_t errlen) {
    (void)err;
    (void)errlen;
    if (!file) return -1;
    file->sel_e = e;
    return 0;
}

int grads_ng_file_selected_e(const grads_ng_file_t* file, int* e) {
    if (!file || !e) return -1;
    *e = file->sel_e;
    return 0;
}

int grads_ng_file_ne(const grads_ng_file_t* file) {
    if (!file) return -1;
    return file->ne;
}

/* Member name for 1-based member m (NULL when the descriptor named no
 * members, or m is outside 1..ne — the reference prints (null) there). */
const char* grads_ng_file_ens_name(const grads_ng_file_t* file, int m) {
    if (!file || !file->desc || !file->desc->ens_names) return NULL;
    if (m < 1 || m > file->ne) return NULL;
    return file->desc->ens_names[m - 1];
}

/* X/Y window selection (`set x` / `set y`), 0-based inclusive, validated
 * against the full grid. Reference GrADS never range-checks `set x/y`
 * (it maps arithmetically, even outside the grid); NG rejects
 * out-of-range bounds instead, since reading outside the stored slice
 * cannot produce meaningful data. */
int grads_ng_file_select_xy(grads_ng_file_t* file, int x1, int x2,
                            int y1, int y2, char* err, size_t errlen) {
#define XY_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)

    if (!file) XY_FAIL("no file is open");
    if (x1 < 0 || x2 < 0 || x1 >= file->nx || x2 >= file->nx)
        XY_FAIL("x=%d..%d out of range (file holds 1..%d)", x1 + 1, x2 + 1,
                file->nx);
    if (y1 < 0 || y2 < 0 || y1 >= file->ny || y2 >= file->ny)
        XY_FAIL("y=%d..%d out of range (file holds 1..%d)", y1 + 1, y2 + 1,
                file->ny);
    if (x1 > x2) XY_FAIL("x range must ascend (got %d %d)", x1 + 1, x2 + 1);
    if (y1 > y2) XY_FAIL("y range must ascend (got %d %d)", y1 + 1, y2 + 1);
    file->sel_x1 = x1;
    file->sel_x2 = x2;
    file->sel_y1 = y1;
    file->sel_y2 = y2;
    return 0;
#undef XY_FAIL
}

int grads_ng_file_window(const grads_ng_file_t* file, int* x1, int* x2,
                         int* y1, int* y2) {
    if (!file || !x1 || !x2 || !y1 || !y2) return -1;
    *x1 = file->sel_x1;
    *x2 = file->sel_x2;
    *y1 = file->sel_y1;
    *y2 = file->sel_y2;
    return 0;
}

/* ---- World coordinates (coordinate milestone, slice 1: lon/lat/lev) ----
 *
 * Reference rules, probed against 2.2.1.oga.1 (see NG_BASELINE.md):
 * - `set x/y` take grid indices; `set lon/lat/lev` take world values.
 * - LINEAR axis: grid = round-half-up((w - start) / incr) + 1, never
 *   validated, never wrapped (`set lon 400` sticks at grid 5 of 4).
 * - LEVELS axis: nearest table entry; the one observed tie (750 between
 *   1000/500) resolved to the higher index.
 * NG keeps the conversion but validates the converted grid strictly
 * in-file, consistent with every other NG bound. */

/* Axis backing store from the descriptor. LINEAR: vals = [start, incr];
 * LEVELS: vals = full value table of length n. */
static void axis_backing(const grads_ng_file_t* file, char axis,
                         const double** vals, int* n, int* linear) {
    const ng_ctl_t* d = (file) ? file->desc : NULL;

    if (vals) *vals = NULL;
    if (n) *n = 0;
    if (linear) *linear = 0;
    if (!d) return;
    if (axis == 'x') {
        if (vals) *vals = d->xvals;
        if (n) *n = file->nx;
        if (linear) *linear = d->xlinear;
    } else if (axis == 'y') {
        if (vals) *vals = d->yvals;
        if (n) *n = file->ny;
        if (linear) *linear = d->ylinear;
    } else if (axis == 'z') {
        if (vals) *vals = d->zvals;
        if (n) *n = file->nz;
        if (linear) *linear = d->zlinear;
    }
}

/* 1-based grid index for a world value (reference rounding, unvalidated).
 * Returns 0 when the axis is missing or the value is not convertible. */
static int world_to_grid(const double* vals, int n, int linear, double w) {
    if (!vals || n <= 0 || !isfinite(w)) return 0;
    if (linear) {
        double g;
        if (vals[1] == 0.0) return 0;
        g = (w - vals[0]) / vals[1] + 1.0;
        if (!isfinite(g)) return 0;
        /* Clamp before the int cast (huge inputs fail validation below). */
        if (g > 2147483647.0 || g < -2147483647.0) return 2147483647;
        return (int)floor(g + 0.5);
    } else {
        int best = 0, i;
        double bd = fabs(w - vals[0]);
        for (i = 1; i < n; i++) {
            double d = fabs(w - vals[i]);
            if (d <= bd) {
                bd = d;
                best = i;  /* last wins ties, as observed */
            }
        }
        return best + 1;
    }
}

/* World value at a 1-based grid index. Returns 0 with *w set, -1 when the
 * axis is missing or the index is outside 1..n. */
int grads_ng_file_grid_to_world(const grads_ng_file_t* file, char axis,
                                int grid, double* w) {
    const double* vals;
    int n, linear;

    if (!w) return -1;
    axis_backing(file, axis, &vals, &n, &linear);
    if (!vals || grid < 1 || grid > n) return -1;
    *w = linear ? vals[0] + (grid - 1) * vals[1] : vals[grid - 1];
    return 0;
}

/* World-coordinate selection (`set lon/lat/lev`): convert both bounds with
 * reference rounding, require ascending converted grids, then store
 * strictly in-file. `lev` takes a single value (level ranges need the
 * varying-z display path). Snapped world values go to s1/s2 (may be NULL)
 * for the reference-style echo. */
int grads_ng_file_select_world(grads_ng_file_t* file, char axis,
                               double w1, double w2,
                               double* s1, double* s2,
                               char* err, size_t errlen) {
#define W_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)
    const double* vals;
    const char* name;
    int n, linear, g1, g2, i;
    double lo, hi, t;

    if (!file) W_FAIL("no file is open");
    if (axis == 'x') name = "lon";
    else if (axis == 'y') name = "lat";
    else if (axis == 'z') name = "lev";
    else W_FAIL("unknown dimension '%c'", axis);
    axis_backing(file, axis, &vals, &n, &linear);
    if (!vals || n <= 0) W_FAIL("no %s axis is defined", name);
    if (axis == 'z' && w1 != w2)
        W_FAIL("level ranges are not implemented yet (use 'set z N')");
    g1 = world_to_grid(vals, n, linear, w1);
    g2 = world_to_grid(vals, n, linear, w2);
    if (g1 < 1 || g1 > n || g2 < 1 || g2 > n) {
        if (linear) {
            lo = vals[0];
            hi = vals[0] + (n - 1) * vals[1];
            if (hi < lo) {
                t = lo;
                lo = hi;
                hi = t;
            }
        } else {
            lo = hi = vals[0];
            for (i = 1; i < n; i++) {
                if (vals[i] < lo) lo = vals[i];
                if (vals[i] > hi) hi = vals[i];
            }
        }
        W_FAIL("%s=%g out of range (file holds %g to %g)", name,
               (g1 < 1) ? w1 : w2, lo, hi);
    }
    if (g1 > g2)
        W_FAIL("'%s' range must ascend (got %g %g)", name, w1, w2);
    if (axis == 'x') {
        file->sel_x1 = g1 - 1;
        file->sel_x2 = g2 - 1;
    } else if (axis == 'y') {
        file->sel_y1 = g1 - 1;
        file->sel_y2 = g2 - 1;
    } else {
        file->sel_z = g1 - 1;
    }
    if (s1 && grads_ng_file_grid_to_world(file, axis, g1, s1) != 0)
        W_FAIL("cannot describe the open file");
    if (s2 && grads_ng_file_grid_to_world(file, axis, g2, s2) != 0)
        W_FAIL("cannot describe the open file");
    return 0;
#undef W_FAIL
}

/* ---- Absolute time (`set time`) ----
 *
 * The axis steps from the TDEF start by the TDEF increment (see
 * ng/src/time/ for the calendar rules). `set time` snaps to the nearest
 * step, ties up, exactly like the reference; out-of-window targets fail
 * strictly (the reference clamps and proceeds). */

/* Canonical "00Z03JAN1987" rendering of step k (0-based). */
int grads_ng_file_time_at(const grads_ng_file_t* file, int k,
                          char* buf, size_t len) {
    ng_time_t t;
    if (!file || !file->tax_ok || !buf || len == 0) return -1;
    if (k < 0 || k >= file->nt) return -1;
    t = ng_time_step(file->tax_start, file->tax_count, file->tax_unit, k);
    ng_time_format(t, buf, len);
    return 0;
}

/* Select a time by GrADS datetime string. Two-valued ranges need the
 * varying-T display path and are rejected honestly. echo_out receives the
 * reference-style "1987:1:3:0" stamp of the snapped step. */
int grads_ng_file_select_time(grads_ng_file_t* file, const char* s,
                              char* echo_out, size_t echo_len,
                              char* err, size_t errlen) {
#define T_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)
    ng_time_t target, step;
    long long tgt, s0, sN, gap;
    int k;

    if (!file) T_FAIL("no file is open");
    if (!file->tax_ok) T_FAIL("no time axis is defined");
    if (!s || ng_time_parse(s, &target) != 0)
        T_FAIL("Syntax Error: Invalid Date/Time value \"%s\".",
               s ? s : "");
    k = ng_time_nearest(file->tax_start, file->tax_count, file->tax_unit,
                        file->nt, target);
    tgt = ng_time_absmin(target);
    step = ng_time_step(file->tax_start, file->tax_count, file->tax_unit, k);
    if (file->nt > 1) {
        /* Strict half-step window around the axis ends. */
        s0 = ng_time_absmin(file->tax_start);
        sN = ng_time_absmin(ng_time_step(file->tax_start, file->tax_count,
                                         file->tax_unit, file->nt - 1));
        if (k == 0) {
            gap = ng_time_absmin(ng_time_step(file->tax_start,
                                              file->tax_count,
                                              file->tax_unit, 1)) - s0;
            if (2 * (s0 - tgt) > gap) k = -1;
        }
        if (k == file->nt - 1 && k >= 0) {
            gap = sN - ng_time_absmin(ng_time_step(file->tax_start,
                                                   file->tax_count,
                                                   file->tax_unit,
                                                   file->nt - 2));
            if (2 * (tgt - sN) > gap) k = -1;
        }
    }
    if (k < 0) {
        char lo[32], hi[32];
        ng_time_format(file->tax_start, lo, sizeof(lo));
        ng_time_format(ng_time_step(file->tax_start, file->tax_count,
                                    file->tax_unit, file->nt - 1),
                       hi, sizeof(hi));
        T_FAIL("time %s out of range (file holds %s to %s)", s, lo, hi);
    }
    file->sel_t = k;
    if (echo_out && echo_len > 0)
        snprintf(echo_out, echo_len, "%lld:%d:%d:%d", step.yr, step.mo,
                 step.dy, step.hr);
    return 0;
#undef T_FAIL
}

/* Close a GrADS file */
void grads_ng_close(grads_ng_file_t* file) {
    grads_ng_session_t* s;
    int i;

    if (!file) return;

    /* Remove from session (swap-remove so destroy never sees a dangling
     * pointer no matter what order callers close files in). */
    s = file->session;
    if (s) {
        for (i = 0; i < s->num_files; i++) {
            if (s->files[i] == file) {
                s->files[i] = s->files[--s->num_files];
                s->files[s->num_files] = NULL;
                break;
            }
        }
    }

    ng_grid_close(file->grid);
    ng_ctl_free(file->desc);
    free(file->vars);
    free(file);
}

/* Create an off-screen canvas */
grads_ng_canvas_t* grads_ng_canvas_create(int width, int height) {
    grads_ng_canvas_t* canvas;
    
    if (width <= 0 || height <= 0) return NULL;
    
    canvas = calloc(1, sizeof(grads_ng_canvas_t));
    if (!canvas) return NULL;
    
    canvas->width = width;
    canvas->height = height;
    canvas->pixels = calloc(width * height, sizeof(rgba_t));
    canvas->zbuffer = calloc(width * height, sizeof(double));
    
    if (!canvas->pixels || !canvas->zbuffer) {
        free(canvas->pixels);
        free(canvas->zbuffer);
        free(canvas);
        return NULL;
    }
    
    /* Default: white background */
    int i;
    for (i = 0; i < width * height; i++) {
        canvas->pixels[i] = (rgba_t){255, 255, 255, 255};
    }
    
    canvas->linewidth = 1;
    canvas->linecolor = 0;
    
    return canvas;
}

/* Destroy canvas */
void grads_ng_canvas_destroy(grads_ng_canvas_t* canvas) {
    if (!canvas) return;
    free(canvas->pixels);
    free(canvas->zbuffer);
    free(canvas);
}

/* Get variable by name */
/* Look up a variable by name (borrowed handle, valid until close). */
grads_ng_var_t* grads_ng_get_var(grads_ng_file_t* file, const char* varname) {
    int i;

    if (!file || !varname || !*varname) return NULL;
    for (i = 0; i < file->vnum; i++) {
        if (var_name_eq(file->desc->vars[i].name, varname))
            return &file->vars[i];
    }
    return NULL;
}

const char* grads_ng_var_name(const grads_ng_var_t* var) {
    if (!var || !var->file) return NULL;
    return var->file->desc->vars[var->index].name;
}

int grads_ng_var_levels(const grads_ng_var_t* var) {
    int n;
    if (!var || !var->file) return -1;
    n = var->file->desc->vars[var->index].nlevels;
    return n == 0 ? 1 : n;
}

double grads_ng_var_undef(const grads_ng_var_t* var) {
    if (!var) return 0.0;
    return var->undef;
}

const char* grads_ng_file_varname(const grads_ng_file_t* file, int index) {
    if (!file || index < 0 || index >= file->vnum) return NULL;
    return file->desc->vars[index].name;
}

/* Read one slice at the selected ensemble, opening the data file lazily
 * on first use. An out-of-range ensemble degrades the whole slice to
 * missing (reference parity: the reference warns "request completely
 * outside file limits" and contours all-missing); the CLI echoes the
 * warning, so this layer stays quiet. */
int grads_ng_var_slice(grads_ng_var_t* var, int t, int z, double* out,
                       char* err, size_t errlen) {
    grads_ng_file_t* file;
    const char* gerr = NULL;

#define VAR_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)

    if (!var || !var->file || !out)
        VAR_FAIL("cannot read from an empty variable reference");
    file = var->file;

    if (file->sel_e < 0 || file->sel_e >= file->ne) {
        long n = (long)file->nx * file->ny, k;
        for (k = 0; k < n; k++) out[k] = NAN;
        return 0;
    }
    if (!file->grid) {
        file->grid = ng_grid_open(file->desc, &gerr);
        if (!file->grid)
            VAR_FAIL("cannot open data for \"%s\": %s",
                     file->ctl_path[0] ? file->ctl_path : "(open file)",
                     gerr ? gerr : "unknown error");
    }
    if (ng_grid_read_slice(file->grid, var->index, t, z, file->sel_e, out,
                           &gerr) != 0)
        VAR_FAIL("%s", gerr ? gerr : "read failed");
    return 0;
#undef VAR_FAIL
}

/* Stub functions for expression engine and rendering */
grads_ng_expr_t* grads_ng_compile_expr(grads_ng_session_t* session, const char* expr) {
    (void)session;
    (void)expr;
    return NULL;  /* Not implemented yet */
}

int grads_ng_eval_expr(grads_ng_expr_t* expr, grads_ng_file_t* file, void* result) {
    (void)expr;
    (void)file;
    (void)result;
    return -1;
}

void grads_ng_free_expr(grads_ng_expr_t* expr) {
    (void)expr;
}

int grads_ng_read_data(grads_ng_var_t* var, int ix, int iy, int iz, int it, void* buffer) {
    (void)var;
    (void)ix;
    (void)iy;
    (void)iz;
    (void)it;
    (void)buffer;
    return -1;
}

int grads_ng_write_data(grads_ng_var_t* var, int ix, int iy, int iz, int it, const void* buffer) {
    (void)var;
    (void)ix;
    (void)iy;
    (void)iz;
    (void)it;
    (void)buffer;
    return -1;
}

/* Placeholder rendering functions */
int grads_ng_canvas_draw_line(grads_ng_canvas_t* canvas, double x1, double y1, double x2, double y2) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    return 0;
}

int grads_ng_canvas_draw_contour(grads_ng_canvas_t* canvas, double* grid, int nx, int ny) {
    (void)canvas;
    (void)grid;
    (void)nx;
    (void)ny;
    return 0;
}

int grads_ng_canvas_draw_vector(grads_ng_canvas_t* canvas, double* u, double* v, int nx, int ny) {
    (void)canvas;
    (void)u;
    (void)v;
    (void)nx;
    (void)ny;
    return 0;
}

int grads_ng_canvas_draw_stnmark(grads_ng_canvas_t* canvas, double* lats, double* lons, int n) {
    (void)canvas;
    (void)lats;
    (void)lons;
    (void)n;
    return 0;
}

/* Output as PNG */
int grads_ng_canvas_save_png(grads_ng_canvas_t* canvas, const char* path) {
    (void)canvas;
    (void)path;
    return -1;  /* Requires libpng */
}

/* Output as SVG */
int grads_ng_canvas_save_svg(grads_ng_canvas_t* canvas, const char* path) {
    (void)canvas;
    (void)path;
    return -1;
}

/* Script execution */
int grads_ng_run_script(grads_ng_session_t* session, const char* script_path) {
    (void)session;
    (void)script_path;
    return -1;
}

int grads_ng_exec_command(grads_ng_session_t* session, const char* command) {
    (void)session;
    (void)command;
    return -1;
}