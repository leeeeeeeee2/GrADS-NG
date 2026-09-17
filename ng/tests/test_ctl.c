/* test_ctl.c - Unit tests for the GrADS descriptor (.ctl) parser.
 *
 * Covers M2 descriptor semantics: LINEAR/LEVELS axes, TDEF counting,
 * EDEF counting, VARS/code/longname splitting, DTYPE/OPTIONS/UNDEF,
 * template detection + expansion, and honest errors for malformed files.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctl.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); failures++; } \
        else { printf("ok   " __VA_ARGS__); printf("\n"); } \
    } while (0)

static int deq(double a, double b) {
    return fabs(a - b) < 1e-9;
}

/* Write an inline fixture to a temp file; returns path (static buffer). */
static const char *make_tmp(const char *name, const char *text) {
    static char path[256];
    FILE *fp;
    snprintf(path, sizeof(path), "%s", name);
    fp = fopen(path, "w");
    if (!fp) return NULL;
    fputs(text, fp);
    fclose(fp);
    return path;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "data";
    char sample[256], levels[256];
    const char *err = NULL;
    ng_ctl_t *c;

    snprintf(sample, sizeof(sample), "%s/sample.ctl", dir);
    snprintf(levels, sizeof(levels), "%s/levels.ctl", dir);

    /* Missing file is an honest error, not a crash. */
    c = ng_ctl_parse("/nonexistent/nope.ctl", &err);
    CHECK(c == NULL && err != NULL, "missing file fails with message");

    c = ng_ctl_parse(NULL, &err);
    CHECK(c == NULL && err != NULL, "NULL path fails with message");
    ng_ctl_free(NULL);
    CHECK(1, "ng_ctl_free(NULL) is a no-op");

    /* sample.ctl: LINEAR axes, default UNDEF, binary dtype. */
    c = ng_ctl_parse(sample, &err);
    CHECK(c != NULL, "parse sample.ctl (%s)", err ? err : "ok");
    if (c) {
        CHECK(c->nx == 4 && c->ny == 3 && c->nz == 2 && c->nt == 2,
              "sample dims 4x3x2x2");
        CHECK(c->xlinear && deq(c->xvals[0], 0.0) && deq(c->xvals[1], 90.0),
              "sample x LINEAR 0/90");
        CHECK(c->ylinear && deq(c->yvals[0], -90.0) && deq(c->yvals[1], 90.0),
              "sample y LINEAR -90/90");
        CHECK(!c->zlinear && c->nz == 2 && deq(c->zvals[0], 1000.0) &&
              deq(c->zvals[1], 500.0), "sample z LEVELS 1000/500");
        CHECK(c->ne == 0, "sample has no ensemble");
        CHECK(deq(c->undef, -9.99e33), "sample default UNDEF -9.99e33");
        CHECK(c->dtype == NG_DTYPE_BINARY, "sample dtype binary");
        CHECK(!c->template && !c->byteswap && !c->sequential,
              "sample has no flags set");
        CHECK(strcmp(c->dset, "^sample.dat") == 0, "sample DSET kept");
        CHECK(c->nvars == 2, "sample has 2 vars");
        CHECK(strcmp(c->vars[0].name, "tsfc") == 0 &&
              c->vars[0].nlevels == 0 && c->vars[0].code == 99,
              "sample var0 tsfc/0/code 99");
        CHECK(strcmp(c->vars[1].name, "hgt") == 0 &&
              c->vars[1].nlevels == 2,
              "sample var1 hgt/2");
        ng_ctl_free(c);
    }

    /* levels.ctl: LEVELS axes, options, ensemble, dtype, template. */
    c = ng_ctl_parse(levels, &err);
    CHECK(c != NULL, "parse levels.ctl (%s)", err ? err : "ok");
    if (c) {
        CHECK(!c->xlinear && c->nx == 3 && deq(c->xvals[0], 10.5) &&
              deq(c->xvals[1], 20.0) && deq(c->xvals[2], 30.25),
              "levels x LEVELS values");
        CHECK(c->ylinear && c->ny == 2, "levels y LINEAR");
        CHECK(!c->zlinear && c->nz == 3 && deq(c->zvals[0], 1000.0) &&
              deq(c->zvals[2], 500.0), "levels z LEVELS values");
        CHECK(c->nt == 4, "levels TDEF count 4");
        CHECK(c->ne == 2, "levels EDEF count 2");
        CHECK(deq(c->undef, -999.0), "levels UNDEF -999");
        CHECK(c->dtype == NG_DTYPE_NETCDF, "levels dtype netcdf");
        CHECK(c->byteswap && c->sequential && c->template,
              "levels OPTIONS flags set");
        CHECK(strcmp(c->title, "Levels/options/ensemble fixture") == 0,
              "levels TITLE kept");
        CHECK(c->nvars == 3 && strcmp(c->vars[1].name, "tair") == 0 &&
              c->vars[1].nlevels == 3 && c->vars[1].code == 11,
              "levels var1 tair/3/code 11");
        CHECK(strstr(c->vars[1].longname, "Kelvin") != NULL ||
              strstr(c->vars[1].longname, "(K)") != NULL,
              "levels var1 long name kept");

        /* Template expansion. */
        {
            char *p = ng_ctl_expand_template(c, 2001, 3, 5, 6);
            CHECK(p && strcmp(p, "^run/2001030506.dat") == 0,
                  "template expands to ^run/2001030506.dat (got %s)",
                  p ? p : "(null)");
            free(p);
        }
        ng_ctl_free(c);
    }

    /* EDEF member names: same-line, following-line, and malformed. */
    c = ng_ctl_parse(levels, &err);
    CHECK(c != NULL && c->ens_names != NULL &&
          strcmp(c->ens_names[0], "memA") == 0 &&
          strcmp(c->ens_names[1], "memB") == 0,
          "levels same-line EDEF names memA/memB");
    ng_ctl_free(c);
    {
        const char *p = make_tmp("test_tmp_edef.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "EDEF 3 NAMES\n* comment skipped\n\nalpha\nbeta\ngamma\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        CHECK(p != NULL, "write following-line EDEF fixture");
        c = ng_ctl_parse(p, &err);
        CHECK(c != NULL, "parse following-line EDEF (%s)",
              err ? err : "ok");
        if (c) {
            CHECK(c->ne == 3 && c->ens_names != NULL &&
                  strcmp(c->ens_names[0], "alpha") == 0 &&
                  strcmp(c->ens_names[2], "gamma") == 0,
                  "following-line EDEF names kept");
            ng_ctl_free(c);
        }
        remove(p);

        p = make_tmp("test_tmp_edefbare.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "EDEF 2\nVARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c != NULL && c->ne == 2 && c->ens_names == NULL,
              "bare EDEF stores no names (%s)", err ? err : "ok");
        ng_ctl_free(c);
        remove(p);

        p = make_tmp("test_tmp_edefshort.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "EDEF 2 NAMES\nonly\nVARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "EDEF needs 2 member names"),
              "truncated EDEF names fail (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_edefmany.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "EDEF 1 NAMES a b\nVARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "too many ensemble names"),
              "extra EDEF names fail (got %s)", err ? err : "(null)");
        remove(p);
    }

    /* Template token widths. */
    {
        const char *p = make_tmp("test_tmp_tpl.ctl",
            "DSET f%y4%y2-%m2-%m1-%d2-%d1-%h2-%h1-100%%.dat\n"
            "XDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\nZDEF 1 LINEAR 0 1\n"
            "TDEF 1 LINEAR 00Z01JAN2000 1DY\nVARS 1\na 0 1 A\nENDVARS\n");
        char *e;
        CHECK(p != NULL, "write template fixture");
        c = ng_ctl_parse(p, &err);
        CHECK(c != NULL, "parse template fixture (%s)", err ? err : "ok");
        if (c) {
            CHECK(c->template, "percent DSET implies template");
            e = ng_ctl_expand_template(c, 2001, 3, 5, 6);
            CHECK(e && strcmp(e, "f200101-03-3-05-5-06-6-100%.dat") == 0,
                  "token widths expand (got %s)", e ? e : "(null)");
            free(e);
            ng_ctl_free(c);
        }
        remove(p);
    }

    /* Malformed inputs fail with a naming message. */
    {
        const char *p;
        p = make_tmp("test_tmp_noend.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "VARS 1\na 0 1 A\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "ENDVARS"),
              "unclosed VARS names ENDVARS (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_badopt.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "OPTIONS yrev\nVARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "yrev"),
              "unsupported OPTION rejected (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_levels.ctl",
            "DSET x.dat\nXDEF 3 LEVELS 1 2\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "LEVELS"),
              "LEVELS count mismatch rejected (got %s)",
              err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_nodset.ctl",
            "XDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "DSET"),
              "missing DSET rejected (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_pdef.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 1 LINEAR 00Z01JAN2000 1DY\n"
            "PDEF 10 10 LCC 1 -100 40 40 30 60\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "PDEF"),
              "PDEF honestly rejected (got %s)", err ? err : "(null)");
        remove(p);

        /* TDEF validation is eager, like the reference open. */
        p = make_tmp("test_tmp_badtdef.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 2 LINEAR GARBAGE 1DY\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "start time"),
              "bad TDEF date rejected (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_badincr.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 2 LINEAR 00Z01JAN2000 1XX\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "increment"),
              "bad TDEF unit rejected (got %s)", err ? err : "(null)");
        remove(p);

        p = make_tmp("test_tmp_zeroincr.ctl",
            "DSET x.dat\nXDEF 1 LINEAR 0 1\nYDEF 1 LINEAR 0 1\n"
            "ZDEF 1 LINEAR 0 1\nTDEF 2 LINEAR 00Z01JAN2000 0DY\n"
            "VARS 1\na 0 1 A\nENDVARS\n");
        c = ng_ctl_parse(p, &err);
        CHECK(c == NULL && err && strstr(err, "increment"),
              "zero TDEF increment rejected (got %s)",
              err ? err : "(null)");
        remove(p);
    }

    /* expand_template(NULL) is safe. */
    CHECK(ng_ctl_expand_template(NULL, 2000, 1, 1, 0) == NULL,
          "expand on NULL ctl returns NULL");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
