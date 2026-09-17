/*
 * ng/src/eval/array.c
 * Elementwise expression evaluation over grid slices.
 *
 * Missing-value rules (GrADS-compatible):
 * - variable load converts float32 UNDEF to NaN;
 * - any arithmetic/logic op with a NaN input yields NaN (comparisons too);
 * - functions propagate NaN; domain errors (sqrt(-1), log(0), 0/0, x/0,
 *   overflow) yield NaN rather than failing the whole display;
 * - unknown variables/functions and shape mismatches are hard errors.
 */

#include "array.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_err[512];

static const char *fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
    return g_err;
}

void ng_array_free(ng_array_t *a) {
    if (!a) return;
    free(a->data);
    free(a);
}

static ng_array_t *array_new(int nx, int ny) {
    ng_array_t *a = malloc(sizeof(*a));
    if (!a) return NULL;
    a->data = malloc((size_t)nx * (size_t)ny * sizeof(*a->data));
    if (!a->data) {
        free(a);
        return NULL;
    }
    a->nx = nx;
    a->ny = ny;
    return a;
}

static ng_array_t *array_const(int nx, int ny, double v) {
    ng_array_t *a = array_new(nx, ny);
    long n, i;
    if (!a) return NULL;
    n = (long)nx * ny;
    for (i = 0; i < n; i++) a->data[i] = v;
    return a;
}

typedef struct {
    grads_ng_file_t *file;
    int nx, ny, t, z;
    const char **err_out;
} eval_ctx_t;

/* Load a variable slice, converting UNDEF to NaN. */
static ng_array_t *load_var(eval_ctx_t *ctx, const char *name) {
    grads_ng_var_t *var;
    ng_array_t *a;
    char emsg[256];
    long n, i;
    float undef_f;

    var = grads_ng_get_var(ctx->file, name);
    if (!var) {
        *ctx->err_out = fail("variable \"%s\" is not defined", name);
        return NULL;
    }
    a = array_new(ctx->nx, ctx->ny);
    if (!a) {
        *ctx->err_out = fail("out of memory");
        return NULL;
    }
    if (grads_ng_var_slice(var, ctx->t, ctx->z, a->data, emsg,
                           sizeof(emsg)) != 0) {
        *ctx->err_out = fail("%s", emsg);
        ng_array_free(a);
        return NULL;
    }
    undef_f = (float)grads_ng_var_undef(var);
    n = (long)ctx->nx * ctx->ny;
    for (i = 0; i < n; i++) {
        if ((float)a->data[i] == undef_f) a->data[i] = NAN;
    }
    return a;
}

static int check_shape(eval_ctx_t *ctx, const ng_array_t *a) {
    if (a->nx != ctx->nx || a->ny != ctx->ny) {
        *ctx->err_out = fail("shape mismatch: %dx%d against %dx%d",
                             a->nx, a->ny, ctx->nx, ctx->ny);
        return -1;
    }
    return 0;
}

/* Elementwise binary op with NaN propagation. */
static ng_array_t *binop(eval_ctx_t *ctx, int op, ng_array_t *l,
                         ng_array_t *r) {
    long n = (long)ctx->nx * ctx->ny;
    long i;
    ng_array_t *o;

    if (check_shape(ctx, l) != 0 || check_shape(ctx, r) != 0) return NULL;
    o = array_new(ctx->nx, ctx->ny);
    if (!o) {
        *ctx->err_out = fail("out of memory");
        return NULL;
    }
    for (i = 0; i < n; i++) {
        double a = l->data[i], b = r->data[i];
        double v = NAN;
        if (!isnan(a) && !isnan(b)) {
            switch (op) {
                case TOK_PLUS: v = a + b; break;
                case TOK_MINUS: v = a - b; break;
                case TOK_STAR: v = a * b; break;
                case TOK_SLASH: v = (b == 0.0) ? NAN : a / b; break;
                case TOK_CARET:
                    v = pow(a, b);
                    if (!isfinite(v)) v = NAN;
                    break;
                case TOK_LT: v = (a < b) ? 1.0 : 0.0; break;
                case TOK_LE: v = (a <= b) ? 1.0 : 0.0; break;
                case TOK_GT: v = (a > b) ? 1.0 : 0.0; break;
                case TOK_GE: v = (a >= b) ? 1.0 : 0.0; break;
                case TOK_EQ: v = (a == b) ? 1.0 : 0.0; break;
                case TOK_NE: v = (a != b) ? 1.0 : 0.0; break;
                case TOK_AND: v = ((a != 0.0) && (b != 0.0)) ? 1.0 : 0.0; break;
                case TOK_OR: v = ((a != 0.0) || (b != 0.0)) ? 1.0 : 0.0; break;
                default:
                    ng_array_free(o);
                    *ctx->err_out = fail("unknown binary operator");
                    return NULL;
            }
            if (!isfinite(v) && op != TOK_LT && op != TOK_LE &&
                op != TOK_GT && op != TOK_GE && op != TOK_EQ &&
                op != TOK_NE && op != TOK_AND && op != TOK_OR)
                v = NAN;  /* overflowed arithmetic becomes missing */
        }
        o->data[i] = v;
    }
    return o;
}

static ng_array_t *unop(eval_ctx_t *ctx, int op, ng_array_t *l) {
    long n = (long)ctx->nx * ctx->ny;
    long i;
    ng_array_t *o;

    if (check_shape(ctx, l) != 0) return NULL;
    o = array_new(ctx->nx, ctx->ny);
    if (!o) {
        *ctx->err_out = fail("out of memory");
        return NULL;
    }
    for (i = 0; i < n; i++) {
        double a = l->data[i];
        double v = NAN;
        if (!isnan(a)) {
            if (op == TOK_MINUS) v = -a;
            else if (op == TOK_NOT) v = (a == 0.0) ? 1.0 : 0.0;
            else {
                ng_array_free(o);
                *ctx->err_out = fail("unknown unary operator");
                return NULL;
            }
        }
        o->data[i] = v;
    }
    return o;
}

static ng_array_t *eval_node(eval_ctx_t *ctx,
                             const grads_ng_ast_node_t *node) {
    ng_array_t *l, *r, *o;
    char emsg[256];
    long n, i;

    if (!node) {
        *ctx->err_out = fail("cannot evaluate an empty expression");
        return NULL;
    }
    switch (node->type) {
        case AST_NUMBER_EXPR:
            return array_const(ctx->nx, ctx->ny, node->value.number);

        case AST_IDENT_EXPR:
            return load_var(ctx, node->value.string ? node->value.string
                                                    : "?");

        case AST_STRING_EXPR:
            *ctx->err_out = fail("a string cannot be used as a number here");
            return NULL;

        case AST_UNARY_EXPR:
            l = eval_node(ctx, node->left);
            if (!l) return NULL;
            o = unop(ctx, node->op, l);
            ng_array_free(l);
            return o;

        case AST_BINARY_EXPR:
            l = eval_node(ctx, node->left);
            if (!l) return NULL;
            r = eval_node(ctx, node->right);
            if (!r) {
                ng_array_free(l);
                return NULL;
            }
            o = binop(ctx, node->op, l, r);
            ng_array_free(l);
            ng_array_free(r);
            return o;

        case AST_CALL_EXPR: {
            /* Elementwise N-argument math calls (max/min/pow + unary set).
             * Unknown names and wrong arity are programming errors (hard
             * fail); domain errors on real data become missing. */
            const char *fname = node->value.call.name;
            int argc = node->value.call.argc;
            int arity = grads_ng_math_arity(fname);
            ng_array_t **args;
            ng_array_t *out;
            double *vals;
            int k;
            if (arity < 0) {
                *ctx->err_out = fail("unknown function \"%s\"",
                                     fname ? fname : "?");
                return NULL;
            }
            if (arity != argc || argc > 16) {
                *ctx->err_out = fail("%s takes %d argument(s) (%d given)",
                                     fname ? fname : "?", arity, argc);
                return NULL;
            }
            args = malloc((size_t)argc * sizeof(*args));
            vals = malloc((size_t)argc * sizeof(*vals));
            if (!args || !vals) {
                free(args);
                free(vals);
                *ctx->err_out = fail("out of memory");
                return NULL;
            }
            for (k = 0; k < argc; k++) {
                args[k] = eval_node(ctx, node->value.call.argv[k]);
                if (!args[k] || check_shape(ctx, args[k]) != 0) {
                    while (--k >= 0) ng_array_free(args[k]);
                    free(args);
                    free(vals);
                    return NULL;
                }
            }
            out = array_new(ctx->nx, ctx->ny);
            if (!out) {
                for (k = 0; k < argc; k++) ng_array_free(args[k]);
                free(args);
                free(vals);
                *ctx->err_out = fail("out of memory");
                return NULL;
            }
            n = (long)ctx->nx * ctx->ny;
            for (i = 0; i < n; i++) {
                double one;
                for (k = 0; k < argc; k++) vals[k] = args[k]->data[i];
                if (grads_ng_math_apply(fname, vals, argc, &one,
                                        emsg, sizeof(emsg)) != 0)
                    one = NAN;
                out->data[i] = one;
            }
            for (k = 0; k < argc; k++) ng_array_free(args[k]);
            free(args);
            free(vals);
            return out;
        }

        default:
            *ctx->err_out =
                fail("this statement cannot be evaluated as grid data");
            return NULL;
    }
}

ng_array_t *ng_eval_array(grads_ng_file_t *file,
                          const grads_ng_ast_node_t *ast,
                          const char **err_out) {
    static const char *noerr = NULL;
    eval_ctx_t ctx;
    const grads_ng_ast_node_t *stmt;
    int nx, ny, nz, nt;

    if (err_out) *err_out = NULL;
    else err_out = &noerr;
    if (!file || !ast) {
        *err_out = fail("cannot evaluate without a file and expression");
        return NULL;
    }
    if (grads_ng_file_dims(file, &nx, &ny, &nz, &nt) != 0) {
        *err_out = fail("cannot describe the open file");
        return NULL;
    }
    ctx.file = file;
    ctx.nx = nx;
    ctx.ny = ny;
    ctx.err_out = err_out;
    if (grads_ng_file_selected(file, &ctx.t, &ctx.z) != 0) {
        ctx.t = 0;
        ctx.z = 0;
    }
    /* Unwrap a parsed program: exactly one expression statement. */
    stmt = ast;
    if (stmt->type == AST_PROGRAM) {
        stmt = stmt->left;
        if (!stmt) {
            *err_out = fail("empty expression");
            return NULL;
        }
        if (stmt->next) {
            *err_out = fail("expected a single expression");
            return NULL;
        }
    }
    return eval_node(&ctx, stmt);
}
