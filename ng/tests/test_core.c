/* test_core.c - Unit tests for session lifecycle and file metadata accessors.
 *
 * Exercises the M1 vertical slice: init, open, dims/nvars/type accessors,
 * out-of-order close (swap-remove), destroy-with-open-files, and NULL safety.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "grads_ng.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); failures++; } \
        else { printf("ok   " __VA_ARGS__); printf("\n"); } \
    } while (0)

int main(int argc, char** argv) {
    const char* ctl = (argc > 1) ? argv[1] : "data/sample.ctl";
    grads_ng_config_t config;
    grads_ng_session_t* session;
    grads_ng_file_t* f1;
    grads_ng_file_t* f2;
    int nx, ny, nz, nt;

    memset(&config, 0, sizeof(config));
    config.headless = 1;
    config.batch_mode = 1;

    session = grads_ng_init(&config);
    CHECK(session != NULL, "init returns a session");

    CHECK(grads_ng_open(NULL, ctl) == NULL, "open with NULL session fails");
    CHECK(grads_ng_open(session, NULL) == NULL, "open with NULL path fails");
    CHECK(grads_ng_open(session, "/nonexistent/nope.ctl") == NULL,
          "open of missing file fails");

    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "open sample.ctl");
    if (f1) {
        CHECK(grads_ng_file_dims(f1, &nx, &ny, &nz, &nt) == 0, "dims accessor ok");
        CHECK(nx == 4 && ny == 3 && nz == 2 && nt == 2,
              "dims are 4x3x2x2 (got %dx%dx%dx%d)", nx, ny, nz, nt);
        CHECK(grads_ng_file_nvars(f1) == 2, "nvars is 2");
        CHECK(grads_ng_file_type(f1) == 0, "default type is grid");
    }

    /* Open a second file, then close out of order: destroy must not crash. */
    f2 = grads_ng_open(session, ctl);
    CHECK(f2 != NULL, "open sample.ctl a second time");
    grads_ng_close(f1);
    CHECK(1, "close first file while second is open");
    grads_ng_close(f2);
    CHECK(1, "close second file");

    /* Variable layer: lookup, metadata, and slice reads. */
    {
        grads_ng_file_t *fv = grads_ng_open(session, ctl);
        grads_ng_var_t *tsfc, *hgt;
        double out[12];
        char emsg[256] = {0};
        CHECK(fv != NULL, "open sample.ctl for variable tests");
        if (fv) {
            tsfc = grads_ng_get_var(fv, "tsfc");
            CHECK(tsfc != NULL, "lookup tsfc");
            CHECK(grads_ng_get_var(fv, "TSFC") == tsfc,
                  "lookup is case-insensitive");
            hgt = grads_ng_get_var(fv, "hgt");
            CHECK(hgt != NULL && grads_ng_var_levels(hgt) == 2,
                  "hgt has 2 levels");
            CHECK(tsfc && grads_ng_var_levels(tsfc) == 1,
                  "tsfc has 1 level");
            CHECK(tsfc && strcmp(grads_ng_var_name(tsfc), "tsfc") == 0,
                  "var name accessor");
            CHECK(strcmp(grads_ng_file_varname(fv, 1), "hgt") == 0,
                  "file varname by index");
            CHECK(grads_ng_file_varname(fv, 2) == NULL,
                  "file varname out of range is NULL");
            CHECK(grads_ng_get_var(fv, "nope") == NULL,
                  "unknown variable is NULL");
            CHECK(grads_ng_get_var(NULL, "tsfc") == NULL &&
                  grads_ng_get_var(fv, NULL) == NULL,
                  "NULL lookup args are NULL");
            if (tsfc) {
                CHECK(grads_ng_var_slice(tsfc, 0, 0, out, emsg,
                                         sizeof(emsg)) == 0,
                      "read tsfc t=1 z=1 (%s)",
                      emsg[0] ? emsg : "ok");
                CHECK(out[0] == 0.25 && out[11] == 23.25,
                      "tsfc slice corners byte-exact");
                CHECK(grads_ng_var_slice(tsfc, 1, 0, out, emsg,
                                         sizeof(emsg)) == 0,
                      "read tsfc t=2 (%s)", emsg[0] ? emsg : "ok");
                CHECK(out[11] == (double)(float)grads_ng_var_undef(tsfc),
                      "UNDEF sentinel passes through");
                CHECK(grads_ng_var_slice(tsfc, 0, 1, out, emsg,
                                         sizeof(emsg)) != 0 && emsg[0],
                      "surface var z=2 rejected with message");
                CHECK(grads_ng_var_slice(tsfc, 5, 0, out, emsg,
                                         sizeof(emsg)) != 0,
                      "time past end rejected");
                CHECK(grads_ng_var_slice(NULL, 0, 0, out, emsg,
                                         sizeof(emsg)) != 0,
                      "NULL var rejected");
            }
            grads_ng_close(fv);
            CHECK(1, "close variable-test file");
        }
    }

    /* X/Y window defaults to the full grid and validates strictly. */
    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "open sample.ctl for window tests");
    if (f1) {
        int x1, x2, y1, y2;
        char werr[256];
        CHECK(grads_ng_file_window(f1, &x1, &x2, &y1, &y2) == 0 &&
              x1 == 0 && x2 == 3 && y1 == 0 && y2 == 2,
              "window defaults to full grid");
        CHECK(grads_ng_file_select_xy(f1, 0, 1, 1, 2, werr,
                                      sizeof(werr)) == 0,
              "select x=1..2 y=2..3 (%s)", werr[0] ? werr : "ok");
        CHECK(grads_ng_file_window(f1, &x1, &x2, &y1, &y2) == 0 &&
              x1 == 0 && x2 == 1 && y1 == 1 && y2 == 2,
              "window reports selection");
        CHECK(grads_ng_file_select_xy(f1, 1, 0, 0, 2, werr,
                                      sizeof(werr)) != 0 &&
              strstr(werr, "ascend") != NULL,
              "reversed x rejected (%s)", werr);
        CHECK(grads_ng_file_select_xy(f1, 0, 9, 0, 2, werr,
                                      sizeof(werr)) != 0 &&
              strstr(werr, "out of range") != NULL,
              "x past end rejected (%s)", werr);
        CHECK(grads_ng_file_select_xy(NULL, 0, 0, 0, 0, werr,
                                      sizeof(werr)) != 0,
              "NULL file rejected");
        CHECK(grads_ng_file_window(NULL, &x1, &x2, &y1, &y2) == -1,
              "NULL window query fails");
        grads_ng_close(f1);
        CHECK(1, "close window-test file");
    }

    /* World-coordinate selection snaps with reference rounding, then
     * validates strictly (sample.ctl: XDEF 4 LINEAR 0 90,
     * YDEF 3 LINEAR -90 90, ZDEF 2 LEVELS 1000 500). */
    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "open sample.ctl for world tests");
    if (f1) {
        double s1, s2, w;
        char werr[256];
        CHECK(grads_ng_file_select_world(f1, 'x', 45.0, 45.0, &s1, &s2,
                                         werr, sizeof(werr)) == 0 &&
              s1 == 90.0 && s2 == 90.0,
              "lon 45 snaps to grid 2 (%.6g %.6g)", s1, s2);
        CHECK(grads_ng_file_select_world(f1, 'x', 0.0, 180.0, &s1, &s2,
                                         werr, sizeof(werr)) == 0 &&
              s1 == 0.0 && s2 == 180.0,
              "lon 0 180 spans grids 1..3");
        CHECK(grads_ng_file_select_world(f1, 'y', 0.0, 0.0, &s1, &s2,
                                         werr, sizeof(werr)) == 0 &&
              s1 == 0.0,
              "lat 0 snaps to grid 2");
        CHECK(grads_ng_file_select_world(f1, 'z', 750.0, 750.0, &s1, &s2,
                                         werr, sizeof(werr)) == 0 &&
              s1 == 500.0,
              "lev tie resolves to higher index (%.6g)", s1);
        CHECK(grads_ng_file_select_world(f1, 'z', 300.0, 300.0, &s1, &s2,
                                         werr, sizeof(werr)) == 0 &&
              s1 == 500.0,
              "lev 300 snaps to nearest (%.6g)", s1);
        CHECK(grads_ng_file_select_world(f1, 'x', 400.0, 400.0, &s1, &s2,
                                         werr, sizeof(werr)) != 0 &&
              strstr(werr, "out of range") != NULL,
              "lon past end rejected (%s)", werr);
        CHECK(grads_ng_file_select_world(f1, 'x', 180.0, 0.0, &s1, &s2,
                                         werr, sizeof(werr)) != 0 &&
              strstr(werr, "ascend") != NULL,
              "reversed lon rejected (%s)", werr);
        CHECK(grads_ng_file_select_world(f1, 'z', 500.0, 1000.0, &s1, &s2,
                                         werr, sizeof(werr)) != 0 &&
              strstr(werr, "not implemented") != NULL,
              "lev range rejected (%s)", werr);
        CHECK(grads_ng_file_select_world(f1, 'q', 1.0, 1.0, &s1, &s2,
                                         werr, sizeof(werr)) != 0,
              "bad axis rejected");
        CHECK(grads_ng_file_grid_to_world(f1, 'x', 3, &w) == 0 && w == 180.0,
              "grid 3 inverts to lon 180");
        CHECK(grads_ng_file_grid_to_world(f1, 'x', 9, &w) != 0,
              "grid past end has no world value");
        grads_ng_close(f1);
        CHECK(1, "close world-test file");
    }

    /* Absolute time selection (sample.ctl: 00Z02JAN1987 1DY, 2 steps). */
    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "open sample.ctl for time tests");
    if (f1) {
        char echo[64], terr[256], tstr[32];
        CHECK(grads_ng_file_time_at(f1, 0, tstr, sizeof(tstr)) == 0 &&
              strcmp(tstr, "00Z02JAN1987") == 0,
              "step 1 renders (%s)", tstr);
        CHECK(grads_ng_file_time_at(f1, 1, tstr, sizeof(tstr)) == 0 &&
              strcmp(tstr, "00Z03JAN1987") == 0,
              "step 2 renders (%s)", tstr);
        CHECK(grads_ng_file_time_at(f1, 2, tstr, sizeof(tstr)) != 0,
              "step past end has no rendering");
        CHECK(grads_ng_file_select_time(f1, "00Z03JAN1987", echo,
                                        sizeof(echo), terr,
                                        sizeof(terr)) == 0 &&
              strcmp(echo, "1987:1:3:0") == 0,
              "set time snaps with reference echo (%s)", echo);
        CHECK(grads_ng_file_select_time(f1, "12Z02JAN1987", echo,
                                        sizeof(echo), terr,
                                        sizeof(terr)) == 0 &&
              strcmp(echo, "1987:1:3:0") == 0,
              "midpoint tie goes up (%s)", echo);
        CHECK(grads_ng_file_select_time(f1, "garbage", echo, sizeof(echo),
                                        terr, sizeof(terr)) != 0 &&
              strstr(terr, "Invalid Date/Time") != NULL,
              "garbage rejected (%s)", terr);
        CHECK(grads_ng_file_select_time(f1, "00Z01JAN2000", echo,
                                        sizeof(echo), terr,
                                        sizeof(terr)) != 0 &&
              strstr(terr, "out of range") != NULL,
              "far time rejected (%s)", terr);
        CHECK(grads_ng_file_select_time(NULL, "00Z02JAN1987", echo,
                                        sizeof(echo), terr,
                                        sizeof(terr)) != 0,
              "NULL file rejected");
        grads_ng_close(f1);
        CHECK(1, "close time-test file");
    }

    /* Ensemble selection (sample.ctl has no EDEF: a single member).
     * select_e never validates (reference parity); out-of-range reads
     * degrade to missing instead of failing. */
    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "open sample.ctl for ensemble tests");
    if (f1) {
        int e = -9;
        char eerr[256];
        grads_ng_var_t *tsfc = grads_ng_get_var(f1, "tsfc");
        double eout[12];
        CHECK(grads_ng_file_ne(f1) == 1, "no EDEF means one member");
        CHECK(grads_ng_file_ne(NULL) == -1, "ne with NULL file fails");
        CHECK(grads_ng_file_selected_e(f1, &e) == 0 && e == 0,
              "ensemble defaults to member 1");
        CHECK(grads_ng_file_ens_name(f1, 1) == NULL,
              "unnamed member has no name");
        CHECK(grads_ng_file_select_e(f1, 1, eerr, sizeof(eerr)) == 0 &&
              grads_ng_file_selected_e(f1, &e) == 0 && e == 1,
              "select_e stores past-the-end index");
        CHECK(grads_ng_file_select_e(f1, -1, eerr, sizeof(eerr)) == 0,
              "select_e stores negative index");
        CHECK(grads_ng_file_select_e(NULL, 0, eerr, sizeof(eerr)) != 0,
              "select_e NULL file fails");
        CHECK(grads_ng_file_selected_e(NULL, &e) != 0,
              "selected_e NULL file fails");
        if (tsfc) {
            int k, allmissing = 1;
            CHECK(grads_ng_var_slice(tsfc, 0, 0, eout, eerr,
                                     sizeof(eerr)) == 0,
                  "out-of-range e degrades, not fails (%s)",
                  eerr[0] ? eerr : "ok");
            for (k = 0; k < 12; k++)
                if (!isnan(eout[k])) allmissing = 0;
            CHECK(allmissing, "degraded slice is all missing");
            CHECK(grads_ng_file_select_e(f1, 0, eerr, sizeof(eerr)) == 0 &&
                  grads_ng_var_slice(tsfc, 0, 0, eout, eerr,
                                     sizeof(eerr)) == 0 &&
                  eout[0] == 0.25,
                  "member 1 reads real data");
        }
        grads_ng_close(f1);
        CHECK(1, "close ensemble-test file");
    }

    /* Destroy with a file still open must clean up without crashing. */
    f1 = grads_ng_open(session, ctl);
    CHECK(f1 != NULL, "reopen sample.ctl");
    grads_ng_destroy(session);
    CHECK(1, "destroy with open file does not crash");

    /* NULL safety on accessors. */
    CHECK(grads_ng_file_dims(NULL, &nx, &ny, &nz, &nt) == -1,
          "dims with NULL file fails");
    CHECK(grads_ng_file_nvars(NULL) == -1, "nvars with NULL file fails");
    CHECK(grads_ng_file_type(NULL) == -1, "type with NULL file fails");
    grads_ng_close(NULL);
    CHECK(1, "close NULL is a no-op");
    grads_ng_destroy(NULL);
    CHECK(1, "destroy NULL is a no-op");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
