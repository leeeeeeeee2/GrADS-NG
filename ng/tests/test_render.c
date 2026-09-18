/* Unit tests for the shaded-grid raster backend (ng/src/render).
 * Byte-determinism is the contract: identical input yields identical
 * files, which is what makes rendered output CTest-comparable. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shade.h"
#include "contour.h"
#include "png.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            printf("FAIL "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

int main(void) {
    /* Shade mapping. */
    CHECK(ng_shade_gray(0.0, 0.0, 100.0) == 0, "lo maps to 0");
    CHECK(ng_shade_gray(100.0, 0.0, 100.0) == 255, "hi maps to 255");
    CHECK(ng_shade_gray(50.0, 0.0, 100.0) == 128, "mid maps to 128");
    CHECK(ng_shade_gray(-5.0, 0.0, 100.0) == 0, "below range clamps");
    CHECK(ng_shade_gray(1e30, 0.0, 100.0) == 255, "above range clamps");
    CHECK(ng_shade_gray(5.0, 5.0, 5.0) == 128, "degenerate range is gray");
    CHECK(ng_shade_gray(NAN, 0.0, 100.0) == -1, "missing maps to -1");

    /* Exact PPM bytes: 2x2 grid, y = 0 row first in memory, missing at
     * (x=1, y=1). File rows run top-first, so the first pixel row covers
     * grid y = 1: shade(20) = white, then magenta for the missing cell. */
    {
        double data[4] = {0.0, 10.0, 20.0, 30.0};  /* row0: 0 10; row1: 20 NaN */
        const char *path = "test_render_tmp.ppm";
        FILE *fp;
        unsigned char px[3];
        char hdr[64];
        int ok = 1, i;

        data[3] = NAN;
        CHECK(ng_shade_write_ppm(path, "t", data, 2, 2) == 0,
              "2x2 write succeeds");
        fp = fopen(path, "rb");
        CHECK(fp != NULL, "2x2 file readable");
        if (fp) {
            /* Header lines compared exactly; pixel data starts wherever
             * the maxval line ends (no hardcoded offset). */
            long base;
            hdr[0] = '\0';
            CHECK(fgets(hdr, sizeof(hdr), fp) != NULL &&
                  strcmp(hdr, "P6\n") == 0, "magic line exact");
            CHECK(fgets(hdr, sizeof(hdr), fp) != NULL &&
                  strcmp(hdr, "# grads-ng shaded t\n") == 0,
                  "comment names the display");
            CHECK(fgets(hdr, sizeof(hdr), fp) != NULL &&
                  strcmp(hdr, "20 20\n") == 0, "dims line exact");
            CHECK(fgets(hdr, sizeof(hdr), fp) != NULL &&
                  strcmp(hdr, "255\n") == 0, "maxval line exact");
            base = ftell(fp);
            /* First pixel row: grid y = 1. Each cell is 10px wide:
             * pixels 0-9 are shade(20) = white, 10-19 are NaN = magenta. */
            if (fread(px, 1, 3, fp) != 3) ok = 0;
            /* shade(20) on [0,20] = 255. */
            ok = ok && px[0] == 255 && px[1] == 255 && px[2] == 255;
            for (i = 0; i < 9; i++) {
                if (fread(px, 1, 3, fp) != 3) {
                    ok = 0;
                    break;
                }
            }
            if (fread(px, 1, 3, fp) != 3) ok = 0;
            ok = ok && px[0] == 255 && px[1] == 0 && px[2] == 255;
            /* Last pixel row: 10 black (grid x = 0) then 10 gray 128. */
            if (fseek(fp, base + 19 * 20 * 3, SEEK_SET) != 0) ok = 0;
            if (fread(px, 1, 3, fp) != 3) ok = 0;
            ok = ok && px[0] == 0 && px[1] == 0 && px[2] == 0;
            for (i = 0; i < 9; i++) {
                if (fread(px, 1, 3, fp) != 3) {
                    ok = 0;
                    break;
                }
            }
            if (fread(px, 1, 3, fp) != 3) ok = 0;
            ok = ok && px[0] == 128 && px[1] == 128 && px[2] == 128;
            CHECK(ok, "pixel rows flip y and map shades");
            fclose(fp);
        }
        remove(path);
    }

    /* Failure paths. */
    {
        double data[1] = {1.0};
        CHECK(ng_shade_write_ppm(NULL, "t", data, 1, 1) != 0,
              "NULL path fails");
        CHECK(ng_shade_write_ppm("x.ppm", "t", NULL, 1, 1) != 0,
              "NULL data fails");
        CHECK(ng_shade_write_ppm("x.ppm", "t", data, 0, 1) != 0,
              "zero width fails");
        CHECK(ng_shade_write_ppm("/nonexistent-dir-xyz/f.ppm", "t", data,
                                 1, 1) != 0,
              "unwritable path fails");
    }

    /* Contour auto levels. */
    {
        double lv[NG_CONTOUR_MAXLEVELS];
        int n;
        n = ng_contour_levels(0.0, 100.0, lv, NG_CONTOUR_MAXLEVELS);
        CHECK(n == 11 && lv[0] == 0.0 && lv[10] == 100.0,
              "0..100 gives 11 levels step 10 (got %d)", n);
        n = ng_contour_levels(-3.45, 43.73, lv, NG_CONTOUR_MAXLEVELS);
        CHECK(n == 9 && lv[0] == 0.0 && lv[8] == 40.0,
              "-3.45..43.73 gives 0..40 step 5 (got %d %g %g)",
              n, n > 0 ? lv[0] : 0.0, n > 0 ? lv[n - 1] : 0.0);
        CHECK(ng_contour_levels(5.0, 5.0, lv, NG_CONTOUR_MAXLEVELS) == 0,
              "degenerate range gives no levels");
        CHECK(ng_contour_levels(NAN, 1.0, lv, NG_CONTOUR_MAXLEVELS) == 0,
              "NaN bound gives no levels");
        CHECK(ng_contour_levels(0.0, 1.0, NULL, 8) == 0,
              "NULL buffer gives no levels");
    }

    /* Marching squares on the plane v(x,y) = x: level 1.5 is the
     * vertical line gx = 1.5, one segment per crossed cell row. */
    {
        double plane[12];
        int x, y;
        for (y = 0; y < 3; y++)
            for (x = 0; x < 4; x++) plane[y * 4 + x] = (double)x;
        CHECK(ng_contour_count(plane, 4, 3, 1.5) == 2,
              "plane level 1.5 traces 2 segments");
        plane[0 * 4 + 2] = NAN; /* knock out cell (1,0) */
        CHECK(ng_contour_count(plane, 4, 3, 1.5) == 1,
              "NaN corner cell emits nothing");
        CHECK(ng_contour_count(plane, 1, 3, 1.5) == 0,
              "degenerate grid traces nothing");
    }

    /* Saddle cell resolves to two segments, never zero or one.
     * Corner order is v0=(0,0) v1=(1,0) v2=(1,1) v3=(0,1), so the data
     * layout is {v0, v1, v3, v2}. */
    {
        double checker[4] = {0.0, 1.0, 1.0, 0.0};
        CHECK(ng_contour_count(checker, 2, 2, 0.5) == 2,
              "saddle gives 2 segments");
    }

    /* RGB raster + overlay. */
    {
        double ramp[6] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0}; /* 3x2 */
        unsigned char *rgb;
        int w = 0, h = 0, nlev, black = 0, i;
        ng_shade_raster_size(3, 2, &w, &h);
        CHECK(w == 30 && h == 20, "raster is 10px per cell");
        rgb = ng_shade_render_rgb(ramp, 3, 2);
        CHECK(rgb != NULL, "raster renders");
        if (rgb) {
            /* y=0 row is the bottom image row: shade(0)=black. */
            CHECK(rgb[(h - 1) * w * 3] == 0, "shade mapping shared with PPM");
            /* Every image row carries its own data row (stride
             * regression: rows once overwrote each other). Ramp
             * 0..5 over lo=0,hi=5 gives shades 0,51,102,153,204,255. */
            {
                static const unsigned char want[6] = {0, 51, 102, 153, 204, 255};
                int yy, okrows = 1;
                for (yy = 0; yy < h && okrows; yy++) {
                    int gy = 1 - yy / NG_SHADE_CELL; /* grid row for image row */
                    int xx;
                    for (xx = 0; xx < w && okrows; xx++) {
                        int gx = xx / NG_SHADE_CELL;
                        unsigned char wantpx = want[gy * 3 + gx];
                        unsigned char gotpx =
                            rgb[((size_t)yy * w + xx) * 3];
                        if (gotpx != wantpx) okrows = 0;
                    }
                }
                CHECK(okrows, "all raster rows map to their data row");
            }
            nlev = ng_shade_overlay_contours(rgb, w, h, ramp, 3, 2);
            CHECK(nlev > 0, "overlay draws levels (%d)", nlev);
            for (i = 0; i < w * h; i++) {
                if (rgb[3 * i] == NG_CONTOUR_R &&
                    rgb[3 * i + 1] == NG_CONTOUR_G &&
                    rgb[3 * i + 2] == NG_CONTOUR_B)
                    black++;
            }
            CHECK(black > 0, "overlay leaves black pixels (%d)", black);
            free(rgb);
        }
        {
            double missing[4] = {NAN, NAN, NAN, NAN};
            unsigned char *m = ng_shade_render_rgb(missing, 2, 2);
            CHECK(m != NULL, "all-missing raster renders");
            if (m) {
                CHECK(ng_shade_overlay_contours(m, 20, 20, missing, 2, 2) == 0,
                      "all-missing overlay draws nothing");
                CHECK(m[0] == 255 && m[1] == 0 && m[2] == 255,
                      "all-missing stays magenta");
                free(m);
            }
        }
        CHECK(ng_shade_render_rgb(NULL, 2, 2) == NULL, "NULL data fails");
        CHECK(ng_shade_overlay_contours(NULL, 0, 0, ramp, 3, 2) == 0,
              "NULL raster draws nothing");
    }

    /* PNG checksums against published vectors. */
    {
        CHECK(ng_png_crc32(NULL, 0) == 0, "crc32 of empty is 0");
        CHECK(ng_png_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u,
              "crc32 test vector");
        CHECK(ng_png_adler32(NULL, 0) == 1, "adler32 of empty is 1");
        CHECK(ng_png_adler32((const unsigned char *)"Wikipedia", 9) == 0x11E60398u,
              "adler32 test vector");
    }

    /* PNG framing: 2x1 red/blue image; every chunk CRC recomputed. */
    {
        static const unsigned char px[6] = {255, 0, 0, 0, 0, 255};
        const char *path = "test_render_tmp.png";
        FILE *fp;
        unsigned char sig[8];
        int ok = 1;
        CHECK(ng_png_write_rgb(path, "t", px, 2, 1) == 0, "2x1 write succeeds");
        fp = fopen(path, "rb");
        CHECK(fp != NULL, "png readable");
        if (fp) {
            unsigned char len[4], type[4];
            if (fread(sig, 1, 8, fp) != 8) ok = 0;
            ok = ok && memcmp(sig, "\211PNG\r\n\032\n", 8) == 0;
            {
                int seen_ihdr = 0, chunk_no = 0;
                for (;;) {
                    unsigned char *data, *both;
                    unsigned long n;
                    unsigned char crcbuf[4];
                    uint32_t want, got;
                    if (fread(len, 1, 4, fp) != 4) {
                        ok = 0;
                        break;
                    }
                    n = ((unsigned long)len[0] << 24) |
                        ((unsigned long)len[1] << 16) |
                        ((unsigned long)len[2] << 8) | len[3];
                    if (fread(type, 1, 4, fp) != 4) {
                        ok = 0;
                        break;
                    }
                    data = malloc(n > 0 ? n : 1);
                    both = malloc(4 + (n > 0 ? n : 0));
                    if (!data || !both) {
                        ok = 0;
                        free(data);
                        free(both);
                        break;
                    }
                    if (n && fread(data, 1, n, fp) != n) ok = 0;
                    if (fread(crcbuf, 1, 4, fp) != 4) ok = 0;
                    want = ((uint32_t)crcbuf[0] << 24) |
                           ((uint32_t)crcbuf[1] << 16) |
                           ((uint32_t)crcbuf[2] << 8) | crcbuf[3];
                    memcpy(both, type, 4);
                    if (n) memcpy(both + 4, data, n);
                    got = ng_png_crc32(both, 4 + n);
                    ok = ok && (got == want);
                    if (chunk_no == 0) {
                        /* First chunk is IHDR promising 2x1 8-bit RGB. */
                        ok = ok && memcmp(type, "IHDR", 4) == 0 && n == 13 &&
                             data[0] == 0 && data[3] == 2 && /* w = 2 */
                             data[4] == 0 && data[7] == 1 && /* h = 1 */
                             data[8] == 8 && data[9] == 2;
                        seen_ihdr = 1;
                    }
                    free(data);
                    free(both);
                    chunk_no++;
                    if (memcmp(type, "IEND", 4) == 0) break;
                    if (chunk_no > 16) {
                        ok = 0;
                        break;
                    }
                }
                ok = ok && seen_ihdr;
            }
            fclose(fp);
            CHECK(ok, "png chunks frame with valid CRCs");
            remove(path);
        }
        CHECK(ng_png_write_rgb(NULL, "t", px, 2, 1) != 0, "NULL path fails");
        CHECK(ng_png_write_rgb("x.png", "t", NULL, 2, 1) != 0, "NULL rgb fails");
        CHECK(ng_png_write_rgb("/nonexistent-dir-xyz/f.png", "t", px, 2, 1) != 0,
              "unwritable path fails");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
