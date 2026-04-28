/*
 * GrADS Script Lexer/Parser for GrADS-NG
 * 
 * Parses GrADS Scripting Language (.gs files)
 * Supports: say, pull, if...else...endif, while, variable definition
 */

#ifndef GRADS_NG_PARSER_H
#define GRADS_NG_PARSER_H

#include <stddef.h>

/* Token types */
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_STRING,
    TOK_NUMBER,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMICOLON,
    TOK_EQUAL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_CARET,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_EQ,
    TOK_NE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    /* Keywords */
    TOK_SAY,
    TOK_PULL,
    TOK_IF,
    TOK_ELSE,
    TOK_ELSEIF,
    TOK_ENDIF,
    TOK_WHILE,
    TOK_ENDWHILE,
    TOK_FOR,
    TOK_ENDFOR,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_RETURN,
    TOK_EXIT,
    TOK_RUN,
    TOK_CLEAR,
    TOK_DISPLAY,
    TOK_SET,
    TOK_QUERY,
    TOK_DRAW,
    TOK_PLOT,
    TOK_OPEN,
    TOK_CLOSE,
    TOK_DEFINE,
    TOK_EOF_MARKER
} grads_ng_token_type_t;

/* Token structure */
typedef struct {
    grads_ng_token_type_t type;
    char* text;
    int text_len;
    double number;
    int line;
    int col;
} grads_ng_token_t;

/* AST node types */
typedef enum {
    AST_PROGRAM,
    AST_BLOCK,
    AST_EXPR_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_RETURN_STMT,
    AST_SAY_STMT,
    AST_PULL_STMT,
    AST_RUN_STMT,
    AST_SET_STMT,
    AST_DISPLAY_STMT,
    AST_IDENT_EXPR,
    AST_NUMBER_EXPR,
    AST_STRING_EXPR,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_ARRAY_EXPR
} grads_ng_ast_type_t;

/* AST node */
typedef struct grads_ng_ast_node {
    grads_ng_ast_type_t type;
    struct grads_ng_ast_node* left;
    struct grads_ng_ast_node* right;
    struct grads_ng_ast_node* next;  /* For lists */
    union {
        double number;
        char* string;
        struct {
            char* name;
            int argc;
            struct grads_ng_ast_node** argv;
        } call;
    } value;
} grads_ng_ast_node_t;

/* Lexer */
typedef struct {
    const char* source;
    size_t source_len;
    size_t pos;
    int line;
    int col;
    grads_ng_token_t current;
    char err_msg[256];
} grads_ng_lexer_t;

/* Parser */
typedef struct {
    grads_ng_lexer_t* lexer;
    grads_ng_ast_node_t* program;
    int has_error;
    char err_msg[512];
} grads_ng_parser_t;

/* Lexer functions */
grads_ng_lexer_t* grads_ng_lexer_create(const char* source);
void grads_ng_lexer_destroy(grads_ng_lexer_t* lexer);
int grads_ng_lexer_next(grads_ng_lexer_t* lexer);
grads_ng_token_t* grads_ng_lexer_current(grads_ng_lexer_t* lexer);
int grads_ng_lexer_expect(grads_ng_lexer_t* lexer, grads_ng_token_type_t type);
void grads_ng_lexer_advance(grads_ng_lexer_t* lexer);

/* Parser functions */
grads_ng_parser_t* grads_ng_parser_create(const char* source);
void grads_ng_parser_destroy(grads_ng_parser_t* parser);
grads_ng_ast_node_t* grads_ng_parser_parse(grads_ng_parser_t* parser);
int grads_ng_parser_has_error(grads_ng_parser_t* parser);
const char* grads_ng_parser_error(grads_ng_parser_t* parser);

/* AST functions */
grads_ng_ast_node_t* grads_ng_ast_create(grads_ng_ast_type_t type);
void grads_ng_ast_destroy(grads_ng_ast_node_t* node);

/* Interpreter (runtime) */
typedef struct {
    void* variables;
    int num_vars;
    void* file_handles[16];
    int current_file;
} grads_ng_vm_t;

grads_ng_vm_t* grads_ng_vm_create(void);
void grads_ng_vm_destroy(grads_ng_vm_t* vm);
int grads_ng_vm_exec(grads_ng_vm_t* vm, grads_ng_ast_node_t* program);

#endif /* GRADS_NG_PARSER_H */