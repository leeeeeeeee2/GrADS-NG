#ifndef NG_SHADE_H
#define NG_SHADE_H

/*
 * ng/src/render/shade.h
 * M5 slice 1: shaded-grid raster output behind `gxprint`.
 *
 * Dependency-free and byte-deterministic: the same array always yields the
 * same file, which is what makes rendered output CTest-comparable.
 * Full vector output (canvas lines/text/PNG) arrives in later M5 slices.
 */

#include <stddef.h>

/* Pixels per grid cell edge. */
#define NG_SHADE_CELL 10

/* Missing-value marker color. */
#define NG_SHADE_MISSING_R 255
#define NG_SHADE_MISSING_G 0
#define NG_SHADE_MISSING_B 255

/* Grayscale shade 0-255 for v on [lo, hi]; NaN maps to -1 (missing).
 * A degenerate range (lo == hi) maps every valid value to mid-gray. */
int ng_shade_gray(double v, double lo, double hi);

/* Write a binary PPM (P6) of the grid: one NG_SHADE_CELL square per cell,
 * y = 0 row at the bottom (north at top), missing cells magenta, with a
 * `# <label>` comment line for traceability. Returns 0 or -1 with errno
 * left from the failing call. */
int ng_shade_write_ppm(const char *path, const char *label,
                       const double *data, int nx, int ny);

/* --- M5 slice 2: RGB raster + contour overlay (shared by PPM/PNG) ---
 *
 * Pixel convention (origin top-left): cell (x, y) covers
 * [x*CELL,(x+1)*CELL) x [(ny-1-y)*CELL,(ny-y)*CELL), so grid y = 0 sits
 * at the bottom. Grid point (gx, gy) maps to (gx*CELL, (ny-1-gy)*CELL). */

/* Raster dimensions for a grid. */
void ng_shade_raster_size(int nx, int ny, int *w, int *h);

/* Render the shaded raster (same mapping as write_ppm, contour-free).
 * Returns a malloc'd w*h*3 buffer (row-major, top row first) or NULL. */
unsigned char *ng_shade_render_rgb(const double *data, int nx, int ny);

/* Overlay contour lines (black, 1px) for the auto levels spanning the
 * valid data range. Returns the level count, or 0 when there is nothing
 * to draw (degenerate grid/range, NULL args). */
int ng_shade_overlay_contours(unsigned char *rgb, int w, int h,
                              const double *data, int nx, int ny);

#endif /* NG_SHADE_H */
