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

    a = run("max(tsfc,hgt)", &err);
    for (i = 0; i < N; i++) {
        double t = raw(0, 0, i / NX, i % NX);
        want[i] = t;  /* hgt slice equals tsfc slice at t=1,z=1 */
    }
    CHECK(a && match(a, want, NULL), "max of equal slices");
    ng_array_free(a);

    a = run("min(tsfc,10)", &err);
    for (i = 0; i < N; i++) {
        double t = raw(0, 0, i / NX, i % NX);
        want[i] = (t < 10.0) ? t : 10.0;
    }
    CHECK(a && match(a, want, NULL), "min against scalar clamp");
    ng_array_free(a);

    a = run("pow(tsfc,2)", &err);
    for (i = 0; i < N; i++) {
        double t = raw(0, 0, i / NX, i % NX);
        want[i] = t * t;
    }
    CHECK(a && match(a, want, NULL), "pow matches square");
    ng_array_free(a);

    /* max skips a lone missing (t=2 sentinel); min likewise. */
    CHECK(grads_ng_file_select(g_file, 1, 0, emsg, sizeof(emsg)) == 0,
          "select t=2 for max/min missing");
    a = run("max(tsfc,1005)", &err);
    for (i = 0; i < N; i++) {
        y = i / NX;
        x = i % NX;
        wnan[i] = 0;
        if (y == 2 && x == 3) want[i] = 1005.0;  /* lone NaN loses */
        else {
            double t = raw(1, 0, y, x);
            want[i] = (t > 1005.0) ? t : 1005.0;
        }
    }
    CHECK(a && match(a, want, wnan), "max skips lone missing");
    ng_array_free(a);

    a = run("max(tsfc-tsfc,tsfc-tsfc)", &err);
    for (i = 0; i < N; i++) {
        wnan[i] = (i / NX == 2 && i % NX == 3);
        want[i] = 0.0;
    }
    CHECK(a && match(a, want, wnan), "max of all-missing stays missing");
    ng_array_free(a);

    a = run("pow(tsfc-tsfc,2)", &err);
    for (i = 0; i < N; i++) {
        wnan[i] = (i / NX == 2 && i % NX == 3);
        want[i] = 0.0;
    }
    CHECK(a && match(a, want, wnan), "pow propagates missing");
    ng_array_free(a);

    CHECK(grads_ng_file_select(g_file, 0, 0, emsg, sizeof(emsg)) == 0,
          "select back to t=1");

    a = run("frobnicate(tsfc)", &err);
    CHECK(!a && err && strstr(err, "frobnicate"), "unknown func named (%s)",
          err ? err : "no message");
    ng_array_free(a);

    a = run("max(tsfc)", &err);
    CHECK(!a && err && strstr(err, "2 argument"), "max arity enforced (%s)",
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
