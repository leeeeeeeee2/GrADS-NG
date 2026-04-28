/* gs_parser.h - GrADS Script Parser for NG */

#ifndef GS_PARSER_H
#define GS_PARSER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Token types */
typedef enum {
    GS_TOKEN_EOF = 0,
    GS_TOKEN_IDENTIFIER,
    GS_TOKEN_STRING,
    GS_TOKEN_NUMBER,
    GS_TOKEN_KEYWORD,
    GS_TOKEN_OPERATOR,
    GS_TOKEN_PAREN_OPEN,
    GS_TOKEN_PAREN_CLOSE,
    GS_TOKEN_BRACE_OPEN,
    GS_TOKEN_BRACE_CLOSE,
    GS_TOKEN_SEMICOLON,
    GS_TOKEN_NEWLINE
} gs_token_type_t;

/* Keywords */
typedef enum {
    GS_KEYWORD_SAY = 0,
    GS_KEYWORD_PULL,
    GS_KEYWORD_IF,
    GS_KEYWORD_ELSE,
    GS_KEYWORD_ENDIF,
    GS_KEYWORD_WHILE,
    GS_KEYWORD_ENDWHILE,
    GS_KEYWORD_BREAK,
    GS_KEYWORD_CONTINUE,
    GS_KEYWORD_RETURN,
    GS_KEYWORD_FUNCTION,
    GS_KEYWORD_DISPLAY,
    GS_KEYWORD_SET,
    GS_KEYWORD_OPEN,
    GS_KEYWORD_CLOSE,
    GS_KEYWORD_QUIT,
    GS_KEYWORD_RUN_PYTHON_SCRIPT,  /* NG extension */
    GS_KEYWORD_UNKNOWN
} gs_keyword_t;

/* Token structure */
typedef struct {
    gs_token_type_t type;
    gs_keyword_t keyword;
    char *text;
    double number;
    int line;
    int column;
} gs_token_t;

/* Parser context */
typedef struct {
    FILE *file;
    char *filename;
    int line;
    int column;
    gs_token_t current_token;
    gs_token_t next_token;
    char *buffer;
    size_t buffer_size;
    size_t buffer_pos;
} gs_parser_t;

/* Variable types */
typedef enum {
    GS_VAR_STRING,
    GS_VAR_NUMBER,
    GS_VAR_ARRAY
} gs_var_type_t;

/* Variable structure */
typedef struct gs_var {
    char name[16];
    gs_var_type_t type;
    union {
        char *str_val;
        double num_val;
        struct {
            double *data;
            int size;
        } array_val;
    } value;
    struct gs_var *next;
} gs_var_t;

/* Function structure */
typedef struct gs_function {
    char name[16];
    gs_token_t *tokens;
    int token_count;
    struct gs_function *next;
} gs_function_t;

/* Execution context */
typedef struct {
    gs_var_t *variables;
    gs_function_t *functions;
    int running;
    int result_code;
} gs_context_t;

/* Parser functions */
int gs_parser_init(gs_parser_t *parser, const char *filename);
void gs_parser_cleanup(gs_parser_t *parser);
int gs_parser_next_token(gs_parser_t *parser, gs_token_t *token);
int gs_parser_peek_token(gs_parser_t *parser, gs_token_t *token);

/* Context functions */
int gs_context_init(gs_context_t *ctx);
void gs_context_cleanup(gs_context_t *ctx);
gs_var_t *gs_context_get_var(gs_context_t *ctx, const char *name);
int gs_context_set_var(gs_context_t *ctx, const char *name, gs_var_type_t type, void *value);

/* Execution functions */
int gs_execute_script(gs_context_t *ctx, const char *filename);
int gs_execute_tokens(gs_context_t *ctx, gs_token_t *tokens, int count);

/* NG extensions */
int gs_execute_python_script(gs_context_t *ctx, const char *script_path);

#ifdef __cplusplus
}
#endif

#endif /* GS_PARSER_H */