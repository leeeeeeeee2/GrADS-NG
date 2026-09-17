/* test_eval.c - Unit tests for the M3 array expression evaluator.
 *
 * Evaluates expressions against tests/data/sample.ctl/.dat and checks
 * elementwise arithmetic, scalar broadcast, math functions, UNDEF->NaN
 * propagation, dimension selection, and honest errors.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grads_ng.h"
#include "parser/grads_ng_parser.h"
#include "array.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); failures++; } \
        else { printf("ok   " __VA_ARGS__); printf("\n"); } \
    } while (0)

#define NX 4
#define NY 3
#define N (NX * NY)

/* Expected raw value (matches gen_sample.py), float-domain like the reader. */
static double raw(int t, int z, int y, int x) {
    float f = (float)(t * 1000.0 + z * 100.0 + y * 10.0 + x + 0.25);
    return (double)f;
}

static grads_ng_file_t *g_file;

static ng_array_t *run(const char *expr, const char **err) {
    grads_ng_parser_t *p = grads_ng_parser_create(expr);
    grads_ng_ast_node_t *ast;
    ng_array_t *a;
    if (!p) return NULL;
    ast = grads_ng_parser_parse(p);
    grads_ng_parser_destroy(p);
    if (!ast) return NULL;
    a = ng_eval_array(g_file, ast, err);
    grads_ng_ast_destroy(ast);
    return a;
}

/* Compare array against per-element expectation; want_nan[i] marks missing. */
static int match(const ng_array_t *a, const double *want, const int *wnan) {
    int i;
    if (!a || a->nx != NX || a->ny != NY) return 0;
    for (i = 0; i < N; i++) {
        if (wnan && wnan[i]) {
            if (!isnan(a->data[i])) return 0;
        } else if (isnan(a->data[i]) || a->data[i] != want[i]) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "data";
    char ctl[256];
    grads_ng_config_t config;
    grads_ng_session_t *session;
    const char *err = NULL;
    ng_array_t *a;
    double want[N];
    int wnan[N];
    int i, y, x;
    char emsg[256];

    snprintf(ctl, sizeof(ctl), "%s/sample.ctl", dir);
    memset(&config, 0, sizeof(config));
    config.headless = 1;
    config.batch_mode = 1;
    session = grads_ng_init(&config);
    CHECK(session != NULL, "init session");
    g_file = grads_ng_open(session, ctl);
    CHECK(g_file != NULL, "open sample.ctl");
    if (!g_file) {
        printf("\n%d checks, %d failures\n", checks, failures);
        return 1;
    }

    /* Pure-variable and scalar broadcast arithmetic at t=1,z=1. */
    a = run("tsfc", &err);
    for (i = 0; i < N; i++) want[i] = raw(0, 0, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "bare variable slice");
    ng_array_free(a);

    a = run("tsfc+1", &err);
    for (i = 0; i < N; i++) want[i] = raw(0, 0, i / NX, i % NX) + 1.0;
    CHECK(a && match(a, want, NULL), "variable plus scalar");
    ng_array_free(a);

    a = run("2*tsfc-1", &err);
    for (i = 0; i < N; i++) want[i] = 2.0 * raw(0, 0, i / NX, i % NX) - 1.0;
    CHECK(a && match(a, want, NULL), "mixed arithmetic");
    ng_array_free(a);

    a = run("hgt-tsfc", &err);
    for (i = 0; i < N; i++) want[i] = 0.0;  /* same t/z slice cancels out */
    CHECK(a && match(a, want, NULL), "variable minus itself is zero");
    ng_array_free(a);

    a = run("TSFC+0", &err);
    for (i = 0; i < N; i++) want[i] = raw(0, 0, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "lookup is case-insensitive");
    ng_array_free(a);

    /* Dimension selection moves the slice. */
    CHECK(grads_ng_file_select(g_file, 1, 1, emsg, sizeof(emsg)) == 0,
          "select t=2 z=2");
    a = run("hgt", &err);
    for (i = 0; i < N; i++) want[i] = raw(1, 1, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "selection moves hgt slice");
    ng_array_free(a);
    CHECK(grads_ng_file_select(g_file, 0, 0, emsg, sizeof(emsg)) == 0,
          "select back to t=1 z=1");
    CHECK(grads_ng_file_select(g_file, 5, 0, emsg, sizeof(emsg)) != 0 &&
          emsg[0], "t past end rejected with message");
    CHECK(grads_ng_file_select(g_file, 0, 9, emsg, sizeof(emsg)) != 0,
          "z past end rejected");

    /* X/Y window slicing clips every evaluation to the selected grid box. */
    CHECK(grads_ng_file_select_xy(g_file, 0, 1, 0, 2, emsg,
                                  sizeof(emsg)) == 0,
          "select x=1..2");
    a = run("hgt", &err);
    if (!a || a->nx != 2 || a->ny != 3) {
        CHECK(0, "windowed slice is 2x3");
    } else {
        int ok = 1;
        for (y = 0; y < 3 && ok; y++)
            for (x = 0; x < 2 && ok; x++)
                if (a->data[y * 2 + x] != raw(0, 0, y, x)) ok = 0;
        CHECK(ok, "windowed values match fixture");
    }
    ng_array_free(a);

    CHECK(grads_ng_file_select_xy(g_file, 0, 0, 1, 1, emsg,
                                  sizeof(emsg)) == 0,
          "select single point x=1 y=2");
    a = run("tsfc+1", &err);
    CHECK(a && a->nx == 1 && a->ny == 1 &&
          a->data[0] == raw(0, 0, 1, 0) + 1.0,
          "expression over point window");
    ng_array_free(a);

    a = run("ave(hgt,z=1,z=2)", &err);
    CHECK(a && a->nx == 1 && a->ny == 1 &&
          a->data[0] == (raw(0, 0, 1, 0) + raw(0, 1, 1, 0)) / 2.0,
          "reduction over point window");
    ng_array_free(a);

    CHECK(grads_ng_file_select_xy(g_file, 0, 3, 0, 2, emsg,
                                  sizeof(emsg)) == 0,
          "restore full window");
    a = run("hgt", &err);
    CHECK(a && a->nx == NX && a->ny == NY, "full grid restored");
    ng_array_free(a);

    /* Evaluation follows world-coordinate selection (lon 0..180 clips
     * to grids 1..3 on the fixture's 90-step axis). */
    {
        double s1, s2;
        CHECK(grads_ng_file_select_world(g_file, 'x', 0.0, 180.0, &s1,
                                         &s2, emsg, sizeof(emsg)) == 0,
              "select lon 0 180");
        a = run("hgt", &err);
        if (!a || a->nx != 3 || a->ny != NY) {
            CHECK(0, "world window is 3x3");
        } else {
            int ok = 1;
            for (y = 0; y < NY && ok; y++)
                for (x = 0; x < 3 && ok; x++)
                    if (a->data[y * 3 + x] != raw(0, 0, y, x)) ok = 0;
            CHECK(ok, "world window values match fixture");
        }
        ng_array_free(a);
        CHECK(grads_ng_file_select_xy(g_file, 0, 3, 0, 2, emsg,
                                      sizeof(emsg)) == 0,
              "restore full window after world select");
    }

    /* UNDEF->NaN propagation at t=2 (sentinel at y=2,x=3). */
    CHECK(grads_ng_file_select(g_file, 1, 0, emsg, sizeof(emsg)) == 0,
          "select t=2");
    a = run("tsfc*2", &err);
    for (i = 0; i < N; i++) {
        y = i / NX;
        x = i % NX;
        wnan[i] = (y == 2 && x == 3);
        want[i] = wnan[i] ? 0.0 : raw(1, 0, y, x) * 2.0;
    }
    CHECK(a && match(a, want, wnan), "missing propagates through multiply");
    ng_array_free(a);

    a = run("tsfc-tsfc", &err);
    for (i = 0; i < N; i++) {
        wnan[i] = (i / NX == 2 && i % NX == 3);
        want[i] = wnan[i] ? 0.0 : 0.0;
    }
    CHECK(a && match(a, want, wnan), "missing minus missing is missing");
    ng_array_free(a);

    a = run("tsfc>0", &err);
    for (i = 0; i < N; i++) {
        wnan[i] = (i / NX == 2 && i % NX == 3);
        want[i] = wnan[i] ? 0.0 : 1.0;
    }
    CHECK(a && match(a, want, wnan), "comparison with missing is missing");
    ng_array_free(a);

    a = run("tsfc/0", &err);
    for (i = 0; i < N; i++) wnan[i] = 1;
    CHECK(a && match(a, want, wnan), "division by zero yields missing");
    ng_array_free(a);

    /* Functions. */
    CHECK(grads_ng_file_select(g_file, 0, 0, emsg, sizeof(emsg)) == 0,
          "select t=1");
    a = run("abs(0-tsfc)", &err);
    for (i = 0; i < N; i++) want[i] = fabs(raw(0, 0, i / NX, i % NX));
    CHECK(a && match(a, want, NULL), "abs of negation");
    ng_array_free(a);

    a = run("sqrt(tsfc)", &err);
    for (i = 0; i < N; i++) want[i] = sqrt(raw(0, 0, i / NX, i % NX));
    CHECK(a && match(a, want, NULL), "sqrt over non-negative slice");
    ng_array_free(a);

    a = run("sqrt(0-tsfc-100)", &err);
    for (i = 0; i < N; i++) wnan[i] = 1;
    CHECK(a && match(a, want, wnan), "sqrt domain error becomes missing");
    ng_array_free(a);

    a = run("SIN(tsfc*0)", &err);
    for (i = 0; i < N; i++) want[i] = 0.0;
    CHECK(a && match(a, want, NULL), "function names case-insensitive");
    ng_array_free(a);

    /* Honest errors. */
    a = run("nope+1", &err);
    CHECK(!a && err && strstr(err, "\"nope\""), "unknown var named (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("pow(tsfc,2)", &err);
    for (i = 0; i < N; i++) {
        double t = raw(0, 0, i / NX, i % NX);
        want[i] = t * t;
    }
    CHECK(a && match(a, want, NULL), "pow matches square");
    ng_array_free(a);

    CHECK(grads_ng_file_select(g_file, 1, 0, emsg, sizeof(emsg)) == 0,
          "select t=2");
    a = run("pow(tsfc-tsfc,2)", &err);
    for (i = 0; i < N; i++) {
        wnan[i] = (i / NX == 2 && i % NX == 3);
        want[i] = 0.0;
    }
    CHECK(a && match(a, want, wnan), "pow propagates missing");
    ng_array_free(a);
    CHECK(grads_ng_file_select(g_file, 0, 0, emsg, sizeof(emsg)) == 0,
          "select back to t=1");

    /* Reductions over t/z ranges (GrADS max/min/ave semantics). */
    a = run("max(tsfc,t=1,t=2)", &err);
    for (i = 0; i < N; i++) {
        /* t=2 wins everywhere except the sentinel, where t=1 fills in. */
        y = i / NX;
        x = i % NX;
        wnan[i] = 0;
        want[i] = (y == 2 && x == 3) ? raw(0, 0, y, x) : raw(1, 0, y, x);
    }
    CHECK(a && match(a, want, wnan), "max over t skips lone missing");
    ng_array_free(a);

    a = run("min(tsfc,t=1,t=2)", &err);
    for (i = 0; i < N; i++) {
        y = i / NX;
        x = i % NX;
        wnan[i] = 0;
        want[i] = raw(0, 0, y, x);  /* t=1 wins; sentinel filled from t=1 */
    }
    CHECK(a && match(a, want, wnan), "min over t");
    ng_array_free(a);

    a = run("ave(tsfc,t=1,t=2)", &err);
    for (i = 0; i < N; i++) {
        double a0, a1;
        y = i / NX;
        x = i % NX;
        wnan[i] = 0;
        a0 = raw(0, 0, y, x);
        if (y == 2 && x == 3) want[i] = a0;  /* only t=1 valid */
        else {
            a1 = raw(1, 0, y, x);
            want[i] = (a0 + a1) / 2.0;
        }
    }
    CHECK(a && match(a, want, wnan), "ave over t skips missing");
    ng_array_free(a);

    a = run("sum(tsfc,t=1,t=2)", &err);
    for (i = 0; i < N; i++) {
        double a0;
        y = i / NX;
        x = i % NX;
        wnan[i] = 0;
        a0 = raw(0, 0, y, x);
        if (y == 2 && x == 3) want[i] = a0;  /* only t=1 valid */
        else want[i] = a0 + raw(1, 0, y, x);
    }
    CHECK(a && match(a, want, wnan), "sum over t skips missing");
    ng_array_free(a);

    a = run("sum(hgt,z=1,z=2)", &err);
    for (i = 0; i < N; i++)
        want[i] = raw(0, 0, i / NX, i % NX) + raw(0, 1, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "sum over z adds levels");
    ng_array_free(a);

    a = run("sum(sqrt(0-tsfc-100),t=1,t=2)", &err);
    for (i = 0; i < N; i++) wnan[i] = 1;
    CHECK(a && match(a, want, wnan), "sum of all-missing stays missing");
    ng_array_free(a);

    a = run("sum(tsfc)", &err);
    CHECK(!a && err && strstr(err, "3 arguments"), "sum arity enforced (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(hgt,z=1,z=2)", &err);
    for (i = 0; i < N; i++) want[i] = raw(0, 1, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "max over z picks upper level");
    ng_array_free(a);

    a = run("ave(tsfc+tsfc,t=1,t=1)", &err);
    for (i = 0; i < N; i++) want[i] = 2.0 * raw(0, 0, i / NX, i % NX);
    CHECK(a && match(a, want, NULL), "reduction over expressions");
    ng_array_free(a);

    a = run("max(tsfc)", &err);
    CHECK(!a && err && strstr(err, "3 arguments"), "max arity enforced (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(tsfc,x=1,x=4)", &err);
    CHECK(!a && err && strstr(err, "only t/z"), "x ranges rejected (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(tsfc,t=2,t=1)", &err);
    CHECK(!a && err && strstr(err, "past end"), "reversed range rejected (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(tsfc,t=1,z=2)", &err);
    CHECK(!a && err && strstr(err, "must match"), "mixed dims rejected (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(tsfc,t=1,t=9)", &err);
    CHECK(!a && err && strstr(err, "out of range"), "range bound checked (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("frobnicate(tsfc)", &err);
    CHECK(!a && err && strstr(err, "frobnicate"), "unknown func named (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("abs(tsfc,tsfc)", &err);
    CHECK(!a && err && strstr(err, "1 argument"), "arity enforced (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("tsfc+", &err);
    CHECK(!a, "truncated expression fails");
    ng_array_free(a);

    grads_ng_destroy(session);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
