/* test_expr.c - Unit tests for the GrADS-NG expression lexer/parser/evaluator.
 *
 * Covers M1 scalar arithmetic: precedence, right-associative power,
 * comparisons, logical operators, and honest errors (undefined variables,
 * division by zero). Run via ctest.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "parser/grads_ng_parser.h"

static int failures = 0;
static int checks = 0;

static void check_value(const char* input, double expected) {
    grads_ng_parser_t* parser = grads_ng_parser_create(input);
    grads_ng_ast_node_t* ast;
    double got;
    char err[256];

    checks++;
    if (!parser) {
        printf("FAIL %-22s (no parser)\n", input);
        failures++;
        return;
    }
    ast = grads_ng_parser_parse(parser);
    if (!ast) {
        printf("FAIL %-22s parse error: %s\n", input,
               grads_ng_parser_error(parser) ? grads_ng_parser_error(parser) : "?");
        failures++;
        grads_ng_parser_destroy(parser);
        return;
    }
    if (grads_ng_ast_eval(ast, &got, err, sizeof(err)) != 0) {
        printf("FAIL %-22s eval error: %s\n", input, err);
        failures++;
    } else if (fabs(got - expected) > 1e-9 * (1.0 + fabs(expected))) {
        printf("FAIL %-22s got %.15g want %.15g\n", input, got, expected);
        failures++;
    } else {
        printf("ok   %-22s = %.15g\n", input, got);
    }
    grads_ng_ast_destroy(ast);
    grads_ng_parser_destroy(parser);
}

static void check_error(const char* input, const char* want_substr) {
    grads_ng_parser_t* parser = grads_ng_parser_create(input);
    grads_ng_ast_node_t* ast;
    double got;
    char err[256] = {0};

    checks++;
    if (!parser) {
        printf("FAIL %-22s (no parser)\n", input);
        failures++;
        return;
    }
    ast = grads_ng_parser_parse(parser);
    if (!ast) {
        /* A parse-time rejection is also an honest error. */
        const char* perr = grads_ng_parser_error(parser);
        if (perr && want_substr && strstr(perr, want_substr)) {
            printf("ok   %-22s parse error (as expected): %s\n", input, perr);
        } else {
            printf("FAIL %-22s unexpected parse error: %s\n", input,
                   perr ? perr : "?");
            failures++;
        }
        grads_ng_parser_destroy(parser);
        return;
    }
    if (grads_ng_ast_eval(ast, &got, err, sizeof(err)) == 0) {
        printf("FAIL %-22s evaluated to %.15g, want error (%s)\n",
               input, got, want_substr ? want_substr : "?");
        failures++;
    } else if (want_substr && !strstr(err, want_substr)) {
        printf("FAIL %-22s error %s, want substring %s\n", input, err, want_substr);
        failures++;
    } else {
        printf("ok   %-22s error (as expected): %s\n", input, err);
    }
    grads_ng_ast_destroy(ast);
    grads_ng_parser_destroy(parser);
}

int main(void) {
    /* Arithmetic + precedence (unary -> ^ -> * / -> + - -> cmp -> && -> ||). */
    check_value("2+3*4", 14.0);
    check_value("(2+3)*4", 20.0);
    check_value("10-4-3", 3.0);
    check_value("7/2", 3.5);
    check_value("2*3^2", 18.0);
    check_value("2^3^2", 512.0);       /* right-associative power */
    check_value("-2^2", -4.0);         /* unary binds looser than power */
    check_value("2^-3", 0.125);
    check_value("--5", 5.0);
    check_value(".5*2", 1.0);
    check_value("1e3+1", 1001.0);
    check_value("1.5e-3*1000", 1.5);
    check_value("(273.16-273.16)*9/5+32", 32.0); /* K->F shape, scalar analog */
    check_value("0", 0.0);

    /* Comparisons yield 1/0. */
    check_value("1+2==3", 1.0);
    check_value("1+2<>3", 0.0);
    check_value("2!=3", 1.0);
    check_value("3<=3", 1.0);
    check_value("4>5", 0.0);
    check_value("4>=4", 1.0);

    /* Logic with GrADS truthiness. */
    check_value("1 && 2", 1.0);
    check_value("1 && 0", 0.0);
    check_value("0 || 0", 0.0);
    check_value("0 || 5", 1.0);
    check_value("!0", 1.0);
    check_value("!3", 0.0);
    check_value("1+1==2 && 2*2==4", 1.0);
    check_value("1 || 0 && 0", 1.0);   /* && binds tighter than || */

    /* Function calls (scalar). */
    check_value("abs(-3)", 3.0);
    check_value("sqrt(16)", 4.0);
    check_value("sin(0)", 0.0);
    check_value("ABS(0-5)+1", 6.0);
    check_value("pow(2,10)", 1024.0);
    check_value("pow(9,0.5)", 3.0);
    check_error("frobnicate(1)", "unknown function");
    check_error("abs(1,2)", "1 argument");
    /* max/min/ave are dimension reductions, not scalar functions. */
    check_error("max(3,7)", "reduces over a dimension range");
    check_error("ave(tsfc,t=1,t=2)", "reduces over a dimension range");
    check_error("sqrt(-1)", "negative");
    check_error("sin(1", "Expected )");

    /* Honest errors: never a silent wrong number. */
    check_error("tsfc", "not defined");
    check_error("1/0", "division by zero");
    check_error("(1+2", "Expected )");
    check_error("1 & 2", "uses '&&'");
    check_error("'abc'+1", "string");
    check_error("1 2", "single expression");
    check_error("2+2 3", "single expression");
    check_error("1 + +", "Unexpected token");  /* used to hang forever */
    check_error("2 *", "Unexpected token");    /* used to hang forever */

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
