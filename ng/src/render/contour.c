/*
 * ng/src/render/contour.c
 * Contour levels + marching-squares tracing (see contour.h).
 */

#include "contour.h"

#include <math.h>

int ng_contour_levels(double lo, double hi, double *levels, int maxlevels) {
    static const double nice[] = {1.0, 2.0, 2.5, 5.0, 10.0};
    double span, raw, mag, step, start;
    int i, n = 0;

    if (!levels || maxlevels <= 0) return 0;
    if (isnan(lo) || isnan(hi) || !(hi > lo)) return 0;
    span = hi - lo;
    raw = span / 10.0;
    mag = pow(10.0, floor(log10(raw)));
    step = 10.0 * mag;  /* fallback, always overwritten below */
    for (i = 0; i < 5; i++) {
        if (nice[i] * mag >= raw) {
            step = nice[i] * mag;
            break;
        }
    }
    /* Thin the step instead of truncating when capped. */
    while ((hi - floor(lo / step) * step) / step + 1 > maxlevels) step *= 2.0;
    start = floor(lo / step) * step;
    for (i = 0; i < maxlevels + 1; i++) {
        double lv = start + i * step;
        if (lv > hi) break;
        if (lv >= lo - step * 1e-9) levels[n++] = lv;
        if (n >= maxlevels) break;
    }
    return n;
}

/* Corner order: 0=(x,y) 1=(x+1,y) 2=(x+1,y+1) 3=(x,y+1). Edge points are
 * written into ex[4], ey[4] (-1 when the edge is not crossed). */
static void cell_edges(const double *v, double level,
                       double x, double y, double *ex, double *ey) {
    int e;
    double t;
    for (e = 0; e < 4; e++) ex[e] = ey[e] = -1.0;
    /* edge 0: bottom 0->1 */
    if ((v[0] < level) != (v[1] < level)) {
        t = (level - v[0]) / (v[1] - v[0]);
        ex[0] = x + t;
        ey[0] = y;
    }
    /* edge 1: right 1->2 */
    if ((v[1] < level) != (v[2] < level)) {
        t = (level - v[1]) / (v[2] - v[1]);
        ex[1] = x + 1.0;
        ey[1] = y + t;
    }
    /* edge 2: top 3->2 (walk 2->3 for symmetry of t, same point) */
    if ((v[2] < level) != (v[3] < level)) {
        t = (level - v[2]) / (v[3] - v[2]);
        ex[2] = x + 1.0 - t;
        ey[2] = y + 1.0;
    }
    /* edge 3: left 0->3 */
    if ((v[0] < level) != (v[3] < level)) {
        t = (level - v[0]) / (v[3] - v[0]);
        ex[3] = x;
        ey[3] = y + t;
    }
}

static void emit(double x0, double y0, double x1, double y1,
                 ng_seg_fn fn, void *ctx) {
    if (fn) fn(x0, y0, x1, y1, ctx);
}

void ng_contour_each(const double *data, int nx, int ny, double level,
                     ng_seg_fn fn, void *ctx) {
    int x, y;
    double v[4], ex[4], ey[4];
    int crossed[4], ncross;

    if (!data || nx < 2 || ny < 2 || isnan(level)) return;
    for (y = 0; y < ny - 1; y++) {
        for (x = 0; x < nx - 1; x++) {
            v[0] = data[(long)y * nx + x];
            v[1] = data[(long)y * nx + x + 1];
            v[2] = data[(long)(y + 1) * nx + x + 1];
            v[3] = data[(long)(y + 1) * nx + x];
            if (isnan(v[0]) || isnan(v[1]) || isnan(v[2]) || isnan(v[3]))
                continue;
            cell_edges(v, level, (double)x, (double)y, ex, ey);
            ncross = 0;
            {
                int e;
                for (e = 0; e < 4; e++) {
                    if (ex[e] >= 0.0) crossed[ncross++] = e;
                }
            }
            if (ncross == 2) {
                emit(ex[crossed[0]], ey[crossed[0]],
                     ex[crossed[1]], ey[crossed[1]], fn, ctx);
            } else if (ncross == 4) {
                /* Saddle: pair edges by the centre average. */
                double c = (v[0] + v[1] + v[2] + v[3]) * 0.25;
                if ((c < level) == (v[0] < level)) {
                    /* centre joins corner 0/1 side: pair 3-0 and 1-2. */
                    emit(ex[3], ey[3], ex[0], ey[0], fn, ctx);
                    emit(ex[1], ey[1], ex[2], ey[2], fn, ctx);
                } else {
                    emit(ex[0], ey[0], ex[1], ey[1], fn, ctx);
                    emit(ex[2], ey[2], ex[3], ey[3], fn, ctx);
                }
            }
            /* ncross == 0: no crossing; odd counts are impossible here. */
        }
    }
}

typedef struct {
    long n;
} count_ctx_t;

static void count_seg(double x0, double y0, double x1, double y1, void *ctx) {
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    ((count_ctx_t *)ctx)->n++;
}

long ng_contour_count(const double *data, int nx, int ny, double level) {
    count_ctx_t ctx;
    ctx.n = 0;
    ng_contour_each(data, nx, ny, level, count_seg, &ctx);
    return ctx.n;
}
