/*
 * ng/src/render/contour.h
 * M5 slice 2: contour lines over the shaded grid.
 *
 * Levels follow GrADS default style: "nice" even intervals spanning the
 * data range (e.g. -5..45 step 5 for July tmax). Tracing is marching
 * squares over cell corners; cells touching a missing (NaN) corner emit
 * no segments, so lines never cross unknown data. Saddle cells resolve
 * by the cell-centre average (the standard asymptotic choice).
 *
 * All coordinates are grid coordinates: integer (x, y) at cell corners,
 * fractional positions linearly interpolated along edges.
 */

#ifndef NG_CONTOUR_H
#define NG_CONTOUR_H

#include <stddef.h>

/* Cap on auto levels; more levels thin the step (never truncate). */
#define NG_CONTOUR_MAXLEVELS 64

/* Contour line color in the rendered raster. */
#define NG_CONTOUR_R 0
#define NG_CONTOUR_G 0
#define NG_CONTOUR_B 0

/* Fill levels[0..maxlevels) with nice even intervals covering [lo, hi].
 * Returns the level count, or 0 when the range is degenerate (lo >= hi,
 * NaN bounds). Levels are ascending. */
int ng_contour_levels(double lo, double hi, double *levels, int maxlevels);

/* One traced segment in grid coordinates. */
typedef struct {
    double x0, y0, x1, y1;
} ng_segment_t;

/* Callback invoked for every segment of `level`. */
typedef void (*ng_seg_fn)(double x0, double y0,
                          double x1, double y1, void *ctx);

/* Trace one level, invoking fn per segment. Cells with a NaN corner are
 * skipped. Does nothing for degenerate grids (nx < 2 or ny < 2). */
void ng_contour_each(const double *data, int nx, int ny, double level,
                     ng_seg_fn fn, void *ctx);

/* Count the segments ng_contour_each would emit (convenience for tests). */
long ng_contour_count(const double *data, int nx, int ny, double level);

#endif /* NG_CONTOUR_H */
