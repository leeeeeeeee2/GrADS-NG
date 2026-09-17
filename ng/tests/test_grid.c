/* test_grid.c - Unit tests for the GrADS binary grid reader.
 *
 * Covers M2 data access: stream big-endian reads against the checked-in
 * sample.dat (byte-exact vs the gen_sample.py formula), UNDEF pass-through,
 * sequential + byteswapped reads via runtime fixtures, and honest errors
 * (bad indices, truncated files, template/non-binary descriptors).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctl.h"
#include "grid.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { printf("FAIL " __VA_ARGS__); printf("\n"); failures++; } \
        else { printf("ok   " __VA_ARGS__); printf("\n"); } \
    } while (0)

#define NX 4
#define NY 3

static double want(int t, int z, int y, int x) {
    float f = (float)(t * 1000.0 + z * 100.0 + y * 10.0 + x + 0.25);
    return (double)f;
}

/* Write a little-endian sequential fixture: 1 var, nt=2, 1 level. */
static int write_seq_le(const char *datpath, const char *ctlpath) {
    static const char ctl[] =
        "DSET %s\n"
        "XDEF 4 LINEAR 0 1\nYDEF 3 LINEAR 0 1\nZDEF 1 LINEAR 0 1\n"
        "TDEF 2 LINEAR 00Z01JAN2000 1DY\n"
        "OPTIONS sequential byteswapped\n"
        "VARS 1\nv1 0 1 V1\nENDVARS\n";
    char buf[1024];
    FILE *fp;
    int t, y, x;
    uint32_t marker = (uint32_t)(NX * NY * 4);
    unsigned char m[4];

    snprintf(buf, sizeof(buf), ctl, datpath);
    fp = fopen(ctlpath, "w");
    if (!fp) return -1;
    fputs(buf, fp);
    fclose(fp);

    /* Little-endian marker bytes, host-independent. */
    m[0] = (unsigned char)(marker & 0xFF);
    m[1] = (unsigned char)((marker >> 8) & 0xFF);
    m[2] = (unsigned char)((marker >> 16) & 0xFF);
    m[3] = (unsigned char)((marker >> 24) & 0xFF);

    fp = fopen(datpath, "wb");
    if (!fp) return -1;
    for (t = 0; t < 2; t++) {
        fwrite(m, 1, 4, fp);
        for (y = 0; y < NY; y++) {
            for (x = 0; x < NX; x++) {
                float f = (float)(t * 1000.0 + y * 10.0 + x + 0.25);
                uint32_t w;
                unsigned char b[4];
                memcpy(&w, &f, 4);
                b[0] = (unsigned char)(w & 0xFF);
                b[1] = (unsigned char)((w >> 8) & 0xFF);
                b[2] = (unsigned char)((w >> 16) & 0xFF);
                b[3] = (unsigned char)((w >> 24) & 0xFF);
                fwrite(b, 1, 4, fp);
            }
        }
        fwrite(m, 1, 4, fp);
    }
    fclose(fp);
    return 0;
}

/* Write a 2-member ensemble fixture: 2x2 grid, 1 var, nt=2, big-endian
 * stream, ensemble outermost (member 0's full time run, then member 1).
 * value = e*100 + t*10 + y*2 + x. Reference-form EDEF names included. */
static int write_ens(const char *datpath, const char *ctlpath) {
    static const char ctl[] =
        "DSET %s\n"
        "XDEF 2 LINEAR 0 1\nYDEF 2 LINEAR -45 90\nZDEF 1 LINEAR 1 1\n"
        "TDEF 2 LINEAR 00Z01JAN2000 1DY\n"
        "EDEF 2 NAMES\nmemA\nmemB\n"
        "VARS 1\nhgt 0 99 probe var\nENDVARS\n";
    char buf[1024];
    FILE *fp;
    int e, t, y, x;

    snprintf(buf, sizeof(buf), ctl, datpath);
    fp = fopen(ctlpath, "w");
    if (!fp) return -1;
    fputs(buf, fp);
    fclose(fp);

    fp = fopen(datpath, "wb");
    if (!fp) return -1;
    for (e = 0; e < 2; e++)
        for (t = 0; t < 2; t++)
            for (y = 0; y < 2; y++)
                for (x = 0; x < 2; x++) {
                    float f = (float)(e * 100 + t * 10 + y * 2 + x);
                    uint32_t w;
                    unsigned char b[4];
                    memcpy(&w, &f, 4);
                    b[0] = (unsigned char)((w >> 24) & 0xFF);
                    b[1] = (unsigned char)((w >> 16) & 0xFF);
                    b[2] = (unsigned char)((w >> 8) & 0xFF);
                    b[3] = (unsigned char)(w & 0xFF);
                    fwrite(b, 1, 4, fp);
                }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "data";
    char ctlpath[256];
    const char *err = NULL;
    ng_ctl_t *c;
    ng_grid_t *g;
    double out[NX * NY];
    int t, z, y, x, bad = 0;

    snprintf(ctlpath, sizeof(ctlpath), "%s/sample.ctl", dir);

    ng_grid_close(NULL);
    CHECK(1, "ng_grid_close(NULL) is a no-op");
    CHECK(ng_grid_open(NULL, &err) == NULL && err, "open NULL ctl fails");
    CHECK(ng_grid_var_levels(NULL, 0) == -1, "var_levels NULL fails");

    c = ng_ctl_parse(ctlpath, &err);
    CHECK(c != NULL, "parse sample.ctl (%s)", err ? err : "ok");
    if (!c) {
        printf("\n%d checks, %d failures\n", checks, failures);
        return 1;
    }

    g = ng_grid_open(c, &err);
    CHECK(g != NULL, "open sample.dat grid (%s)", err ? err : "ok");
    if (!g) {
        ng_ctl_free(c);
        printf("\n%d checks, %d failures\n", checks, failures);
        return 1;
    }

    CHECK(ng_grid_var_levels(g, 0) == 1, "tsfc has 1 stored level");
    CHECK(ng_grid_var_levels(g, 1) == 2, "hgt has 2 stored levels");
    CHECK(ng_grid_var_levels(g, 2) == -1, "var_levels out of range fails");

    /* Byte-exact reads of every slice of both variables (e=0: the
     * sample file has no EDEF, so a single member). */
    for (t = 0; t < 2; t++) {
        /* var 0 tsfc (surface): z must be 0. */
        CHECK(ng_grid_read_slice(g, 0, t, 0, 0, out, &err) == 0,
              "read tsfc t=%d (%s)", t, err ? err : "ok");
        for (y = 0; y < NY && !bad; y++)
            for (x = 0; x < NX; x++) {
                double w = want(t, 0, y, x);
                if (t == 1 && y == 2 && x == 3)
                    w = (double)(float)c->undef;  /* sentinel passes through */
                if (out[y * NX + x] != w) bad = 1;
            }
        CHECK(!bad, "tsfc t=%d values byte-exact", t);
        bad = 0;
        CHECK(ng_grid_read_slice(g, 0, t, 1, 0, out, &err) != 0 && err,
              "tsfc z=1 rejected (%s)", err ? err : "no message");

        /* var 1 hgt (2 levels). */
        for (z = 0; z < 2; z++) {
            CHECK(ng_grid_read_slice(g, 1, t, z, 0, out, &err) == 0,
                  "read hgt t=%d z=%d (%s)", t, z, err ? err : "ok");
            for (y = 0; y < NY && !bad; y++)
                for (x = 0; x < NX; x++)
                    if (out[y * NX + x] != want(t, z, y, x)) bad = 1;
            CHECK(!bad, "hgt t=%d z=%d values byte-exact", t, z);
            bad = 0;
        }
    }

    /* Index validation. */
    CHECK(ng_grid_read_slice(g, -1, 0, 0, 0, out, &err) != 0 && err,
          "negative var rejected");
    CHECK(ng_grid_read_slice(g, 2, 0, 0, 0, out, &err) != 0 && err,
          "var past end rejected");
    CHECK(ng_grid_read_slice(g, 0, 2, 0, 0, out, &err) != 0 && err,
          "time past end rejected");
    CHECK(ng_grid_read_slice(g, 0, 0, 0, 0, NULL, &err) != 0 && err,
          "NULL output rejected");
    /* No EDEF means one member: e=0 reads, anything else fails. */
    CHECK(ng_grid_read_slice(g, 0, 0, 0, 1, out, &err) != 0 && err &&
          strstr(err, "ensemble 2 out of range (file holds 1..1)") != NULL,
          "e=1 without EDEF rejected (%s)", err ? err : "no message");
    CHECK(ng_grid_read_slice(g, 0, 0, 0, -1, out, &err) != 0 && err,
          "negative e rejected");
    ng_grid_close(g);

    /* Non-binary / templated descriptors are honestly rejected. */
    {
        char lv[256];
        ng_ctl_t *c2;
        snprintf(lv, sizeof(lv), "%s/levels.ctl", dir);
        c2 = ng_ctl_parse(lv, &err);
        CHECK(c2 != NULL, "parse levels.ctl (%s)", err ? err : "ok");
        if (c2) {
            CHECK(ng_grid_open(c2, &err) == NULL && err,
                  "non-binary/template grid rejected (%s)",
                  err ? err : "no message");
            ng_ctl_free(c2);
        }
    }

    /* Truncated data file fails at open with sizes named. */
    {
        FILE *fp = fopen("test_tmp_short.dat", "wb");
        ng_ctl_t *c3;
        static const char ctl[] =
            "DSET test_tmp_short.dat\n"
            "XDEF 4 LINEAR 0 1\nYDEF 3 LINEAR 0 1\nZDEF 1 LINEAR 0 1\n"
            "TDEF 2 LINEAR 00Z01JAN2000 1DY\nVARS 1\nv1 0 1 V1\nENDVARS\n";
        FILE *cf = fopen("test_tmp_short.ctl", "w");
        CHECK(fp && cf, "write truncation fixtures");
        if (fp) {
            char zeros[8] = {0};
            fwrite(zeros, 1, sizeof(zeros), fp);
            fclose(fp);
        }
        if (cf) {
            fputs(ctl, cf);
            fclose(cf);
        }
        c3 = ng_ctl_parse("test_tmp_short.ctl", &err);
        CHECK(c3 != NULL, "parse truncation ctl (%s)", err ? err : "ok");
        if (c3) {
            CHECK(ng_grid_open(c3, &err) == NULL && err &&
                  strstr(err, "holds 8 bytes"),
                  "short file rejected with sizes (%s)",
                  err ? err : "no message");
            ng_ctl_free(c3);
        }
        remove("test_tmp_short.dat");
        remove("test_tmp_short.ctl");
    }

    /* Sequential + byteswapped round-trip. */
    CHECK(write_seq_le("test_tmp_seq.dat", "test_tmp_seq.ctl") == 0,
          "write sequential/LE fixtures");
    {
        ng_ctl_t *c4 = ng_ctl_parse("test_tmp_seq.ctl", &err);
        CHECK(c4 != NULL, "parse sequential ctl (%s)", err ? err : "ok");
        if (c4) {
            ng_grid_t *g4 = ng_grid_open(c4, &err);
            CHECK(g4 != NULL, "open sequential grid (%s)",
                  err ? err : "ok");
            if (g4) {
                for (t = 0; t < 2; t++) {
                    CHECK(ng_grid_read_slice(g4, 0, t, 0, 0, out, &err) == 0,
                          "read sequential t=%d (%s)", t, err ? err : "ok");
                    for (y = 0; y < NY && !bad; y++)
                        for (x = 0; x < NX; x++)
                            if (out[y * NX + x] != want(t, 0, y, x)) bad = 1;
                    CHECK(!bad, "sequential t=%d values byte-exact", t);
                    bad = 0;
                }
                ng_grid_close(g4);
            }
            ng_ctl_free(c4);
        }
    }
    remove("test_tmp_seq.dat");
    remove("test_tmp_seq.ctl");

    /* Ensemble layout: members outermost, names parsed, bounds checked. */
    CHECK(write_ens("test_tmp_ens.dat", "test_tmp_ens.ctl") == 0,
          "write ensemble fixtures");
    {
        ng_ctl_t *c5 = ng_ctl_parse("test_tmp_ens.ctl", &err);
        CHECK(c5 != NULL, "parse ensemble ctl (%s)", err ? err : "ok");
        if (c5) {
            int e, t;
            CHECK(c5->ne == 2, "ensemble count 2");
            CHECK(c5->ens_names != NULL &&
                  strcmp(c5->ens_names[0], "memA") == 0 &&
                  strcmp(c5->ens_names[1], "memB") == 0,
                  "member names memA/memB");
            ng_grid_t *g5 = ng_grid_open(c5, &err);
            CHECK(g5 != NULL, "open ensemble grid (%s)",
                  err ? err : "ok");
            if (g5) {
                for (e = 0; e < 2; e++)
                    for (t = 0; t < 2; t++) {
                        CHECK(ng_grid_read_slice(g5, 0, t, 0, e, out,
                                                 &err) == 0,
                              "read ensemble e=%d t=%d (%s)", e, t,
                              err ? err : "ok");
                        for (y = 0; y < 2 && !bad; y++)
                            for (x = 0; x < 2; x++)
                                if (out[y * 2 + x] !=
                                    (double)(float)(e * 100 + t * 10 +
                                                   y * 2 + x))
                                    bad = 1;
                        CHECK(!bad, "ensemble e=%d t=%d values match "
                              "member-outermost layout", e, t);
                        bad = 0;
                    }
                CHECK(ng_grid_read_slice(g5, 0, 0, 0, 2, out, &err) != 0 &&
                      err, "e=2 past end rejected (%s)",
                      err ? err : "no message");
                ng_grid_close(g5);
            }
            ng_ctl_free(c5);
        }
    }
    remove("test_tmp_ens.dat");
    remove("test_tmp_ens.ctl");

    ng_ctl_free(c);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
