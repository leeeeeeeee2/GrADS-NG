/*
 * ng/src/render/shade.c
 * Shaded-grid raster output (see shade.h).
 */

#include "shade.h"

#include <math.h>
#include <stdio.h>
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
