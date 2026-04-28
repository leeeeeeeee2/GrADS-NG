#ifndef NG_CANVAS_H
#define NG_CANVAS_H

/*
 * ng/src/render/canvas.h
 * Virtual canvas — the renderer's public interface.
 *
 * All drawing goes through this API.  The backend (Cairo image surface,
 * future Metal/Skia/WebGPU) is selected at build time or at runtime.
 * X11 is never referenced here.
 *
 * Coordinate system: floating-point inches, origin bottom-left,
 * matching original GrADS page-coordinate conventions.
 */

#include <stdint.h>
#include <stddef.h>

typedef struct ng_canvas ng_canvas_t;

/* RGBA color, components 0–255 */
typedef struct { uint8_t r, g, b, a; } ng_color_t;

/* Standard GrADS default palette (indices 0–15) */
extern const ng_color_t NG_PALETTE[16];

/* Create / destroy */
ng_canvas_t *ng_canvas_new(double width_in, double height_in, int dpi);
void         ng_canvas_free(ng_canvas_t *c);

/* Frame control */
void ng_canvas_clear(ng_canvas_t *c, ng_color_t bg);
void ng_canvas_new_frame(ng_canvas_t *c);    /* clear to background */

/* Drawing primitives — all coordinates in inches */
void ng_canvas_set_color(ng_canvas_t *c, ng_color_t col);
void ng_canvas_set_line_width(ng_canvas_t *c, double pts);
void ng_canvas_move_to(ng_canvas_t *c, double x, double y);
void ng_canvas_line_to(ng_canvas_t *c, double x, double y);
void ng_canvas_stroke(ng_canvas_t *c);
void ng_canvas_fill_rect(ng_canvas_t *c,
                         double x, double y, double w, double h);
void ng_canvas_fill_polygon(ng_canvas_t *c,
                            const double *xy, int npts); /* xy: x0,y0,x1,y1,… */

/* Text */
void ng_canvas_draw_text(ng_canvas_t *c, double x, double y,
                         const char *text, double size_pts, double rot_deg);

/* Clip region */
void ng_canvas_set_clip(ng_canvas_t *c,
                        double x, double y, double w, double h);
void ng_canvas_reset_clip(ng_canvas_t *c);

/* Output */
int ng_canvas_write_png(ng_canvas_t *c, const char *path);
int ng_canvas_write_svg(ng_canvas_t *c, const char *path);

/* Raw RGBA access (for display in a GUI window later) */
const uint8_t *ng_canvas_rgba(ng_canvas_t *c, int *width, int *height);

#endif /* NG_CANVAS_H */
