/* gx_virtual.h - Virtual display backend for GrADS-NG */

#ifndef GX_VIRTUAL_H
#define GX_VIRTUAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual display context */
typedef struct {
    int width;
    int height;
    uint32_t *buffer;  /* RGBA buffer */
    int current_color;
    int line_width;
    double current_x, current_y;
    int clip_xmin, clip_ymin, clip_xmax, clip_ymax;
} gx_virtual_t;

/* Initialize virtual display */
int gx_virtual_init(gx_virtual_t *ctx, int width, int height);

/* Cleanup virtual display */
void gx_virtual_cleanup(gx_virtual_t *ctx);

/* Get buffer for external access */
uint32_t *gx_virtual_get_buffer(gx_virtual_t *ctx, int *width, int *height);

/* GX-compatible interface functions */
void gx_virtual_gxdbgn(gx_virtual_t *ctx, double xsz, double ysz);
void gx_virtual_gxdend(gx_virtual_t *ctx);
void gx_virtual_gxdfrm(gx_virtual_t *ctx, int clear);
void gx_virtual_gxdcol(gx_virtual_t *ctx, int color);
void gx_virtual_gxdwid(gx_virtual_t *ctx, int width);
void gx_virtual_gxdmov(gx_virtual_t *ctx, double x, double y);
void gx_virtual_gxddrw(gx_virtual_t *ctx, double x, double y);
void gx_virtual_gxdrec(gx_virtual_t *ctx, double x1, double y1, double x2, double y2);
void gx_virtual_gxdsgl(gx_virtual_t *ctx);
void gx_virtual_gxddbl(gx_virtual_t *ctx);
void gx_virtual_gxdswp(gx_virtual_t *ctx);
void gx_virtual_gxdfil(gx_virtual_t *ctx, double *xy, int num);
void gx_virtual_gxdxsz(gx_virtual_t *ctx, int w, int h);

/* Export functions */
int gx_virtual_export_png(gx_virtual_t *ctx, const char *filename);
int gx_virtual_export_svg(gx_virtual_t *ctx, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* GX_VIRTUAL_H */