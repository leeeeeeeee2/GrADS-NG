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

/* File operations */
grads_ng_file_t* grads_ng_open(grads_ng_session_t* session, const char* ctl_path);
void grads_ng_close(grads_ng_file_t* file);

/* Data access */
grads_ng_var_t* grads_ng_get_var(grads_ng_file_t* file, const char* varname);
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