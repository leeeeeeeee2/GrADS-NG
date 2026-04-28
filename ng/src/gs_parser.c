/* gs_parser.c - GrADS Script Parser implementation */

#include "gs_parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Keywords mapping */
static const char *gs_keywords[] = {
    "say", "pull", "if", "else", "endif", "while", "endwhile",
    "break", "continue", "return", "function", "display", "set",
    "open", "close", "quit", "run_python_script", NULL
};

/* Initialize parser */
int gs_parser_init(gs_parser_t *parser, const char *filename) {
    if (!parser || !filename) return -1;

    parser->file = fopen(filename, "r");
    if (!parser->file) return -1;

    parser->filename = strdup(filename);
    parser->line = 1;
    parser->column = 1;
    parser->buffer = NULL;
    parser->buffer_size = 0;
    parser->buffer_pos = 0;

    /* Initialize tokens */
    parser->current_token.type = GS_TOKEN_EOF;
    parser->current_token.text = NULL;
    parser->next_token.type = GS_TOKEN_EOF;
    parser->next_token.text = NULL;

    return 0;
}

/* Cleanup parser */
void gs_parser_cleanup(gs_parser_t *parser) {
    if (!parser) return;

    if (parser->file) fclose(parser->file);
    if (parser->filename) free(parser->filename);
    if (parser->current_token.text) free(parser->current_token.text);
    if (parser->next_token.text) free(parser->next_token.text);
    if (parser->buffer) free(parser->buffer);
}

/* Read character from file */
static int read_char(gs_parser_t *parser) {
    if (parser->buffer_pos >= parser->buffer_size) {
        /* Read next chunk */
        if (parser->buffer_size == 0) {
            parser->buffer_size = 1024;
            parser->buffer = (char *)malloc(parser->buffer_size);
        }

        size_t read_size = fread(parser->buffer, 1, parser->buffer_size, parser->file);
        if (read_size == 0) return EOF;

        parser->buffer_pos = 0;
        parser->buffer_size = read_size;
    }

    char c = parser->buffer[parser->buffer_pos++];
    if (c == '\n') {
        parser->line++;
        parser->column = 1;
    } else {
        parser->column++;
    }

    return c;
}

/* Peek character without consuming */
static int peek_char(gs_parser_t *parser) {
    if (parser->buffer_pos >= parser->buffer_size) {
        /* Need to read ahead */
        size_t current_pos = parser->buffer_pos;
        int c = read_char(parser);
        parser->buffer_pos = current_pos;  /* Restore position */
        return c;
    }
    return parser->buffer[parser->buffer_pos];
}

/* Get keyword from string */
static gs_keyword_t get_keyword(const char *str) {
    for (int i = 0; gs_keywords[i] != NULL; i++) {
        if (strcmp(str, gs_keywords[i]) == 0) {
            return (gs_keyword_t)i;
        }
    }
    return GS_KEYWORD_UNKNOWN;
}

/* Tokenize next token */
int gs_parser_next_token(gs_parser_t *parser, gs_token_t *token) {
    if (!parser || !token) return -1;

    /* Skip whitespace and comments */
    int c;
    while ((c = peek_char(parser)) != EOF) {
        if (isspace(c)) {
            read_char(parser);
            continue;
        }
        if (c == '*') {
            /* Comment - skip to end of line */
            while ((c = read_char(parser)) != EOF && c != '\n');
            continue;
        }
        break;
    }

    if (c == EOF) {
        token->type = GS_TOKEN_EOF;
        token->text = NULL;
        return 0;
    }

    token->line = parser->line;
    token->column = parser->column;

    /* Handle different token types */
    if (isalpha(c) || c == '_') {
        /* Identifier or keyword */
        char buffer[256];
        int i = 0;

        while (i < sizeof(buffer) - 1 &&
               (isalnum(c) || c == '_') &&
               (c = read_char(parser)) != EOF) {
            buffer[i++] = c;
        }
        buffer[i] = '\0';

        token->text = strdup(buffer);
        token->keyword = get_keyword(buffer);

        if (token->keyword != GS_KEYWORD_UNKNOWN) {
            token->type = GS_TOKEN_KEYWORD;
        } else {
            token->type = GS_TOKEN_IDENTIFIER;
        }

        return 0;
    }

    if (isdigit(c) || c == '.' || c == '-') {
        /* Number */
        char buffer[256];
        int i = 0;

        while (i < sizeof(buffer) - 1 &&
               (isdigit(c) || c == '.' || c == '-' || c == 'e' || c == 'E') &&
               (c = read_char(parser)) != EOF) {
            buffer[i++] = c;
        }
        buffer[i] = '\0';

        token->text = strdup(buffer);
        token->type = GS_TOKEN_NUMBER;
        token->number = strtod(buffer, NULL);

        return 0;
    }

    if (c == '"' || c == '\'') {
        /* String */
        char quote = c;
        read_char(parser);  /* Consume quote */

        char buffer[1024];
        int i = 0;

        while (i < sizeof(buffer) - 1 &&
               (c = read_char(parser)) != EOF &&
               c != quote) {
            buffer[i++] = c;
        }
        buffer[i] = '\0';

        token->text = strdup(buffer);
        token->type = GS_TOKEN_STRING;

        return 0;
    }

    /* Single character tokens */
    read_char(parser);

    switch (c) {
        case '(': token->type = GS_TOKEN_PAREN_OPEN; break;
        case ')': token->type = GS_TOKEN_PAREN_CLOSE; break;
        case '{': token->type = GS_TOKEN_BRACE_OPEN; break;
        case '}': token->type = GS_TOKEN_BRACE_CLOSE; break;
        case ';': token->type = GS_TOKEN_SEMICOLON; break;
        case '\n': token->type = GS_TOKEN_NEWLINE; break;
        default:
            token->type = GS_TOKEN_OPERATOR;
            token->text = (char *)malloc(2);
            token->text[0] = c;
            token->text[1] = '\0';
            break;
    }

    return 0;
}

/* Peek at next token without consuming */
int gs_parser_peek_token(gs_parser_t *parser, gs_token_t *token) {
    if (!parser || !token) return -1;

    /* Save current state */
    gs_token_t saved_token = parser->current_token;

    /* Get next token */
    int result = gs_parser_next_token(parser, token);

    /* Restore state */
    parser->current_token = saved_token;

    return result;
}

/* Context management */
int gs_context_init(gs_context_t *ctx) {
    if (!ctx) return -1;

    ctx->variables = NULL;
    ctx->functions = NULL;
    ctx->running = 1;
    ctx->result_code = 0;

    return 0;
}

void gs_context_cleanup(gs_context_t *ctx) {
    if (!ctx) return;

    /* Clean up variables */
    gs_var_t *var = ctx->variables;
    while (var) {
        gs_var_t *next = var->next;
        if (var->type == GS_VAR_STRING && var->value.str_val) {
            free(var->value.str_val);
        } else if (var->type == GS_VAR_ARRAY && var->value.array_val.data) {
            free(var->value.array_val.data);
        }
        free(var);
        var = next;
    }

    /* Clean up functions */
    gs_function_t *func = ctx->functions;
    while (func) {
        gs_function_t *next = func->next;
        if (func->tokens) free(func->tokens);
        free(func);
        func = next;
    }
}

gs_var_t *gs_context_get_var(gs_context_t *ctx, const char *name) {
    if (!ctx || !name) return NULL;

    gs_var_t *var = ctx->variables;
    while (var) {
        if (strcmp(var->name, name) == 0) {
            return var;
        }
        var = var->next;
    }

    return NULL;
}

int gs_context_set_var(gs_context_t *ctx, const char *name, gs_var_type_t type, void *value) {
    if (!ctx || !name) return -1;

    gs_var_t *var = gs_context_get_var(ctx, name);
    if (!var) {
        /* Create new variable */
        var = (gs_var_t *)malloc(sizeof(gs_var_t));
        if (!var) return -1;

        strncpy(var->name, name, sizeof(var->name) - 1);
        var->type = type;
        var->next = ctx->variables;
        ctx->variables = var;
    }

    /* Set value */
    switch (type) {
        case GS_VAR_STRING:
            if (var->type == GS_VAR_STRING && var->value.str_val) {
                free(var->value.str_val);
            }
            var->value.str_val = strdup((const char *)value);
            break;
        case GS_VAR_NUMBER:
            var->value.num_val = *(double *)value;
            break;
        case GS_VAR_ARRAY:
            /* TODO: Implement array support */
            break;
    }

    var->type = type;
    return 0;
}

/* Execute script */
int gs_execute_script(gs_context_t *ctx, const char *filename) {
    /* TODO: Implement full script execution */
    printf("Executing script: %s\n", filename);
    return 0;
}

/* Execute tokens */
int gs_execute_tokens(gs_context_t *ctx, gs_token_t *tokens, int count) {
    /* TODO: Implement token execution */
    printf("Executing %d tokens\n", count);
    return 0;
}

/* NG extension: Execute Python script */
int gs_execute_python_script(gs_context_t *ctx, const char *script_path) {
    /* TODO: Implement Python integration */
    printf("Executing Python script: %s\n", script_path);
    return 0;
}