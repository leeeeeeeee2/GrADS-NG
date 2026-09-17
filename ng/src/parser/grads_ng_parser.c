/*
 * GrADS Script Lexer/Parser Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "parser/grads_ng_parser.h"

/* Keyword table */
static const char* keywords[] = {
    "say", "pull", "if", "else", "elseif", "endif", "while", "endwhile",
    "for", "endfor", "break", "continue", "return", "exit", "run",
    "clear", "display", "set", "query", "draw", "plot", "open", "close",
    "define", NULL
};

static const int keyword_tokens[] = {
    TOK_SAY, TOK_PULL, TOK_IF, TOK_ELSE, TOK_ELSEIF, TOK_ENDIF,
    TOK_WHILE, TOK_ENDWHILE, TOK_FOR, TOK_ENDFOR, TOK_BREAK, TOK_CONTINUE,
    TOK_RETURN, TOK_EXIT, TOK_RUN, TOK_CLEAR, TOK_DISPLAY, TOK_SET,
    TOK_QUERY, TOK_DRAW, TOK_PLOT, TOK_OPEN, TOK_CLOSE, TOK_DEFINE
};

/* Helper: is identifier start character */
static int is_ident_start(int c) {
    return isalpha(c) || c == '_' || c == '$';
}

/* Helper: is identifier character */
static int is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '$';
}

/* Create lexer */
grads_ng_lexer_t* grads_ng_lexer_create(const char* source) {
    grads_ng_lexer_t* lexer;
    
    if (!source) return NULL;
    
    lexer = calloc(1, sizeof(grads_ng_lexer_t));
    if (!lexer) return NULL;
    
    lexer->source = source;
    lexer->source_len = strlen(source);
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 0;
    
    /* Initialize first token */
    if (grads_ng_lexer_next(lexer) != 0) lexer->err = 1;

    return lexer;
}

/* Destroy lexer */
void grads_ng_lexer_destroy(grads_ng_lexer_t* lexer) {
    if (!lexer) return;
    free(lexer->current.text);
    free(lexer);
}

/* Get current token */
grads_ng_token_t* grads_ng_lexer_current(grads_ng_lexer_t* lexer) {
    if (!lexer) return NULL;
    return &lexer->current;
}

/* Skip whitespace and comments */
static void skip_whitespace(grads_ng_lexer_t* lexer) {
    while (lexer->pos < lexer->source_len) {
        int c = lexer->source[lexer->pos];
        
        /* Skip whitespace */
        if (isspace(c)) {
            if (c == '\n') {
                lexer->line++;
                lexer->col = 0;
            }
            lexer->pos++;
            lexer->col++;
            continue;
        }
        
        /* Skip comments */
        if (c == '*' && lexer->pos + 1 < lexer->source_len && 
            lexer->source[lexer->pos + 1] == '*') {
            /* Skip to end of line */
            while (lexer->pos < lexer->source_len && 
                   lexer->source[lexer->pos] != '\n') {
                lexer->pos++;
            }
            continue;
        }
        
        break;
    }
}

/* Read a string literal */
static int read_string(grads_ng_lexer_t* lexer, char delim) {
    size_t start = lexer->pos + 1;
    size_t end = start;
    
    while (end < lexer->source_len && lexer->source[end] != delim) {
        if (lexer->source[end] == '\\' && end + 1 < lexer->source_len) {
            end += 2;  /* Skip escaped character */
            continue;
        }
        end++;
    }
    
    if (end >= lexer->source_len) {
        snprintf(lexer->err_msg, sizeof(lexer->err_msg),
                "Unterminated string at line %d", lexer->line);
        return -1;
    }
    
    lexer->current.text_len = end - start;
    lexer->current.text = malloc(lexer->current.text_len + 1);
    if (!lexer->current.text) return -1;
    
    strncpy(lexer->current.text, lexer->source + start, lexer->current.text_len);
    lexer->current.text[lexer->current.text_len] = '\0';
    lexer->pos = end + 1;
    
    return 0;
}

/* Read a number */
static int read_number(grads_ng_lexer_t* lexer) {
    size_t start = lexer->pos;
    
    /* Integer part */
    while (lexer->pos < lexer->source_len && isdigit(lexer->source[lexer->pos])) {
        lexer->pos++;
    }
    
    /* Fractional part */
    if (lexer->pos < lexer->source_len && lexer->source[lexer->pos] == '.') {
        lexer->pos++;
        while (lexer->pos < lexer->source_len && isdigit(lexer->source[lexer->pos])) {
            lexer->pos++;
        }
    }
    
    /* Exponent part */
    if (lexer->pos < lexer->source_len && 
        (lexer->source[lexer->pos] == 'e' || lexer->source[lexer->pos] == 'E')) {
        lexer->pos++;
        if (lexer->pos < lexer->source_len && 
            (lexer->source[lexer->pos] == '+' || lexer->source[lexer->pos] == '-')) {
            lexer->pos++;
        }
        while (lexer->pos < lexer->source_len && isdigit(lexer->source[lexer->pos])) {
            lexer->pos++;
        }
    }
    
    /* Parse the number */
    size_t len = lexer->pos - start;
    char* buf = malloc(len + 1);
    if (!buf) return -1;
    
    strncpy(buf, lexer->source + start, len);
    buf[len] = '\0';
    
    lexer->current.number = atof(buf);
    free(buf);
    
    return 0;
}

/* Read an identifier or keyword */
static void read_ident(grads_ng_lexer_t* lexer) {
    size_t start = lexer->pos;
    
    while (lexer->pos < lexer->source_len && is_ident_char(lexer->source[lexer->pos])) {
        lexer->pos++;
    }
    
    lexer->current.text_len = lexer->pos - start;
    lexer->current.text = malloc(lexer->current.text_len + 1);
    if (!lexer->current.text) return;
    
    strncpy(lexer->current.text, lexer->source + start, lexer->current.text_len);
    lexer->current.text[lexer->current.text_len] = '\0';
    
    /* Check if it's a keyword */
    int i = 0;
    while (keywords[i]) {
        if (strcmp(lexer->current.text, keywords[i]) == 0) {
            lexer->current.type = keyword_tokens[i];
            break;
        }
        i++;
    }
    
    if (keywords[i] == NULL) {
        lexer->current.type = TOK_IDENT;
    }
}

/* Get next token */
int grads_ng_lexer_next(grads_ng_lexer_t* lexer) {
    if (!lexer) return -1;
    
    /* Free previous token text */
    free(lexer->current.text);
    lexer->current.text = NULL;
    lexer->current.text_len = 0;
    
    /* Skip whitespace */
    skip_whitespace(lexer);
    
    if (lexer->pos >= lexer->source_len) {
        lexer->current.type = TOK_EOF;
        return 0;
    }
    
    int c = lexer->source[lexer->pos];
    lexer->col = lexer->pos;
    
    /* String literal */
    if (c == '"' || c == '\'') {
        if (read_string(lexer, c) < 0) return -1;
        lexer->current.type = TOK_STRING;
        return 0;
    }
    
    /* Number (integers, floats, scientific notation, leading-dot like .5) */
    if (isdigit(c) ||
        (c == '.' && lexer->pos + 1 < lexer->source_len &&
         isdigit((unsigned char)lexer->source[lexer->pos + 1]))) {
        if (read_number(lexer) < 0) return -1;
        lexer->current.type = TOK_NUMBER;
        return 0;
    }
    
    /* Identifier or keyword */
    if (is_ident_start(c)) {
        read_ident(lexer);
        return 0;
    }
    
    /* Single character tokens */
    lexer->pos++;
    lexer->current.type = c;
    lexer->current.text_len = 0;
    lexer->current.text = NULL;
    
    switch (c) {
        case '(': lexer->current.type = TOK_LPAREN; break;
        case ')': lexer->current.type = TOK_RPAREN; break;
        case '{': lexer->current.type = TOK_LBRACE; break;
        case '}': lexer->current.type = TOK_RBRACE; break;
        case '[': lexer->current.type = TOK_LBRACKET; break;
        case ']': lexer->current.type = TOK_RBRACKET; break;
        case ',': lexer->current.type = TOK_COMMA; break;
        case ';': lexer->current.type = TOK_SEMICOLON; break;
        case '=': lexer->current.type = TOK_EQUAL; break;
        case '+': lexer->current.type = TOK_PLUS; break;
        case '-': lexer->current.type = TOK_MINUS; break;
        case '*': lexer->current.type = TOK_STAR; break;
        case '/': lexer->current.type = TOK_SLASH; break;
        case '^': lexer->current.type = TOK_CARET; break;
        case '<': lexer->current.type = TOK_LT; break;
        case '>': lexer->current.type = TOK_GT; break;
        case '&': lexer->current.type = TOK_AND; break;
        case '|': lexer->current.type = TOK_OR; break;
        case '!': lexer->current.type = TOK_NOT; break;
        default:
            snprintf(lexer->err_msg, sizeof(lexer->err_msg),
                    "Unknown character '%c' at line %d", c, lexer->line);
            return -1;
    }
    
    /* Check for two-character operators */
    if (lexer->pos < lexer->source_len) {
        int nc = lexer->source[lexer->pos];
        
        if (c == '=' && nc == '=') {
            lexer->current.type = TOK_EQ;
            lexer->pos++;
        } else if (c == '<' && nc == '=') {
            lexer->current.type = TOK_LE;
            lexer->pos++;
        } else if (c == '>' && nc == '=') {
            lexer->current.type = TOK_GE;
            lexer->pos++;
        } else if (c == '<' && nc == '>') {
            lexer->current.type = TOK_NE;
            lexer->pos++;
        } else if (c == '&' && nc == '&') {
            lexer->current.type = TOK_AND;
            lexer->pos++;
        } else if (c == '|' && nc == '|') {
            lexer->current.type = TOK_OR;
            lexer->pos++;
        } else if (c == '!' && nc == '=') {
            lexer->current.type = TOK_NE;
            lexer->pos++;
        } else if ((c == '&' || c == '|') && nc != c) {
            snprintf(lexer->err_msg, sizeof(lexer->err_msg),
                    "Single '%c' at line %d; GrADS uses '%c%c' for logical %s",
                    c, lexer->line, c, c, c == '&' ? "AND" : "OR");
            return -1;
        }
    }
    
    return 0;
}

/* Expect token type */
int grads_ng_lexer_expect(grads_ng_lexer_t* lexer, grads_ng_token_type_t type) {
    if (lexer->current.type != type) {
        snprintf(lexer->err_msg, sizeof(lexer->err_msg),
                "Expected token %d but got %d at line %d",
                type, lexer->current.type, lexer->current.line);
        return -1;
    }
    return 0;
}

/* Advance to next token */
void grads_ng_lexer_advance(grads_ng_lexer_t* lexer) {
    if (!lexer || lexer->err) return;
    if (grads_ng_lexer_next(lexer) != 0) lexer->err = 1;
}

/* Create parser */
grads_ng_parser_t* grads_ng_parser_create(const char* source) {
    grads_ng_parser_t* parser;
    
    if (!source) return NULL;
    
    parser = calloc(1, sizeof(grads_ng_parser_t));
    if (!parser) return NULL;
    
    parser->lexer = grads_ng_lexer_create(source);
    if (!parser->lexer) {
        free(parser);
        return NULL;
    }
    
    return parser;
}

/* Destroy parser */
void grads_ng_parser_destroy(grads_ng_parser_t* parser) {
    if (!parser) return;
    grads_ng_lexer_destroy(parser->lexer);
    free(parser);
}

/* Check for errors */
int grads_ng_parser_has_error(grads_ng_parser_t* parser) {
    if (!parser) return 1;
    return parser->has_error;
}

/* Get error message */
const char* grads_ng_parser_error(grads_ng_parser_t* parser) {
    if (!parser) return "NULL parser";
    if (parser->has_error) return parser->err_msg;
    if (parser->lexer->err_msg[0]) return parser->lexer->err_msg;
    return NULL;
}

/* Create AST node */
grads_ng_ast_node_t* grads_ng_ast_create(grads_ng_ast_type_t type) {
    grads_ng_ast_node_t* node;
    
    node = calloc(1, sizeof(grads_ng_ast_node_t));
    if (!node) return NULL;
    
    node->type = type;
    return node;
}

/* Destroy AST node */
void grads_ng_ast_destroy(grads_ng_ast_node_t* node) {
    if (!node) return;
    
    if (node->left) grads_ng_ast_destroy(node->left);
    if (node->right) grads_ng_ast_destroy(node->right);
    
    if (node->next) grads_ng_ast_destroy(node->next);
    
    if (node->type == AST_CALL_EXPR) {
        int i;
        for (i = 0; i < node->value.call.argc; i++)
            grads_ng_ast_destroy(node->value.call.argv[i]);
        free(node->value.call.argv);
        free(node->value.call.name);
    } else if (node->type == AST_STRING_EXPR ||
               node->type == AST_IDENT_EXPR ||
               node->type == AST_BINARY_EXPR ||
               node->type == AST_UNARY_EXPR) {
        free(node->value.string);
    }

    free(node);
}

/* Simple parser: parse expression */
static grads_ng_ast_node_t* parse_expr(grads_ng_parser_t* parser);
static grads_ng_ast_node_t* parse_unary(grads_ng_parser_t* parser);
static grads_ng_ast_node_t* parse_power(grads_ng_parser_t* parser);

/* True while parsing may continue: any recorded lexer or parser failure
 * must stop every loop, since the current token stops advancing then. */
#define PARSE_OK(p) (!(p)->has_error && !(p)->lexer->err)

/* Parse primary expression */
static grads_ng_ast_node_t* parse_primary(grads_ng_parser_t* parser) {
    grads_ng_lexer_t* lex = parser->lexer;
    grads_ng_ast_node_t* node;
    
    switch (lex->current.type) {
        case TOK_NUMBER:
            node = grads_ng_ast_create(AST_NUMBER_EXPR);
            node->value.number = lex->current.number;
            grads_ng_lexer_advance(lex);
            return node;
            
        case TOK_STRING:
            node = grads_ng_ast_create(AST_STRING_EXPR);
            node->value.string = strdup(lex->current.text);
            grads_ng_lexer_advance(lex);
            return node;
            
        case TOK_IDENT: {
            char* name = strdup(lex->current.text);
            if (!name) {
                parser->has_error = 1;
                snprintf(parser->err_msg, sizeof(parser->err_msg),
                        "out of memory");
                return NULL;
            }
            grads_ng_lexer_advance(lex);
            if (PARSE_OK(parser) && lex->current.type == TOK_LPAREN) {
                /* Function call: name(expr, ...). */
                grads_ng_ast_node_t** argv = NULL;
                int argc = 0, cap = 0;
                grads_ng_lexer_advance(lex);
                node = grads_ng_ast_create(AST_CALL_EXPR);
                if (!node) {
                    free(name);
                    parser->has_error = 1;
                    return NULL;
                }
                node->value.call.name = name;
                node->value.call.argc = 0;
                node->value.call.argv = NULL;
                if (lex->current.type != TOK_RPAREN) {
                    for (;;) {
                        grads_ng_ast_node_t* arg = parse_expr(parser);
                        /* Dimension argument (max/ave style): t=1, z=2.
                         * Represented as an EQUAL binary node so the
                         * evaluator can tell ranges from values. */
                        if (PARSE_OK(parser) && arg &&
                            arg->type == AST_IDENT_EXPR &&
                            lex->current.type == TOK_EQUAL) {
                            grads_ng_ast_node_t* val;
                            grads_ng_ast_node_t* dim;
                            grads_ng_lexer_advance(lex);
                            val = parse_expr(parser);
                            if (!PARSE_OK(parser) || !val) {
                                grads_ng_ast_destroy(arg);
                                grads_ng_ast_destroy(val);
                                grads_ng_ast_destroy(node);
                                if (PARSE_OK(parser)) {
                                    parser->has_error = 1;
                                    snprintf(parser->err_msg,
                                            sizeof(parser->err_msg),
                                            "bad dimension value");
                                }
                                return NULL;
                            }
                            dim = grads_ng_ast_create(AST_BINARY_EXPR);
                            if (!dim) {
                                grads_ng_ast_destroy(arg);
                                grads_ng_ast_destroy(val);
                                grads_ng_ast_destroy(node);
                                parser->has_error = 1;
                                return NULL;
                            }
                            dim->op = TOK_EQUAL;
                            dim->left = arg;
                            dim->right = val;
                            arg = dim;
                        }
                        if (!PARSE_OK(parser) || !arg) {
                            grads_ng_ast_destroy(arg);
                            grads_ng_ast_destroy(node);
                            if (PARSE_OK(parser)) {
                                parser->has_error = 1;
                                snprintf(parser->err_msg,
                                        sizeof(parser->err_msg),
                                        "bad argument to %s", name);
                            }
                            return NULL;
                        }
                        if (argc >= 16) {
                            grads_ng_ast_destroy(arg);
                            grads_ng_ast_destroy(node);
                            parser->has_error = 1;
                            snprintf(parser->err_msg,
                                    sizeof(parser->err_msg),
                                    "%s takes at most 16 arguments", name);
                            return NULL;
                        }
                        if (argc >= cap) {
                            int ncap = cap ? cap * 2 : 4;
                            grads_ng_ast_node_t** nav =
                                realloc(argv, (size_t)ncap * sizeof(*nav));
                            if (!nav) {
                                grads_ng_ast_destroy(arg);
                                grads_ng_ast_destroy(node);
                                parser->has_error = 1;
                                return NULL;
                            }
                            argv = nav;
                            cap = ncap;
                        }
                        argv[argc++] = arg;
                        /* Incremental ownership: node frees these on any
                         * later error via grads_ng_ast_destroy. */
                        node->value.call.argv = argv;
                        node->value.call.argc = argc;
                        if (lex->current.type == TOK_COMMA) {
                            grads_ng_lexer_advance(lex);
                            continue;
                        }
                        break;
                    }
                }
                if (lex->current.type != TOK_RPAREN) {
                    grads_ng_ast_destroy(node);
                    parser->has_error = 1;
                    snprintf(parser->err_msg, sizeof(parser->err_msg),
                            "Expected ) to close %s(", name);
                    return NULL;
                }
                grads_ng_lexer_advance(lex);
                node->value.call.argc = argc;
                /* Shrink note: argv keeps its buffer; destroy frees it. */
                node->value.call.argv = argv;
                return node;
            }
            node = grads_ng_ast_create(AST_IDENT_EXPR);
            if (!node) {
                free(name);
                parser->has_error = 1;
                return NULL;
            }
            node->value.string = name;
            return node;
        }
            
        case TOK_LPAREN:
            grads_ng_lexer_advance(lex);
            node = parse_expr(parser);
            if (parser->has_error) return NULL;
            if (lex->current.type != TOK_RPAREN) {
                parser->has_error = 1;
                snprintf(parser->err_msg, sizeof(parser->err_msg),
                        "Expected )");
                return NULL;
            }
            grads_ng_lexer_advance(lex);
            return node;
            
        default:
            parser->has_error = 1;
            snprintf(parser->err_msg, sizeof(parser->err_msg),
                    "Unexpected token %d", lex->current.type);
            return NULL;
    }
}

/* Parse unary expression */
static grads_ng_ast_node_t* parse_unary(grads_ng_parser_t* parser) {
    grads_ng_lexer_t* lex = parser->lexer;
    grads_ng_ast_node_t* node;
    
    if (lex->current.type == TOK_NOT || lex->current.type == TOK_MINUS) {
        node = grads_ng_ast_create(AST_UNARY_EXPR);
        if (!node) return NULL;
        node->op = lex->current.type;
        grads_ng_lexer_advance(lex);
        node->left = parse_unary(parser);
        return node;
    }

    return parse_power(parser);
}

/* Parse power (right-associative, binds tighter than unary on its left:
 * -2^2 parses as -(2^2); 2^-3 and 2^3^2 work as in GrADS/Fortran). */
static grads_ng_ast_node_t* parse_power(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* base = parse_primary(parser);

    if (parser->lexer->current.type == TOK_CARET) {
        grads_ng_ast_node_t* node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!node) return NULL;
        node->op = TOK_CARET;
        node->left = base;
        grads_ng_lexer_advance(parser->lexer);
        node->right = parse_unary(parser);  /* right side takes unary + power */
        return node;
    }

    return base;
}

/* Parse multiplication/division */
static grads_ng_ast_node_t* parse_factor(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_unary(parser);

    while (PARSE_OK(parser) &&
           (parser->lexer->current.type == TOK_STAR ||
            parser->lexer->current.type == TOK_SLASH)) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!new_node) return NULL;
        new_node->op = parser->lexer->current.type;
        new_node->left = node;
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_unary(parser);
        node = new_node;
    }

    return node;
}

/* Parse addition/subtraction */
static grads_ng_ast_node_t* parse_term(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_factor(parser);
    
    while (PARSE_OK(parser) &&
           (parser->lexer->current.type == TOK_PLUS ||
            parser->lexer->current.type == TOK_MINUS)) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!new_node) return NULL;
        new_node->op = parser->lexer->current.type;
        new_node->left = node;
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_factor(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse comparison */
static grads_ng_ast_node_t* parse_comparison(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_term(parser);
    
    while (PARSE_OK(parser) &&
           (parser->lexer->current.type == TOK_LT ||
            parser->lexer->current.type == TOK_GT ||
            parser->lexer->current.type == TOK_LE ||
            parser->lexer->current.type == TOK_GE ||
            parser->lexer->current.type == TOK_EQ ||
            parser->lexer->current.type == TOK_NE)) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!new_node) return NULL;
        new_node->op = parser->lexer->current.type;
        new_node->left = node;
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_term(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse logical AND */
static grads_ng_ast_node_t* parse_logical_and(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_comparison(parser);
    
    while (PARSE_OK(parser) && parser->lexer->current.type == TOK_AND) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!new_node) return NULL;
        new_node->op = TOK_AND;
        new_node->left = node;
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_comparison(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse logical OR */
static grads_ng_ast_node_t* parse_logical_or(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_logical_and(parser);
    
    while (PARSE_OK(parser) && parser->lexer->current.type == TOK_OR) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        if (!new_node) return NULL;
        new_node->op = TOK_OR;
        new_node->left = node;
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_logical_and(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse expression */
static grads_ng_ast_node_t* parse_expr(grads_ng_parser_t* parser) {
    return parse_logical_or(parser);
}

/* Parse statement */
static grads_ng_ast_node_t* parse_statement(grads_ng_parser_t* parser) {
    grads_ng_lexer_t* lex = parser->lexer;
    grads_ng_ast_node_t* node = NULL;
    grads_ng_ast_node_t* last = NULL;
    
    while (PARSE_OK(parser) &&
           lex->current.type != TOK_EOF &&
           lex->current.type != TOK_RBRACE) {
        grads_ng_ast_node_t* stmt = NULL;
        
        switch (lex->current.type) {
            case TOK_SAY:
                stmt = grads_ng_ast_create(AST_SAY_STMT);
                grads_ng_lexer_advance(lex);
                stmt->left = parse_expr(parser);
                break;
                
            case TOK_IF:
                stmt = grads_ng_ast_create(AST_IF_STMT);
                grads_ng_lexer_advance(lex);
                stmt->left = parse_expr(parser);  /* condition */
                if (lex->current.type == TOK_LBRACE || lex->current.type == TOK_SEMICOLON) {
                    grads_ng_lexer_advance(lex);
                }
                stmt->right = parse_statement(parser);  /* then-branch */
                break;
                
            case TOK_WHILE:
                stmt = grads_ng_ast_create(AST_WHILE_STMT);
                grads_ng_lexer_advance(lex);
                stmt->left = parse_expr(parser);  /* condition */
                stmt->right = parse_statement(parser);  /* body */
                break;
                
            case TOK_IDENT:
                /* Assignment or expression */
                stmt = parse_expr(parser);
                if (lex->current.type == TOK_EQUAL) {
                    grads_ng_ast_node_t* assign = grads_ng_ast_create(AST_IDENT_EXPR);
                    assign->left = stmt;
                    grads_ng_lexer_advance(lex);
                    assign->right = parse_expr(parser);
                    stmt = assign;
                }
                break;
                
            default:
                stmt = parse_expr(parser);
                break;
        }
        
        /* Skip semicolon */
        if (lex->current.type == TOK_SEMICOLON) {
            grads_ng_lexer_advance(lex);
        }
        
        /* Add to linked list */
        if (stmt) {
            if (last) {
                last->next = stmt;
            } else {
                node = stmt;
            }
            last = stmt;
        }
    }
    
    return node;
}

/* Parse program */
grads_ng_ast_node_t* grads_ng_parser_parse(grads_ng_parser_t* parser) {
    if (!parser || !parser->lexer) return NULL;

    grads_ng_ast_node_t* program = grads_ng_ast_create(AST_PROGRAM);
    if (!program) return NULL;

    program->left = parse_statement(parser);

    if (parser->has_error) {
        grads_ng_ast_destroy(program);
        return NULL;
    }

    /* A lexer failure mid-stream (e.g. a bad character after a valid
     * prefix) must fail the parse, not silently truncate the input. */
    if (parser->lexer->err) {
        parser->has_error = 1;
        snprintf(parser->err_msg, sizeof(parser->err_msg), "%s",
                 parser->lexer->err_msg);
        grads_ng_ast_destroy(program);
        return NULL;
    }

    /* Trailing garbage after a complete statement is an error. */
    if (parser->lexer->current.type != TOK_EOF) {
        parser->has_error = 1;
        snprintf(parser->err_msg, sizeof(parser->err_msg),
                "Unexpected trailing input at line %d", parser->lexer->line);
        grads_ng_ast_destroy(program);
        return NULL;
    }

    return program;
}

/* VM: Create */
grads_ng_vm_t* grads_ng_vm_create(void) {
    return calloc(1, sizeof(grads_ng_vm_t));
}

/* VM: Destroy */
void grads_ng_vm_destroy(grads_ng_vm_t* vm) {
    if (!vm) return;
    free(vm);
}

/* Case-insensitive name match for math functions. */
static int math_name_eq(const char* a, const char* b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int grads_ng_math_known(const char *name) {
    return name && (math_name_eq(name, "abs") || math_name_eq(name, "sqrt") ||
                    math_name_eq(name, "exp") || math_name_eq(name, "log") ||
                    math_name_eq(name, "sin") || math_name_eq(name, "cos") ||
                    math_name_eq(name, "pow"));
}

int grads_ng_math_arity(const char *name) {
    if (!name) return -1;
    if (math_name_eq(name, "pow")) return 2;
    return grads_ng_math_known(name) ? 1 : -1;
}

int grads_ng_math_apply(const char* name, const double* argv, int argc,
                        double* out, char* err, size_t errlen) {
    double x;

#define MATH_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)

    if (!name || !argv || !out) MATH_FAIL("cannot apply an empty function");
    if (math_name_eq(name, "pow")) {
        double y;
        if (argc != 2)
            MATH_FAIL("%s takes 2 arguments (%d given)",
                      name ? name : "?", argc);
        x = argv[0];
        y = argv[1];
        /* pow follows arithmetic: any NaN input yields NaN. */
        if (isnan(x) || isnan(y)) {
            *out = NAN;
            return 0;
        }
        *out = pow(x, y);
        if (!isfinite(*out)) MATH_FAIL("power overflow for %g^%g", x, y);
        return 0;
    }
    if (argc != 1)
        MATH_FAIL("%s takes 1 argument (%d given)",
                  name ? name : "?", argc);
    x = argv[0];
    if (isnan(x)) {
        *out = x;
        return 0;
    }
    if (math_name_eq(name, "abs")) *out = fabs(x);
    else if (math_name_eq(name, "sqrt")) {
        if (x < 0.0) MATH_FAIL("sqrt of negative value %g", x);
        *out = sqrt(x);
    } else if (math_name_eq(name, "exp")) {
        *out = exp(x);
        if (!isfinite(*out)) MATH_FAIL("exp overflow for %g", x);
    } else if (math_name_eq(name, "log")) {
        if (x <= 0.0) MATH_FAIL("log of non-positive value %g", x);
        *out = log(x);
    } else if (math_name_eq(name, "sin")) *out = sin(x);
    else if (math_name_eq(name, "cos")) *out = cos(x);
    else MATH_FAIL("unknown function \"%s\"", name);
    return 0;
#undef MATH_FAIL
}

/* VM: Execute simple AST */
int grads_ng_vm_exec(grads_ng_vm_t* vm, grads_ng_ast_node_t* program) {
    (void)vm;
    (void)program;
    return 0;
}

/* Scalar expression evaluator.
 * GrADS truthiness: 0 is false, anything else is true; results are 1/0.
 * Division by zero and domain errors are hard errors here: without a data
 * model there is no UNDEF value to propagate (M3 adds missing-value rules). */
int grads_ng_ast_eval(const grads_ng_ast_node_t* node, double* out,
                      char* err, size_t errlen) {
    double l, r;

#define NG_EVAL_FAIL(fmt, ...) do { \
        if (err && errlen > 0) snprintf(err, errlen, fmt, ##__VA_ARGS__); \
        return -1; \
    } while (0)

    if (!node || !out) NG_EVAL_FAIL("cannot evaluate an empty expression");

    /* Unwrap a parsed program: -e takes exactly one expression statement. */
    if (node->type == AST_PROGRAM) {
        const grads_ng_ast_node_t* stmt = node->left;
        if (!stmt) NG_EVAL_FAIL("empty expression");
        if (stmt->next) NG_EVAL_FAIL("expected a single expression");
        return grads_ng_ast_eval(stmt, out, err, errlen);
    }

    switch (node->type) {
        case AST_NUMBER_EXPR:
            *out = node->value.number;
            return 0;

        case AST_IDENT_EXPR:
            NG_EVAL_FAIL("variable \"%s\" is not defined (no dataset is open)",
                         node->value.string ? node->value.string : "?");

        case AST_STRING_EXPR:
            NG_EVAL_FAIL("a string cannot be used as a number here");

        case AST_UNARY_EXPR:
            if (grads_ng_ast_eval(node->left, &l, err, errlen) != 0) return -1;
            if (node->op == TOK_MINUS) { *out = -l; return 0; }
            if (node->op == TOK_NOT) { *out = (l == 0.0) ? 1.0 : 0.0; return 0; }
            NG_EVAL_FAIL("unknown unary operator");

        case AST_BINARY_EXPR:
            if (grads_ng_ast_eval(node->left, &l, err, errlen) != 0) return -1;
            if (grads_ng_ast_eval(node->right, &r, err, errlen) != 0) return -1;
            switch (node->op) {
                case TOK_PLUS:  *out = l + r; return 0;
                case TOK_MINUS: *out = l - r; return 0;
                case TOK_STAR:  *out = l * r; return 0;
                case TOK_SLASH:
                    if (r == 0.0) NG_EVAL_FAIL("division by zero");
                    *out = l / r; return 0;
                case TOK_CARET:
                    *out = pow(l, r);
                    if (!isfinite(*out)) NG_EVAL_FAIL("power overflow for %g^%g", l, r);
                    return 0;
                case TOK_LT: *out = (l < r) ? 1.0 : 0.0; return 0;
                case TOK_LE: *out = (l <= r) ? 1.0 : 0.0; return 0;
                case TOK_GT: *out = (l > r) ? 1.0 : 0.0; return 0;
                case TOK_GE: *out = (l >= r) ? 1.0 : 0.0; return 0;
                case TOK_EQ: *out = (l == r) ? 1.0 : 0.0; return 0;
                case TOK_NE: *out = (l != r) ? 1.0 : 0.0; return 0;
                case TOK_AND: *out = ((l != 0.0) && (r != 0.0)) ? 1.0 : 0.0; return 0;
                case TOK_OR:  *out = ((l != 0.0) || (r != 0.0)) ? 1.0 : 0.0; return 0;
                default: break;
            }
            NG_EVAL_FAIL("unknown binary operator");

        case AST_CALL_EXPR: {
            double vals[16];
            int i;
            const char *cname = node->value.call.name;
            /* Reductions need grid context; -e is scalar-only. */
            if (cname && (math_name_eq(cname, "max") ||
                          math_name_eq(cname, "min") ||
                          math_name_eq(cname, "ave") ||
                          math_name_eq(cname, "sum")))
                NG_EVAL_FAIL("\"%s\" reduces over a dimension range "
                             "(e.g. %s(x,t=1,t=2)); use d", cname, cname);
            if (node->value.call.argc > 16)
                NG_EVAL_FAIL("too many arguments to %s",
                             node->value.call.name);
            for (i = 0; i < node->value.call.argc; i++) {
                if (grads_ng_ast_eval(node->value.call.argv[i], &vals[i],
                                      err, errlen) != 0) return -1;
            }
            if (grads_ng_math_apply(node->value.call.name, vals,
                                    node->value.call.argc, out,
                                    err, errlen) != 0) return -1;
            return 0;
        }

        default:
            break;
    }

    NG_EVAL_FAIL("this statement cannot be evaluated as a number "
                 "(only arithmetic expressions are supported)");
#undef NG_EVAL_FAIL
}