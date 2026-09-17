/* test_core.c - Unit tests for session lifecycle and file metadata accessors.
 *
 * Exercises the M1 vertical slice: init, open, dims/nvars/type accessors,
 * out-of-order close (swap-remove), destroy-with-open-files, and NULL safety.
 */
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
