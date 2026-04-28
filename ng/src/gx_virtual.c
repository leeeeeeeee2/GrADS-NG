/* gx_virtual.c - Virtual display backend implementation */

#include "gx_virtual.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <png.h>
#include <cairo.h>
#include <cairo-svg.h>

/* Color palette - matches original GrADS colors (RGBA) */
static uint32_t gx_colors[16] = {
    0xFF000000,  /* 0 - black */
    0xFFFFFFFF,  /* 1 - white */
    0xFFFF0000,  /* 2 - red */
    0xFF00FF00,  /* 3 - green */
    0xFF0000FF,  /* 4 - blue */
    0xFF00FFFF,  /* 5 - cyan */
    0xFFFF00FF,  /* 6 - magenta */
    0xFFFFFF00,  /* 7 - yellow */
    0xFFFFA500,  /* 8 - orange */
    0xFF800080,  /* 9 - purple */
    0xFF98FB98,  /* 10 - yellow-green */
    0xFFADD8E6,  /* 11 - light blue */
    0xFFFFD700,  /* 12 - gold/yellow-orange */
    0xFF008080,  /* 13 - teal/blue-green */
    0xFF8A2BE2,  /* 14 - blue-violet */
    0xFF808080   /* 15 - grey */
};

/* Initialize virtual display */
int gx_virtual_init(gx_virtual_t *ctx, int width, int height) {
    if (!ctx || width <= 0 || height <= 0) {
        return -1;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->buffer = (uint32_t *)malloc(width * height * sizeof(uint32_t));

    if (!ctx->buffer) {
        return -1;
    }

    memset(ctx->buffer, 0, width * height * sizeof(uint32_t));

    ctx->current_color = 0;
    ctx->line_width = 1;
    ctx->current_x = 0.0;
    ctx->current_y = 0.0;
    ctx->clip_xmin = 0;
    ctx->clip_ymin = 0;
    ctx->clip_xmax = width;
    ctx->clip_ymax = height;

    return 0;
}

/* Cleanup virtual display */
void gx_virtual_cleanup(gx_virtual_t *ctx) {
    if (ctx && ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
}

/* Get buffer for external access */
uint32_t *gx_virtual_get_buffer(gx_virtual_t *ctx, int *width, int *height) {
    if (ctx && ctx->buffer) {
        *width = ctx->width;
        *height = ctx->height;
        return ctx->buffer;
    }
    return NULL;
}

/* Set pixel in buffer (with bounds checking) */
static void set_pixel(gx_virtual_t *ctx, int x, int y, uint32_t color) {
    if (!ctx || !ctx->buffer) return;
    if (x < ctx->clip_xmin || x >= ctx->clip_xmax ||
        y < ctx->clip_ymin || y >= ctx->clip_ymax) return;

    ctx->buffer[y * ctx->width + x] = color;
}

/* Get current color */
static uint32_t get_current_color(gx_virtual_t *ctx) {
    if (!ctx) return gx_colors[0];
    int color_idx = ctx->current_color;
    if (color_idx < 0 || color_idx > 15) color_idx = 0;
    return gx_colors[color_idx];
}

/* GX Interface Functions */

void gx_virtual_gxdbgn(gx_virtual_t *ctx, double xsz, double ysz) {
    /* Initialize virtual display */
    int width = (int)(xsz + 0.5);
    int height = (int)(ysz + 0.5);
    if (width < 100) width = 800;
    if (height < 100) height = 600;

    gx_virtual_cleanup(ctx);
    gx_virtual_init(ctx, width, height);
    printf("Virtual display initialized: %dx%d\n", width, height);
}

void gx_virtual_gxdend(gx_virtual_t *ctx) {
    /* Cleanup */
    gx_virtual_cleanup(ctx);
}

void gx_virtual_gxdfrm(gx_virtual_t *ctx, int clear) {
    /* New frame - clear buffer if requested */
    if (ctx && ctx->buffer && clear) {
        memset(ctx->buffer, 0, ctx->width * ctx->height * sizeof(uint32_t));
    }
}

void gx_virtual_gxdcol(gx_virtual_t *ctx, int color) {
    /* Set current color */
    if (ctx) {
        ctx->current_color = color;
    }
}

void gx_virtual_gxdwid(gx_virtual_t *ctx, int width) {
    /* Set line width */
    if (ctx) {
        ctx->line_width = width;
    }
}

void gx_virtual_gxdmov(gx_virtual_t *ctx, double x, double y) {
    /* Move pen */
    if (ctx) {
        ctx->current_x = x;
        ctx->current_y = y;
    }
}

void gx_virtual_gxddrw(gx_virtual_t *ctx, double x, double y) {
    /* Draw line from current position to (x,y) */
    if (!ctx) return;

    uint32_t color = get_current_color(ctx);

    /* Simple line drawing - Bresenham algorithm */
    int x0 = (int)(ctx->current_x + 0.5);
    int y0 = (int)(ctx->current_y + 0.5);
    int x1 = (int)(x + 0.5);
    int y1 = (int)(y + 0.5);

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(ctx, x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    ctx->current_x = x;
    ctx->current_y = y;
}

void gx_virtual_gxdrec(gx_virtual_t *ctx, double x1, double y1, double x2, double y2) {
    /* Draw filled rectangle */
    if (!ctx) return;

    uint32_t color = get_current_color(ctx);

    int ix1 = (int)(x1 + 0.5);
    int iy1 = (int)(y1 + 0.5);
    int ix2 = (int)(x2 + 0.5);
    int iy2 = (int)(y2 + 0.5);

    /* Ensure x1 < x2, y1 < y2 */
    if (ix1 > ix2) { int t = ix1; ix1 = ix2; ix2 = t; }
    if (iy1 > iy2) { int t = iy1; iy1 = iy2; iy2 = t; }

    for (int y = iy1; y <= iy2; y++) {
        for (int x = ix1; x <= ix2; x++) {
            set_pixel(ctx, x, y, color);
        }
    }
}

void gx_virtual_gxdsgl(gx_virtual_t *ctx) {
    /* Set single buffer mode - no-op for virtual */
    (void)ctx;
}

void gx_virtual_gxddbl(gx_virtual_t *ctx) {
    /* Set double buffer mode - no-op for virtual */
    (void)ctx;
}

void gx_virtual_gxdswp(gx_virtual_t *ctx) {
    /* Swap buffers - no-op for virtual */
    (void)ctx;
}

void gx_virtual_gxdfil(gx_virtual_t *ctx, double *xy, int num) {
    /* Hardware polygon fill - simplified bounding box fill */
    if (!ctx || num < 6 || !xy) return;

    uint32_t color = get_current_color(ctx);

    /* Find bounding box */
    int min_x = ctx->width, max_x = 0;
    int min_y = ctx->height, max_y = 0;

    for (int i = 0; i < num; i += 2) {
        int x = (int)(xy[i] + 0.5);
        int y = (int)(xy[i+1] + 0.5);
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    /* Fill bounding box */
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            set_pixel(ctx, x, y, color);
        }
    }
}

void gx_virtual_gxdxsz(gx_virtual_t *ctx, int w, int h) {
    /* Resize virtual display */
    if (ctx && (w != ctx->width || h != ctx->height)) {
        gx_virtual_cleanup(ctx);
        gx_virtual_init(ctx, w, h);
    }
}

/* PNG export implementation */
int gx_virtual_export_png(gx_virtual_t *ctx, const char *filename) {
    if (!ctx || !ctx->buffer || !filename) {
        return -1;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open PNG file for writing: %s\n", filename);
        return -1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);

    // Set PNG header info
    png_set_IHDR(png, info, ctx->width, ctx->height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    // Convert RGBA to row pointers (PNG expects top-to-bottom, our buffer is top-to-bottom)
    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * ctx->height);
    if (!row_pointers) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < ctx->height; y++) {
        row_pointers[y] = (png_bytep)(ctx->buffer + y * ctx->width * 4);
    }

    png_write_image(png, row_pointers);
    png_write_end(png, NULL);

    // Cleanup
    free(row_pointers);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    printf("PNG exported to: %s (%dx%d)\n", filename, ctx->width, ctx->height);
    return 0;
}

int gx_virtual_export_svg(gx_virtual_t *ctx, const char *filename) {
    /* TODO: Implement SVG export */
    printf("SVG export not yet implemented: %s\n", filename);
    return -1;
}