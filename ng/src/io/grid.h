#ifndef NG_GRID_H
#define NG_GRID_H

/*
 * ng/src/io/grid.h
 * GrADS sequential/stream binary data reader.
 *
 * Layout (classic GrADS, matching gaddes.c record order):
 *   variables in VARS order; inside each variable, ensemble members outer,
 *   then time steps, levels innermost; each record holds nx*ny big-endian
 *   float32 values with x varying fastest. OPTIONS sequential wraps every
 *   record in 4-byte Fortran markers. OPTIONS byteswapped swaps each
 *   4-byte word.
 *
 * The file is memory-mapped read-only: slices are decoded on demand into
 * caller buffers, never copied wholesale. UNDEF values pass through
 * untouched; masking is the expression engine's job (M3).
 */

#include <stddef.h>

#include "ctl.h"

typedef struct ng_grid ng_grid_t;

/* Open the data file described by ctl (non-template binary only).
 * Returns NULL on error with *err_out set (static string, do not free). */
ng_grid_t *ng_grid_open(const ng_ctl_t *ctl, const char **err_out);
void       ng_grid_close(ng_grid_t *g);

/* Levels held by variable var (1 for surface variables). */
int ng_grid_var_levels(const ng_grid_t *g, int var);

/* Read one (var, t, z, e) slice into out[ny*nx] doubles, x fastest.
 * Indices are 0-based. Ensemble is outermost (reference-probed): each
 * member holds the full nt x levels run. Returns 0 on success, -1 with
 * *err_out set. */
int ng_grid_read_slice(ng_grid_t *g, int var, int t, int z, int e,
                       double *out, const char **err_out);

#endif /* NG_GRID_H */
