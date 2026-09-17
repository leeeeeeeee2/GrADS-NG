/*
 * GrADS-NG Core Implementation
 * 
 * Platform abstraction layer for cross-platform compatibility.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "grads_ng.h"
#include "backend/platform.h"
#include "../io/ctl.h"
#include "../io/grid.h"

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
    int sel_z;              /* selected level index, 0-based (`set z`) */
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
    file->ne = desc->ne;
    file->vnum = desc->nvars;
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

/* Read one slice, opening the data file lazily on first use. */
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

    if (!file->grid) {
        file->grid = ng_grid_open(file->desc, &gerr);
        if (!file->grid)
            VAR_FAIL("cannot open data for \"%s\": %s",
                     file->ctl_path[0] ? file->ctl_path : "(open file)",
                     gerr ? gerr : "unknown error");
    }
    if (ng_grid_read_slice(file->grid, var->index, t, z, out, &gerr) != 0)
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