/*
 * ng/src/io/ctl.c
 * GrADS descriptor (.ctl) parser implementation.
 *
 * M2 scope notes (all deviations are honest errors, never silent behavior):
 * - TDEF stores only the time-step count; the absolute time axis is built by
 *   the time module (M3/M4). The start/increment tokens are validated present.
 * - EDEF stores the ensemble count plus member names: same-line names
 *   (`EDEF 2 NAMES memA memB`) or one name per following line when the
 *   card ends with a bare NAMES (the reference form). A bare `EDEF n`
 *   stores no names.
 * - VARS lines: name + level count are authoritative; a leading integer in
 *   the remainder is kept as the GRIB code; the full remainder is the
 *   long name. There is no per-variable units field in .ctl binary files,
 *   so units stays "" until NetCDF support (M2+) fills it.
 * - OPTIONS understands byteswapped / sequential / template only.
 * - PDEF (projected grids) is rejected; it needs interpolation support.
 */

#include "ctl.h"
#include "ng_time.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NG_CTL_LINE_MAX 4096
#define NG_CTL_UNDEF_DEFAULT (-9.99e33)

/* Static error buffer: matches the "do not free" contract in ctl.h.
 * Not thread-safe; the parser is single-session for now. */
static char g_err[512];

static const char *fail(int lineno, const char *fmt, ...) {
    va_list ap;
    int off;

    if (lineno > 0)
        off = snprintf(g_err, sizeof(g_err), "line %d: ", lineno);
    else
        off = snprintf(g_err, sizeof(g_err), "%s", "");

    va_start(ap, fmt);
    vsnprintf(g_err + off, sizeof(g_err) - (size_t)off, fmt, ap);
    va_end(ap);
    return g_err;
}

/* ASCII case-insensitive string equality (portable across MSVC/POSIX). */
static int eqi(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* In-place trim; returns pointer to first non-space character. */
static char *trim(char *s) {
    size_t n;
    while (*s && isspace((unsigned char)*s)) s++;
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

/* Split the next whitespace-delimited token off *cursor (NUL-terminates it).
 * Returns NULL when no token remains. */
static char *next_token(char **cursor) {
    char *p = *cursor;
    char *tok;

    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        *cursor = p;
        return NULL;
    }
    tok = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';
    *cursor = p;
    return tok;
}

static int parse_int(const char *tok, int *out) {
    char *end;
    long v;

    if (!tok || !*tok) return -1;
    v = strtol(tok, &end, 10);
    if (*end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_double(const char *tok, double *out) {
    char *end;
    double v;

    if (!tok || !*tok) return -1;
    v = strtod(tok, &end);
    if (*end != '\0') return -1;
    *out = v;
    return 0;
}

/* Directive keywords: a member-name line starting with one of these is a
 * malformed EDEF (the names ran into the next card), not a member name. */
static int is_directive(const char *tok) {
    static const char *const kw[] = {
        "DSET", "TITLE", "UNDEF", "XDEF", "YDEF", "ZDEF", "TDEF", "EDEF",
        "VARS", "ENDVARS", "DTYPE", "OPTIONS", "PDEF", NULL
    };
    int i;

    if (!tok) return 0;
    for (i = 0; kw[i]; i++)
        if (eqi(tok, kw[i])) return 1;
    return 0;
}

/* Store ensemble member name idx (0-based) of count (truncates overlong
 * names). Allocates the table on first use. */
static int push_ens_name(ng_ctl_t *ctl, int count, int idx, const char *name,
                         int lineno) {
    if (idx < 0 || idx >= count) {
        fail(lineno, "too many ensemble names (EDEF count is %d)", count);
        return -1;
    }
    if (!ctl->ens_names) {
        ctl->ens_names = calloc((size_t)count, sizeof(*ctl->ens_names));
        if (!ctl->ens_names) {
            fail(lineno, "out of memory");
            return -1;
        }
    }
    snprintf(ctl->ens_names[idx], NG_CTL_NAMELEN, "%s", name);
    return 0;
}

/* Parse "n LINEAR start incr" or "n LEVELS v1..vn" axis definitions. */
static int parse_axis(char **cursor, int lineno, const char *what,
                      int *n_out, double **vals_out, int *linear_out) {
    char *tok;
    int n, i;
    double *vals;

    tok = next_token(cursor);
    if (!tok || parse_int(tok, &n) != 0 || n <= 0)
        return (fail(lineno, "%s needs a positive grid count", what), -1);

    tok = next_token(cursor);
    if (!tok)
        return (fail(lineno, "%s needs LINEAR or LEVELS mapping", what), -1);

    if (eqi(tok, "LINEAR")) {
        char *sstart = next_token(cursor);
        char *sincr = next_token(cursor);
        double start, incr;

        if (!sstart || !sincr || parse_double(sstart, &start) != 0 ||
            parse_double(sincr, &incr) != 0)
            return (fail(lineno, "%s LINEAR needs start and increment values",
                          what), -1);
        if (next_token(cursor))
            return (fail(lineno, "%s LINEAR takes exactly 2 values", what), -1);
        vals = malloc(2 * sizeof(*vals));
        if (!vals) return (fail(0, "out of memory"), -1);
        vals[0] = start;
        vals[1] = incr;
        *linear_out = 1;
    } else if (eqi(tok, "LEVELS")) {
        double v;
        vals = malloc((size_t)n * sizeof(*vals));
        if (!vals) return (fail(0, "out of memory"), -1);
        for (i = 0; i < n; i++) {
            tok = next_token(cursor);
            if (!tok || parse_double(tok, &v) != 0) {
                free(vals);
                return (fail(lineno, "%s LEVELS needs %d values", what, n), -1);
            }
            vals[i] = v;
        }
        if (next_token(cursor)) {
            free(vals);
            return (fail(lineno, "%s LEVELS takes exactly %d values", what, n),
                    -1);
        }
        *linear_out = 0;
    } else {
        return (fail(lineno, "%s needs LINEAR or LEVELS mapping, got \"%s\"",
                      what, tok), -1);
    }

    free(*vals_out);
    *vals_out = vals;
    *n_out = n;
    return 0;
}

static int parse_dtype(const char *tok, ng_dtype_t *out) {
    if (eqi(tok, "binary") || eqi(tok, "stream")) *out = NG_DTYPE_BINARY;
    else if (eqi(tok, "netcdf")) *out = NG_DTYPE_NETCDF;
    else if (eqi(tok, "hdf") || eqi(tok, "hdf4") || eqi(tok, "hdfsds"))
        *out = NG_DTYPE_HDF4;
    else if (eqi(tok, "hdf5")) *out = NG_DTYPE_HDF5;
    else if (eqi(tok, "grib") || eqi(tok, "grib1")) *out = NG_DTYPE_GRIB;
    else if (eqi(tok, "grib2")) *out = NG_DTYPE_GRIB2;
    else if (eqi(tok, "bufr")) *out = NG_DTYPE_BUFR;
    else return -1;
    return 0;
}

ng_ctl_t *ng_ctl_parse(const char *path, const char **err_out) {
    FILE *fp;
    char line[NG_CTL_LINE_MAX];
    ng_ctl_t *ctl = NULL;
    int lineno = 0;
    int seen_dset = 0, seen_x = 0, seen_y = 0, seen_z = 0, seen_t = 0;
    int seen_vars = 0;
    int opt_template = 0;
    const char *err = NULL;

    if (err_out) *err_out = NULL;
    if (!path) {
        if (err_out) *err_out = fail(0, "NULL descriptor path");
        return NULL;
    }

    fp = fopen(path, "r");
    if (!fp) {
        if (err_out) *err_out = fail(0, "cannot open descriptor \"%s\"", path);
        return NULL;
    }

    ctl = calloc(1, sizeof(*ctl));
    if (!ctl) {
        fclose(fp);
        if (err_out) *err_out = fail(0, "out of memory");
        return NULL;
    }
    ctl->undef = NG_CTL_UNDEF_DEFAULT;
    ctl->dtype = NG_DTYPE_BINARY;

    /* Record the descriptor directory: "^file" resolves against it. */
    {
        const char *slash = strrchr(path, '/');
#ifdef _WIN32
        const char *bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        if (slash) {
            size_t n = (size_t)(slash - path);
            if (n >= sizeof(ctl->ctldir)) n = sizeof(ctl->ctldir) - 1;
            memcpy(ctl->ctldir, path, n);
            ctl->ctldir[n] = '\0';
        } else {
            snprintf(ctl->ctldir, sizeof(ctl->ctldir), ".");
        }
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p, *keyword, *cursor, *tok;
        lineno++;

        if (!strchr(line, '\n') && !feof(fp)) {
            err = fail(lineno, "line exceeds %d characters", NG_CTL_LINE_MAX);
            break;
        }
        p = trim(line);
        if (*p == '\0' || *p == '*') continue;  /* blank line or comment */

        cursor = p;
        keyword = next_token(&cursor);
        if (!keyword) continue;

        if (eqi(keyword, "DSET")) {
            p = trim(cursor);
            if (*p == '\0') {
                err = fail(lineno, "DSET needs a data file path");
                break;
            }
            snprintf(ctl->dset, sizeof(ctl->dset), "%s", p);
            seen_dset = 1;
        } else if (eqi(keyword, "TITLE")) {
            p = trim(cursor);
            snprintf(ctl->title, sizeof(ctl->title), "%s", p);
        } else if (eqi(keyword, "UNDEF")) {
            double v;
            tok = next_token(&cursor);
            if (!tok || parse_double(tok, &v) != 0 || next_token(&cursor)) {
                err = fail(lineno, "UNDEF needs exactly one numeric value");
                break;
            }
            ctl->undef = v;
        } else if (eqi(keyword, "XDEF")) {
            if (parse_axis(&cursor, lineno, "XDEF", &ctl->nx, &ctl->xvals,
                           &ctl->xlinear) != 0) {
                err = g_err;
                break;
            }
            seen_x = 1;
        } else if (eqi(keyword, "YDEF")) {
            if (parse_axis(&cursor, lineno, "YDEF", &ctl->ny, &ctl->yvals,
                           &ctl->ylinear) != 0) {
                err = g_err;
                break;
            }
            seen_y = 1;
        } else if (eqi(keyword, "ZDEF")) {
            if (parse_axis(&cursor, lineno, "ZDEF", &ctl->nz, &ctl->zvals,
                           &ctl->zlinear) != 0) {
                err = g_err;
                break;
            }
            seen_z = 1;
        } else if (eqi(keyword, "TDEF")) {
            int n;
            char *kind, *start, *incr;
            tok = next_token(&cursor);
            if (!tok || parse_int(tok, &n) != 0 || n <= 0) {
                err = fail(lineno, "TDEF needs a positive time-step count");
                break;
            }
            kind = next_token(&cursor);
            start = next_token(&cursor);
            incr = next_token(&cursor);
            if (!kind || !eqi(kind, "LINEAR") || !start || !incr) {
                err = fail(lineno, "TDEF needs LINEAR start and increment "
                                   "(e.g. 00Z02JAN1987 1DY)");
                break;
            }
            /* Eager validation, like the reference open: bad start dates,
             * unknown units, and non-positive counts fail here. */
            {
                ng_time_t t0;
                int icount;
                ng_time_unit_t unit;
                if (strlen(start) >= sizeof(ctl->tdef_start) ||
                    ng_time_parse(start, &t0) != 0) {
                    err = fail(lineno, "TDEF has an invalid start time "
                                       "\"%s\"", start);
                    break;
                }
                if (strlen(incr) >= sizeof(ctl->tdef_incr) ||
                    ng_incr_parse(incr, &icount, &unit) != 0) {
                    err = fail(lineno, "TDEF has an invalid time increment "
                                       "\"%s\" (want N with MN/HR/DY/MO/YR,"
                                       " N >= 1)", incr);
                    break;
                }
                snprintf(ctl->tdef_start, sizeof(ctl->tdef_start), "%s",
                         start);
                snprintf(ctl->tdef_incr, sizeof(ctl->tdef_incr), "%s", incr);
            }
            ctl->nt = n;
            seen_t = 1;
        } else if (eqi(keyword, "EDEF")) {
            /* Ensemble count plus member names. Same-line names
             * (`EDEF 2 NAMES memA memB`) are stored directly; a trailing
             * bare NAMES (`EDEF 2 NAMES`, the reference form) takes one
             * name per following line. A bare `EDEF n` stores no names. */
            int want, got = 0, saw_names = 0, i;
            tok = next_token(&cursor);
            if (!tok || parse_int(tok, &want) != 0 || want <= 0) {
                err = fail(lineno, "EDEF needs a positive ensemble count");
                break;
            }
            free(ctl->ens_names);
            ctl->ens_names = NULL;
            ctl->ne = want;
            tok = next_token(&cursor);
            if (tok && eqi(tok, "NAMES")) {
                saw_names = 1;
                tok = next_token(&cursor);
            }
            while (tok) {
                if (push_ens_name(ctl, want, got, tok, lineno) != 0) {
                    err = g_err;
                    break;
                }
                got++;
                tok = next_token(&cursor);
            }
            if (err) break;
            if (got == 0 && saw_names) {
                for (i = 0; i < want; i++) {
                    char *np = NULL, *name, *rc;
                    /* Next significant line (blanks/comments skipped). */
                    do {
                        if (!fgets(line, sizeof(line), fp)) {
                            err = fail(lineno, "EDEF needs %d member names, "
                                              "found %d", want, i);
                            break;
                        }
                        lineno++;
                        if (!strchr(line, '\n') && !feof(fp)) {
                            err = fail(lineno, "line exceeds %d characters",
                                       NG_CTL_LINE_MAX);
                            break;
                        }
                        np = trim(line);
                    } while (*np == '\0' || *np == '*');
                    if (err) break;
                    rc = np;
                    name = next_token(&rc);
                    if (!name || is_directive(name)) {
                        err = fail(lineno, "EDEF needs %d member names, "
                                          "found %d", want, i);
                        break;
                    }
                    if (push_ens_name(ctl, want, i, name, lineno) != 0) {
                        err = g_err;
                        break;
                    }
                }
                if (err) break;
            }
        } else if (eqi(keyword, "VARS")) {
            int want, i;
            tok = next_token(&cursor);
            if (!tok || parse_int(tok, &want) != 0 || want <= 0 ||
                next_token(&cursor)) {
                err = fail(lineno, "VARS needs exactly one positive count");
                break;
            }
            if (want > NG_CTL_MAXVARS) {
                err = fail(lineno, "VARS count %d exceeds limit %d", want,
                           NG_CTL_MAXVARS);
                break;
            }
            for (i = 0; i < want; i++) {
                ng_var_t *var;
                char *name, *slevs, *rest, *first;
                int levs;
                lineno++;
                if (!fgets(line, sizeof(line), fp)) {
                    err = fail(lineno, "VARS section never closed with ENDVARS");
                    break;
                }
                if (!strchr(line, '\n') && !feof(fp)) {
                    err = fail(lineno, "line exceeds %d characters",
                               NG_CTL_LINE_MAX);
                    break;
                }
                p = trim(line);
                if (*p == '\0' || *p == '*') {
                    i--;  /* blanks/comments don't consume a slot */
                    continue;
                }
                cursor = p;
                name = next_token(&cursor);
                slevs = next_token(&cursor);
                if (!name || !slevs || parse_int(slevs, &levs) != 0 ||
                    levs < 0 || levs > NG_CTL_MAXLEVS) {
                    err = fail(lineno, "variable needs a name and a level "
                                       "count 0..%d", NG_CTL_MAXLEVS);
                    break;
                }
                var = &ctl->vars[ctl->nvars++];
                snprintf(var->name, sizeof(var->name), "%s", name);
                var->nlevels = levs;
                var->units[0] = '\0';
                var->code = 0;
                rest = trim(cursor);
                snprintf(var->longname, sizeof(var->longname), "%s", rest);
                first = rest;
                while (*first && !isspace((unsigned char)*first)) first++;
                {
                    char saved = *first;
                    int code;
                    *first = '\0';
                    if (parse_int(rest, &code) == 0) var->code = code;
                    *first = saved;
                }
            }
            if (err) break;
            /* Expect ENDVARS on the next non-blank, non-comment line. */
            for (;;) {
                lineno++;
                if (!fgets(line, sizeof(line), fp)) {
                    err = fail(lineno,
                               "VARS section never closed with ENDVARS");
                    break;
                }
                p = trim(line);
                if (*p == '\0' || *p == '*') continue;
                cursor = p;
                tok = next_token(&cursor);
                if (!tok || !eqi(tok, "ENDVARS")) {
                    err = fail(lineno, "expected ENDVARS, got \"%s\"",
                               tok ? tok : "");
                    break;
                }
                break;
            }
            if (err) break;
            seen_vars = 1;
        } else if (eqi(keyword, "ENDVARS")) {
            err = fail(lineno, "ENDVARS without a VARS section");
            break;
        } else if (eqi(keyword, "DTYPE")) {
            tok = next_token(&cursor);
            if (!tok || next_token(&cursor) ||
                parse_dtype(tok, &ctl->dtype) != 0) {
                err = fail(lineno, "unknown DTYPE \"%s\" "
                                   "(want binary, netcdf, hdf, hdf5, grib, "
                                   "grib2, bufr)", tok ? tok : "");
                break;
            }
        } else if (eqi(keyword, "OPTIONS")) {
            while ((tok = next_token(&cursor)) != NULL) {
                if (eqi(tok, "byteswapped")) ctl->byteswap = 1;
                else if (eqi(tok, "sequential")) ctl->sequential = 1;
                else if (eqi(tok, "template")) opt_template = 1;
                else {
                    err = fail(lineno, "unsupported OPTION \"%s\" "
                                       "(M2 supports byteswapped, sequential, "
                                       "template)", tok);
                    break;
                }
            }
            if (err) break;
        } else if (eqi(keyword, "PDEF")) {
            err = fail(lineno, "PDEF (projected grids) is not supported yet");
            break;
        } else {
            err = fail(lineno, "unknown directive \"%s\"", keyword);
            break;
        }
    }

    fclose(fp);

    if (!err) {
        if (!seen_dset) err = fail(0, "missing required DSET");
        else if (!seen_x) err = fail(0, "missing required XDEF");
        else if (!seen_y) err = fail(0, "missing required YDEF");
        else if (!seen_z) err = fail(0, "missing required ZDEF");
        else if (!seen_t) err = fail(0, "missing required TDEF");
        else if (!seen_vars) err = fail(0, "missing required VARS..ENDVARS");
    }
    if (err) {
        ng_ctl_free(ctl);
        if (err_out) *err_out = err;
        return NULL;
    }

    ctl->template = (strchr(ctl->dset, '%') != NULL) || opt_template;
    return ctl;
}

void ng_ctl_free(ng_ctl_t *ctl) {
    if (!ctl) return;
    free(ctl->xvals);
    free(ctl->yvals);
    free(ctl->zvals);
    free(ctl->ens_names);
    free(ctl);
}

/* Emit one template token; returns characters written (excluding NUL). */
static int emit_token(char *dst, size_t cap, char kind, int width,
                      int yr, int mo, int dy, int hr) {
    int v = 0;
    switch (kind) {
        case 'y': v = (width == 4) ? yr : yr % 100; break;
        case 'm': v = mo; break;
        case 'd': v = dy; break;
        case 'h': v = hr; break;
        default: return 0;
    }
    return snprintf(dst, cap, "%0*d", width, v);
}

char *ng_ctl_expand_template(const ng_ctl_t *ctl, int yr, int mo, int dy,
                             int hr) {
    const char *s;
    size_t cap, len = 0;
    char *out;

    if (!ctl) return NULL;
    /* Worst case: every byte becomes a 4-char token plus NUL. */
    cap = 4 * strlen(ctl->dset) + 1;
    out = malloc(cap);
    if (!out) return NULL;

    for (s = ctl->dset; *s; s++) {
        char tmp[16];
        int w = 0;
        if (*s != '%' || (!s[1])) {
            out[len++] = *s;
            continue;
        }
        if (s[1] == '%') {
            out[len++] = '%';
            s++;
            continue;
        }
        /* Known tokens: %y4 %y2 %m2 %m1 %d2 %d1 %h2 %h1. */
        if ((s[1] == 'y' || s[1] == 'm' || s[1] == 'd' || s[1] == 'h') &&
            (s[2] == '1' || s[2] == '2' || s[2] == '4')) {
            if (s[1] == 'y' && s[2] != '4' && s[2] != '2') goto verbatim;
            if (s[1] != 'y' && s[2] != '1' && s[2] != '2') goto verbatim;
            w = emit_token(tmp, sizeof(tmp), s[1], s[2] - '0',
                           yr, mo, dy, hr);
            if (w <= 0) goto verbatim;
            memcpy(out + len, tmp, (size_t)w);
            len += (size_t)w;
            s += 2;
            continue;
        }
verbatim:
        /* Unknown % sequences pass through verbatim (documented). */
        out[len++] = *s;
    }
    out[len] = '\0';
    return out;
}
