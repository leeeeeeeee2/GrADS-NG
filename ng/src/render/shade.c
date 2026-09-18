/*
 * ng/src/render/shade.c
 * Shaded-grid raster output (see shade.h).
 */

#include "shade.h"

#include "contour.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ng_shade_gray(double v, double lo, double hi) {
    double f;
    if (isnan(v)) return -1;
    if (!(hi > lo)) return 128;  /* degenerate range (incl. NaN bounds) */
    f = (v - lo) / (hi - lo);
    if (f <= 0.0) return 0;
    if (f >= 1.0) return 255;
    return (int)(f * 255.0 + 0.5);
}

int ng_shade_write_ppm(const char *path, const char *label,
                       const double *data, int nx, int ny) {
    FILE *fp;
    double lo = 0.0, hi = 0.0;
    long n, k, count = 0;
    int x, y, cx, cy, g;
    unsigned char px[3];

    if (!path || !data || nx <= 0 || ny <= 0) return -1;
    n = (long)nx * ny;
    for (k = 0; k < n; k++) {
        double v = data[k];
        if (isnan(v)) continue;
        if (count == 0 || v < lo) lo = v;
        if (count == 0 || v > hi) hi = v;
        count++;
    }

    fp = fopen(path, "wb");
    if (!fp) return -1;
    fprintf(fp, "P6\n# grads-ng shaded %s\n%d %d\n255\n",
            (label && *label) ? label : "display",
            nx * NG_SHADE_CELL, ny * NG_SHADE_CELL);
    /* PPM rows run top-first: emit the highest grid row first. */
    for (y = ny - 1; y >= 0; y--) {
        for (cy = 0; cy < NG_SHADE_CELL; cy++) {
            for (x = 0; x < nx; x++) {
                double v = data[(long)y * nx + x];
                g = ng_shade_gray(v, lo, hi);
                if (g < 0) {
                    px[0] = NG_SHADE_MISSING_R;
                    px[1] = NG_SHADE_MISSING_G;
                    px[2] = NG_SHADE_MISSING_B;
                } else {
                    px[0] = px[1] = px[2] = (unsigned char)g;
                }
                for (cx = 0; cx < NG_SHADE_CELL; cx++) {
                    if (fwrite(px, 1, 3, fp) != 3) {
                        fclose(fp);
                        return -1;
                    }
                }
            }
        }
    }
    if (fclose(fp) != 0) return -1;
    return 0;
}

/* --- M5 slice 2: RGB raster + contour overlay --- */

void ng_shade_raster_size(int nx, int ny, int *w, int *h) {
    if (w) *w = nx * NG_SHADE_CELL;
    if (h) *h = ny * NG_SHADE_CELL;
}

/* Valid-data range; returns 0 when no valid cell exists. */
static int data_range(const double *data, long n, double *lo, double *hi) {
    long k, count = 0;
    double l = 0.0, h = 0.0;
    for (k = 0; k < n; k++) {
        double v = data[k];
        if (isnan(v)) continue;
        if (count == 0 || v < l) l = v;
        if (count == 0 || v > h) h = v;
        count++;
    }
    if (count == 0) return 0;
    *lo = l;
    *hi = h;
    return 1;
}

unsigned char *ng_shade_render_rgb(const double *data, int nx, int ny) {
    int w, h, x, y, cx, cy, g;
    double lo, hi;
    unsigned char *rgb, *row;

    if (!data || nx <= 0 || ny <= 0) return NULL;
    if (!data_range(data, (long)nx * ny, &lo, &hi)) {
        lo = 0.0;
        hi = 0.0; /* all missing: grays map degenerate, cells magenta */
    }
    ng_shade_raster_size(nx, ny, &w, &h);
    rgb = malloc((size_t)w * h * 3);
    if (!rgb) return NULL;
    for (y = ny - 1; y >= 0; y--) {
        row = rgb + (size_t)(ny - 1 - y) * NG_SHADE_CELL * w * 3;
        for (cy = 0; cy < NG_SHADE_CELL; cy++) {
            unsigned char *px = row + (size_t)cy * w * 3;
            for (x = 0; x < nx; x++) {
                unsigned char c[3];
                g = ng_shade_gray(data[(long)y * nx + x], lo, hi);
                if (g < 0) {
                    c[0] = NG_SHADE_MISSING_R;
                    c[1] = NG_SHADE_MISSING_G;
                    c[2] = NG_SHADE_MISSING_B;
                } else {
                    c[0] = c[1] = c[2] = (unsigned char)g;
                }
                for (cx = 0; cx < NG_SHADE_CELL; cx++) {
                    px[0] = c[0];
                    px[1] = c[1];
                    px[2] = c[2];
                    px += 3;
                }
            }
        }
    }
    return rgb;
}

typedef struct {
    unsigned char *rgb;
    int w, h, ny;
} plot_ctx_t;

static void plot_dot(plot_ctx_t *p, int ix, int iy) {
    unsigned char *px;
    if (ix < 0 || iy < 0 || ix >= p->w || iy >= p->h) return;
    px = p->rgb + (size_t)iy * p->w * 3 + (size_t)ix * 3;
    px[0] = NG_CONTOUR_R;
    px[1] = NG_CONTOUR_G;
    px[2] = NG_CONTOUR_B;
}

/* Bresenham between grid points, mapped to pixels. */
static void plot_seg(double x0, double y0, double x1, double y1, void *ctx) {
    plot_ctx_t *p = ctx;
    int ax = (int)(x0 * NG_SHADE_CELL + 0.5);
    int ay = (int)((p->ny - 1 - y0) * NG_SHADE_CELL + 0.5);
    int bx = (int)(x1 * NG_SHADE_CELL + 0.5);
    int by = (int)((p->ny - 1 - y1) * NG_SHADE_CELL + 0.5);
    int dx = abs(bx - ax), dy = -abs(by - ay);
    int sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        plot_dot(p, ax, ay);
        if (ax == bx && ay == by) break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            ax += sx;
        }
        if (e2 <= dx) {
            err += dx;
            ay += sy;
        }
    }
}

int ng_shade_overlay_contours(unsigned char *rgb, int w, int h,
                              const double *data, int nx, int ny) {
    double lo, hi, levels[NG_CONTOUR_MAXLEVELS];
    int nlev, i;
    plot_ctx_t ctx;

    if (!rgb || !data || nx < 2 || ny < 2) return 0;
    if (w != nx * NG_SHADE_CELL || h != ny * NG_SHADE_CELL) return 0;
    if (!data_range(data, (long)nx * ny, &lo, &hi)) return 0;
    nlev = ng_contour_levels(lo, hi, levels, NG_CONTOUR_MAXLEVELS);
    ctx.rgb = rgb;
    ctx.w = w;
    ctx.h = h;
    ctx.ny = ny;
    for (i = 0; i < nlev; i++)
        ng_contour_each(data, nx, ny, levels[i], plot_seg, &ctx);
    return nlev;
}
