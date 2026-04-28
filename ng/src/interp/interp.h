#ifndef NG_INTERP_H
#define NG_INTERP_H

/*
 * ng/src/interp/interp.h
 * GrADS scripting language (.gs) interpreter — front-end interface.
 *
 * Compatibility target: 100% of valid GrADS 2.2.1 .gs scripts.
 *
 * Architecture:
 *   Lexer  →  Token stream
 *   Parser →  AST (Abstract Syntax Tree)
 *   Eval   →  Walks AST, dispatches commands to ng_core command table
 */

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Token types (lexer output)
 * --------------------------------------------------------------------- */
typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_STRING,     /* single-quoted or bare word */
    TOK_NUMBER,     /* integer or float literal */
    TOK_IDENT,      /* variable name or keyword */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_EQ,         /* = assignment */
    TOK_EQEQ,       /* == comparison */
    TOK_NEQ,        /* != */
    TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_CONCAT,     /* # string concat */
    TOK_PERCENT,    /* % modulo */
    TOK_AMP,        /* & logical and */
    TOK_PIPE,       /* | logical or */
    TOK_BANG,       /* ! logical not */
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_ENDIF,
    TOK_KW_WHILE,
    TOK_KW_ENDWHILE,
    TOK_KW_BREAK,
    TOK_KW_RETURN,
    TOK_KW_FUNCTION,
    TOK_KW_ENDFUNC,
    TOK_KW_SAY,
    TOK_KW_PULL,
} ng_toktype_t;

typedef struct {
    ng_toktype_t type;
    char        *text;    /* heap-allocated, caller frees via ng_token_free */
    int          line;
} ng_token_t;

/* -------------------------------------------------------------------------
 * AST node types (parser output)
 * --------------------------------------------------------------------- */
typedef enum {
    ND_PROGRAM,
    ND_BLOCK,
    ND_ASSIGN,      /* var = expr */
    ND_IF,          /* if ... else ... endif */
    ND_WHILE,       /* while ... endwhile */
    ND_BREAK,
    ND_RETURN,
    ND_SAY,         /* say expr */
    ND_PULL,        /* pull var */
    ND_CALL,        /* 'cmd args...' — GrADS command call */
    ND_FUNC_DEF,    /* function name(args) ... endfunc */
    ND_FUNC_CALL,   /* name(args) */
    ND_BINOP,       /* binary expression */
    ND_UNOP,        /* unary expression */
    ND_NUMBER,
    ND_STRING,
    ND_IDENT,       /* variable reference */
} ng_ndtype_t;

typedef struct ng_node ng_node_t;
struct ng_node {
    ng_ndtype_t  type;
    char        *sval;          /* for STRING, IDENT, CALL text */
    double       nval;          /* for NUMBER */
    int          iop;           /* operator token type for BINOP/UNOP */
    ng_node_t   *left;
    ng_node_t   *right;
    ng_node_t   *alt;           /* else branch, function body, etc. */
    ng_node_t   *next;          /* sibling in block */
    int          line;
};

/* -------------------------------------------------------------------------
 * Interpreter state
 * --------------------------------------------------------------------- */
typedef struct ng_interp ng_interp_t;

/* Command handler callback: receives the full command line string,
 * returns 0 on success, non-zero on error. */
typedef int (*ng_cmd_fn)(ng_interp_t *interp, const char *cmdline, void *userdata);

ng_interp_t *ng_interp_new(void);
void         ng_interp_free(ng_interp_t *interp);

/* Register a command handler (e.g. "display", "set", "open") */
void ng_interp_register_cmd(ng_interp_t *interp, const char *name,
                            ng_cmd_fn fn, void *userdata);

/* Execute a .gs script file */
int ng_interp_exec_file(ng_interp_t *interp, const char *path);

/* Execute a string of .gs script source */
int ng_interp_exec_str(ng_interp_t *interp, const char *src);

/* Variable access (for command handlers to read/write script vars) */
int         ng_interp_set_var(ng_interp_t *interp, const char *name, const char *val);
const char *ng_interp_get_var(ng_interp_t *interp, const char *name);

/* Error reporting */
const char *ng_interp_errmsg(ng_interp_t *interp);

#endif /* NG_INTERP_H */
