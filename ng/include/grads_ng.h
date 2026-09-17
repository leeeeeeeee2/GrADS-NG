/*
 * GrADS-NG: Next Generation GrADS
 * 
 * A modern, cross-platform GrADS implementation with native headless mode,
 * Python bindings, and modular architecture.
 * 
 * Copyright (C) 2026 GrADS-NG Team
 */

#ifndef GRADS_NG_H
#define GRADS_NG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Version info */
#define GRADS_NG_VERSION_MAJOR 0
#define GRADS_NG_VERSION_MINOR 1
#define GRADS_NG_VERSION_PATCH 0

/* Platform detection */
#if defined(_WIN32) || defined(_WIN64)
    #define GRADS_NG_PLATFORM_WINDOWS
#elif defined(__APPLE__)
    #define GRADS_NG_PLATFORM_MACOS
#elif defined(__linux__)
    #define GRADS_NG_PLATFORM_LINUX
#endif

/* Data types */
typedef int32_t grads_ng_int;
typedef int64_t grads_ng_long;
typedef float grads_ng_float;
typedef double grads_ng_double;

/* Handle types */
typedef struct grads_ng_session grads_ng_session_t;
typedef struct grads_ng_file grads_ng_file_t;
typedef struct grads_ng_var grads_ng_var_t;
typedef struct grads_ng_canvas grads_ng_canvas_t;
typedef struct grads_ng_expr grads_ng_expr_t;

/* Configuration */
typedef struct {
    bool headless;
    bool python_enabled;
    int batch_mode;
    char* gaddir;
    char* gatdir;
    int cache_size;
    bool use_opendap;
} grads_ng_config_t;

/* Initialization / Cleanup */
grads_ng_session_t* grads_ng_init(grads_ng_config_t* config);
void grads_ng_destroy(grads_ng_session_t* session);
/* Reason the most recent session call failed, or NULL when it succeeded. */
const char* grads_ng_session_error(const grads_ng_session_t* session);

/* File operations */
grads_ng_file_t* grads_ng_open(grads_ng_session_t* session, const char* ctl_path);
void grads_ng_close(grads_ng_file_t* file);

/* File metadata accessors. grads_ng_file_t stays opaque: callers must use
 * these instead of reaching into the struct. dims/nvars/type return 0 on
 * success, -1 when file (or any out-pointer for dims) is NULL. */
int grads_ng_file_dims(const grads_ng_file_t* file,
                       int* nx, int* ny, int* nz, int* nt);
int grads_ng_file_nvars(const grads_ng_file_t* file);
int grads_ng_file_type(const grads_ng_file_t* file);
/* Dimension selection (`set t` / `set z`), 0-based and range-checked.
 * select returns 0 or -1 with a message; selected reports current values. */
int grads_ng_file_select(grads_ng_file_t* file, int t, int z,
                         char* err, size_t errlen);
int grads_ng_file_selected(const grads_ng_file_t* file, int* t, int* z);
/* Ensemble selection (`set e`), 0-based and deliberately UNVALIDATED like
 * the reference (any integer sticks, even 0 or past the last member).
 * select_e stores it (NULL file fails); selected_e reports it; ne reports
 * the member count (1 when the descriptor has no EDEF); ens_name names
 * 1-based member m, or NULL when unnamed or out of range. */
int grads_ng_file_select_e(grads_ng_file_t* file, int e,
                           char* err, size_t errlen);
int grads_ng_file_selected_e(const grads_ng_file_t* file, int* e);
int grads_ng_file_ne(const grads_ng_file_t* file);
const char* grads_ng_file_ens_name(const grads_ng_file_t* file, int m);
/* X/Y window selection (`set x` / `set y`), 0-based inclusive, validated
 * against the full grid; defaults to the whole grid on open. select_xy
 * returns 0 or -1 with a message; window reports current values. */
int grads_ng_file_select_xy(grads_ng_file_t* file, int x1, int x2,
                            int y1, int y2, char* err, size_t errlen);
int grads_ng_file_window(const grads_ng_file_t* file, int* x1, int* x2,
                         int* y1, int* y2);
/* World-coordinate selection (`set lon/lat/lev`): reference rounding
 * (LINEAR inverse round-half-up; LEVELS nearest, ties to higher index),
 * ascending converted grids, strict in-file storage. `lev` takes a single
 * value. Snapped worlds go to s1/s2 (may be NULL) for the echo. Returns 0
 * or -1 with a message. grid_to_world inverts one 1-based index. */
int grads_ng_file_select_world(grads_ng_file_t* file, char axis,
                               double w1, double w2,
                               double* s1, double* s2,
                               char* err, size_t errlen);
int grads_ng_file_grid_to_world(const grads_ng_file_t* file, char axis,
                                int grid, double* w);
/* Absolute time axis (`set time`, `q dims` Time). time_at renders step k
 * (0-based) canonically ("00Z03JAN1987"). select_time snaps a GrADS
 * datetime string to the nearest step (ties up), stores it, and reports
 * the reference-style "1987:1:3:0" stamp in echo_out (may be NULL).
 * Time ranges need the varying-T display path and fail honestly. */
int grads_ng_file_time_at(const grads_ng_file_t* file, int k,
                          char* buf, size_t len);
int grads_ng_file_select_time(grads_ng_file_t* file, const char* s,
                              char* echo_out, size_t echo_len,
                              char* err, size_t errlen);

/* Data access.
 * Variable handles are borrowed from the file (valid until close, do not
 * free). Lookup is ASCII case-insensitive like GrADS; NULL when absent. */
grads_ng_var_t* grads_ng_get_var(grads_ng_file_t* file, const char* varname);
const char* grads_ng_var_name(const grads_ng_var_t* var);
int grads_ng_var_levels(const grads_ng_var_t* var);
double grads_ng_var_undef(const grads_ng_var_t* var);
const char* grads_ng_file_varname(const grads_ng_file_t* file, int index);
/* Read one (t, z) slice at the selected ensemble, 0-based, into
 * out[nx*ny] doubles, x fastest. The data file opens lazily on the first
 * call. A selected ensemble outside 1..ne degrades to all-missing
 * (reference parity) rather than failing. Returns 0 on success, -1 with
 * a message in err (up to errlen bytes) otherwise. */
int grads_ng_var_slice(grads_ng_var_t* var, int t, int z, double* out,
                       char* err, size_t errlen);
int grads_ng_read_data(grads_ng_var_t* var, int ix, int iy, int iz, int it, void* buffer);
int grads_ng_write_data(grads_ng_var_t* var, int ix, int iy, int iz, int it, const void* buffer);

/* Expression engine */
grads_ng_expr_t* grads_ng_compile_expr(grads_ng_session_t* session, const char* expr);
int grads_ng_eval_expr(grads_ng_expr_t* expr, grads_ng_file_t* file, void* result);
void grads_ng_free_expr(grads_ng_expr_t* expr);

/* Canvas / Rendering (virtual/off-screen) */
grads_ng_canvas_t* grads_ng_canvas_create(int width, int height);
void grads_ng_canvas_destroy(grads_ng_canvas_t* canvas);
int grads_ng_canvas_draw_line(grads_ng_canvas_t* canvas, double x1, double y1, double x2, double y2);
int grads_ng_canvas_draw_contour(grads_ng_canvas_t* canvas, double* grid, int nx, int ny);
int grads_ng_canvas_draw_vector(grads_ng_canvas_t* canvas, double* u, double* v, int nx, int ny);
int grads_ng_canvas_draw_stnmark(grads_ng_canvas_t* canvas, double* lats, double* lons, int n);

/* Output */
int grads_ng_canvas_save_png(grads_ng_canvas_t* canvas, const char* path);
int grads_ng_canvas_save_svg(grads_ng_canvas_t* canvas, const char* path);

/* Scripting */
int grads_ng_run_script(grads_ng_session_t* session, const char* script_path);
int grads_ng_exec_command(grads_ng_session_t* session, const char* command);

/* Python bindings */
#if defined(GRADS_NG_PYTHON)
#include <Python.h>
PyObject* grads_ng_py_import_module(void);
#endif

#endif /* GRADS_NG_H */