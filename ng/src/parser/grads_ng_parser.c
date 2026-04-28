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
    grads_ng_lexer_next(lexer);
    
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
    int has_dot = 0;
    int has_exp = 0;
    
    /* Integer part */
    while (lexer->pos < lexer->source_len && isdigit(lexer->source[lexer->pos])) {
        lexer->pos++;
    }
    
    /* Fractional part */
    if (lexer->pos < lexer->source_len && lexer->source[lexer->pos] == '.') {
        has_dot = 1;
        lexer->pos++;
        while (lexer->pos < lexer->source_len && isdigit(lexer->source[lexer->pos])) {
            lexer->pos++;
        }
    }
    
    /* Exponent part */
    if (lexer->pos < lexer->source_len && 
        (lexer->source[lexer->pos] == 'e' || lexer->source[lexer->pos] == 'E')) {
        has_exp = 1;
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
    
    /* Number */
    if (isdigit(c)) {
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
    grads_ng_lexer_next(lexer);
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
    
    if (node->type == AST_CALL_EXPR && node->value.call.name) {
        free(node->value.call.name);
    }
    
    free(node);
}

/* Simple parser: parse expression */
static grads_ng_ast_node_t* parse_expr(grads_ng_parser_t* parser);

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
            
        case TOK_IDENT:
            node = grads_ng_ast_create(AST_IDENT_EXPR);
            node->value.string = strdup(lex->current.text);
            grads_ng_lexer_advance(lex);
            return node;
            
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
        node->value.string = malloc(2);
        node->value.string[0] = (char)lex->current.type;
        node->value.string[1] = '\0';
        grads_ng_lexer_advance(lex);
        node->left = parse_unary(parser);
        return node;
    }
    
    return parse_primary(parser);
}

/* Parse multiplication/division */
static grads_ng_ast_node_t* parse_factor(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_unary(parser);
    
    while (parser->lexer->current.type == TOK_STAR || 
           parser->lexer->current.type == TOK_SLASH ||
           parser->lexer->current.type == TOK_CARET) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
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
    
    while (parser->lexer->current.type == TOK_PLUS || 
           parser->lexer->current.type == TOK_MINUS) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        new_node->left = node;
        new_node->value.string = malloc(2);
        new_node->value.string[0] = (char)parser->lexer->current.type;
        new_node->value.string[1] = '\0';
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_factor(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse comparison */
static grads_ng_ast_node_t* parse_comparison(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_term(parser);
    
    while (parser->lexer->current.type == TOK_LT || 
           parser->lexer->current.type == TOK_GT ||
           parser->lexer->current.type == TOK_LE ||
           parser->lexer->current.type == TOK_GE ||
           parser->lexer->current.type == TOK_EQ ||
           parser->lexer->current.type == TOK_NE) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
        new_node->left = node;
        new_node->value.string = malloc(3);
        snprintf(new_node->value.string, 3, "%c%c", 
                (char)parser->lexer->current.type, '\0');
        grads_ng_lexer_advance(parser->lexer);
        new_node->right = parse_term(parser);
        node = new_node;
    }
    
    return node;
}

/* Parse logical AND */
static grads_ng_ast_node_t* parse_logical_and(grads_ng_parser_t* parser) {
    grads_ng_ast_node_t* node = parse_comparison(parser);
    
    while (parser->lexer->current.type == TOK_AND) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
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
    
    while (parser->lexer->current.type == TOK_OR) {
        grads_ng_ast_node_t* new_node = grads_ng_ast_create(AST_BINARY_EXPR);
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
    
    while (lex->current.type != TOK_EOF && 
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

/* VM: Execute simple AST */
int grads_ng_vm_exec(grads_ng_vm_t* vm, grads_ng_ast_node_t* program) {
    (void)vm;
    (void)program;
    return 0;
}