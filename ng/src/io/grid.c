/*
 * ng/src/io/grid.c
 * GrADS sequential/stream binary data reader implementation.
 */

#include "grid.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

struct ng_grid {
    const ng_ctl_t *desc; /* borrowed; the caller keeps it alive. */
    ng_mmap_t *map;
    const unsigned char *base;
    size_t size;
    size_t slice_words;   /* nx*ny */
    size_t slice_bytes;   /* slice_words*4 + markers */
    int swap;             /* swap each 4-byte word after read */
    size_t *var_base;     /* file offset of each variable's first slice */
};

static char g_err[512];

static const char *fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
    return g_err;
}

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static int host_is_little_endian(void) {
    const uint16_t probe = 1;
    return *(const unsigned char *)&probe == 1;
}

static int var_levels(const ng_ctl_t *c, int var) {
    int n = c->vars[var].nlevels;
    return n == 0 ? 1 : n;
}

ng_grid_t *ng_grid_open(const ng_ctl_t *ctl, const char **err_out) {
    ng_grid_t *g;
    char path[NG_CTL_PATHLEN * 2];
    size_t size = 0;
    size_t off;
    int v;

    if (err_out) *err_out = NULL;
    if (!ctl) {
        if (err_out) *err_out = fail("NULL descriptor");
        return NULL;
    }
    if (ctl->dtype != NG_DTYPE_BINARY) {
        if (err_out)
            *err_out = fail("only BINARY datasets can be read "
                            "(descriptor dtype is %d)", (int)ctl->dtype);
        return NULL;
    }
    if (ctl->template) {
        if (err_out)
            *err_out = fail("templated datasets need the time axis (M3/M4)");
        return NULL;
    }
    if (ctl->nvars <= 0 || ctl->nx <= 0 || ctl->ny <= 0 || ctl->nt <= 0) {
        if (err_out) *err_out = fail("descriptor has no data to read");
        return NULL;
    }

    /* Resolve the data path: ^file is relative to the descriptor dir. */
    if (ctl->dset[0] == '^')
        snprintf(path, sizeof(path), "%s/%s", ctl->ctldir, ctl->dset + 1);
    else
        snprintf(path, sizeof(path), "%s", ctl->dset);

    g = calloc(1, sizeof(*g));
    if (!g) {
        if (err_out) *err_out = fail("out of memory");
        return NULL;
    }
    g->desc = ctl;
    g->map = ng_mmap_open(path, &size);
    if (!g->map) {
        if (err_out) *err_out = fail("cannot open data file \"%s\"", path);
        free(g);
        return NULL;
    }
    g->base = (const unsigned char *)ng_mmap_data(g->map);
    g->size = size;

    g->slice_words = (size_t)ctl->nx * (size_t)ctl->ny;
    g->slice_bytes =
        g->slice_words * 4 + (ctl->sequential ? 8 : 0);
    /* Classic GrADS files are big-endian; byteswapped means the opposite. */
    g->swap = (host_is_little_endian() != 0) != (ctl->byteswap != 0);

    g->var_base = malloc((size_t)ctl->nvars * sizeof(*g->var_base));
    if (!g->var_base) {
        if (err_out) *err_out = fail("out of memory");
        ng_mmap_close(g->map);
        free(g);
        return NULL;
    }
    off = 0;
    for (v = 0; v < ctl->nvars; v++) {
        g->var_base[v] = off;
        off += (size_t)ctl->nt * (size_t)var_levels(ctl, v) * g->slice_bytes;
    }
    if (off > size) {
        if (err_out)
            *err_out = fail("data file \"%s\" holds %lu bytes but the "
                            "descriptor needs %lu", path,
                            (unsigned long)size, (unsigned long)off);
        free(g->var_base);
        ng_mmap_close(g->map);
        free(g);
        return NULL;
    }
    return g;
}

void ng_grid_close(ng_grid_t *g) {
    if (!g) return;
    ng_mmap_close(g->map);
    free(g->var_base);
    free(g);
}

int ng_grid_var_levels(const ng_grid_t *g, int var) {
    if (!g || var < 0 || var >= g->desc->nvars) return -1;
    return var_levels(g->desc, var);
}

int ng_grid_read_slice(ng_grid_t *g, int var, int t, int z, double *out,
                       const char **err_out) {
    const ng_ctl_t *c;
    size_t rec, pos, i;
    const unsigned char *p;

    if (err_out) *err_out = NULL;
    if (!g || !out) {
        if (err_out) *err_out = fail("NULL grid or output buffer");
        return -1;
    }
    c = g->desc;
    if (var < 0 || var >= c->nvars) {
        if (err_out)
            *err_out = fail("variable %d out of range (0..%d)", var,
                            c->nvars - 1);
        return -1;
    }
    if (t < 0 || t >= c->nt) {
        if (err_out)
            *err_out = fail("time step %d out of range (0..%d)", t, c->nt - 1);
        return -1;
    }
    if (z < 0 || z >= var_levels(c, var)) {
        if (err_out)
            *err_out = fail("level %d out of range for \"%s\" (0..%d)", z,
                            c->vars[var].name, var_levels(c, var) - 1);
        return -1;
    }

    /* Record order: time outer, level inner. */
    rec = (size_t)t * (size_t)var_levels(c, var) + (size_t)z;
    pos = g->var_base[var] + rec * g->slice_bytes;
    if (c->sequential) {
        uint32_t head, tail, expect;
        memcpy(&head, g->base + pos, 4);
        if (g->swap) head = bswap32(head);
        expect = (uint32_t)(g->slice_words * 4);
        if (head != expect) {
            if (err_out)
                *err_out = fail("record marker %u, expected %u "
                                "(wrong format or corrupt file)",
                                head, expect);
            return -1;
        }
        pos += 4;
        p = g->base + pos + g->slice_words * 4;
        memcpy(&tail, p, 4);
        if (g->swap) tail = bswap32(tail);
        if (tail != expect) {
            if (err_out)
                *err_out = fail("trailing record marker %u, expected %u",
                                tail, expect);
            return -1;
        }
    }
    p = g->base + pos;
    for (i = 0; i < g->slice_words; i++) {
        uint32_t w;
        float f;
        memcpy(&w, p + i * 4, 4);
        if (g->swap) w = bswap32(w);
        memcpy(&f, &w, 4);
        out[i] = (double)f;
    }
    return 0;
}
