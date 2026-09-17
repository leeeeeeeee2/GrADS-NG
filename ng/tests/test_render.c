/* Unit tests for the shaded-grid raster backend (ng/src/render).
 * Byte-determinism is the contract: identical input yields identical
 * files, which is what makes rendered output CTest-comparable. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shade.h"

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

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
