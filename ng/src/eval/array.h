#ifndef NG_ARRAY_H
#define NG_ARRAY_H

/*
 * ng/src/eval/array.h
 * Elementwise expression evaluation over grid slices (M3).
 *
 * An ng_array_t is one nx*ny slice, x fastest, with NaN marking missing
 * values (UNDEF converts to NaN on load; NaN propagates through every
 * operation and function, matching GrADS missing-value semantics).
 * Scalars broadcast elementwise. Shapes must match exactly: M3 evaluates a
 * single selected (t, z) slice per file, so every variable operand shares
 * the file's nx*ny grid.
 */

#include <stddef.h>

#include "grads_ng.h"
#include "parser/grads_ng_parser.h"

typedef struct {
    double *data;   /* nx*ny values, NaN = missing */
    int nx, ny;
} ng_array_t;

void ng_array_free(ng_array_t *a);

/* Evaluate a parsed single-expression AST against the file's current
 * dimension selection. Returns NULL with *err_out set (static string,
 * do not free) on undefined variables, unknown functions, shape errors,
 * or resource failure. */
ng_array_t *ng_eval_array(grads_ng_file_t *file,
                          const grads_ng_ast_node_t *ast,
                          const char **err_out);

#endif /* NG_ARRAY_H */
