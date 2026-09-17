/*
 * GrADS-NG CLI Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <getopt.h>

#include "grads_ng.h"
#include "parser/grads_ng_parser.h"
#include "array.h"

/* exec_command results */
#define NG_CMD_OK   0
#define NG_CMD_ERR  1
#define NG_CMD_QUIT 2

#define NG_MAX_OPEN 16

typedef struct {
    grads_ng_session_t* session;
    grads_ng_file_t* files[NG_MAX_OPEN];
    char paths[NG_MAX_OPEN][512];
    int nfiles;
} ng_cli_state_t;

/* ASCII case-insensitive word comparison (portable across MSVC/POSIX). */
static int cmd_word_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void print_cmd_help(void) {
    printf("Supported commands:\n");
    printf("  open <file.ctl>   Open a GrADS descriptor file\n");
    printf("  close N           Close file N (must be the last open file)\n");
    printf("  q file [n]        Describe open file(s)\n");
    printf("  q dims            Show dimension selection\n");
    printf("  set t N           Select time step (1-based)\n");
    printf("  set z N           Select level index (1-based)\n");
    printf("  d <expr>          Summarize an expression slice\n");
    printf("  help              Show this list\n");
    printf("  quit | exit       Leave GrADS-NG\n");
}

static void print_file_summary(int num, const char* path, grads_ng_file_t* f) {
    int nx, ny, nz, nt, nvars, type;
    const char* tname;

    grads_ng_file_dims(f, &nx, &ny, &nz, &nt);
    nvars = grads_ng_file_nvars(f);
    type = grads_ng_file_type(f);
    tname = (type == 1) ? "station" : (type == 2 ? "bufr" : "grid");

    printf("File %d: %s\n", num, path);
    printf("  Type: %s  Dimensions: %d x %d x %d x %d  Variables: %d\n",
           tname, nx, ny, nz, nt, nvars);
    if (nvars > 0) {
        int i;
        printf("  Names:");
        for (i = 0; i < nvars; i++) {
            const char* name = grads_ng_file_varname(f, i);
            printf(" %s", name ? name : "?");
        }
        printf("\n");
    }
}

/* GrADS commands accepted but scheduled for later milestones. */
static const char* ng_future_cmds[] = {
    "sdfopen", "xdfopen", "define",
    "clear", "draw", "print", "gxprint", "enable", "disable",
    "reinit", "reset", NULL
};

/* Report an evaluated array as value statistics (NaN = missing).
 * Plots arrive in M5; until then this is the honest, checkable face of `d`. */
static int display_array(const char* label, const ng_array_t* a,
                         int t1, int z1) {
    double min, max, sum;
    long count = 0, nundef = 0;
    long n = (long)a->nx * a->ny;
    long k;

    min = max = sum = 0.0;
    for (k = 0; k < n; k++) {
        double v = a->data[k];
        if (isnan(v)) {
            nundef++;
            continue;
        }
        if (count == 0 || v < min) min = v;
        if (count == 0 || v > max) max = v;
        sum += v;
        count++;
    }

    printf("Displaying %s: %d x %d (t=%d, z=%d)\n", label, a->nx, a->ny,
           t1, z1);
    if (count == 0) {
        printf("  all %ld values missing\n", nundef);
    } else {
        printf("  Min: %.6g  Max: %.6g  Mean: %.6g over %ld values",
               min, max, sum / (double)count, count);
        if (nundef > 0) printf(" (%ld missing)", nundef);
        printf("\n");
    }
    printf("(M3 reports values; plots arrive in M5.)\n");
    return NG_CMD_OK;
}

/* The `d` unknown-variable error, with the available names listed. */
static int unknown_var_error(grads_ng_file_t* f, const char* path,
                             const char* name) {
    int n = grads_ng_file_nvars(f);
    int k;
    fprintf(stderr, "ERROR: variable \"%s\" is not defined in \"%s\".\n",
            name, path);
    if (n > 0) {
        fprintf(stderr, "\nAvailable variables:");
        for (k = 0; k < n; k++)
            fprintf(stderr, " %s", grads_ng_file_varname(f, k));
        fprintf(stderr, "\n");
    }
    return NG_CMD_ERR;
}

/* Execute one GrADS command line. The buffer is modified in place. */
static int exec_command(ng_cli_state_t* st, char* line) {
    char* word;
    char* args;
    char* p = line;
    int i;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '*') return NG_CMD_OK;  /* blank line or comment */

    word = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';
    while (*p && isspace((unsigned char)*p)) p++;
    args = p;
    for (i = (int)strlen(args) - 1; i >= 0 &&
         isspace((unsigned char)args[i]); i--) args[i] = '\0';

    if (cmd_word_eq(word, "quit") || cmd_word_eq(word, "exit")) return NG_CMD_QUIT;

    if (cmd_word_eq(word, "help") || strcmp(word, "?") == 0) {
        print_cmd_help();
        return NG_CMD_OK;
    }

    if (cmd_word_eq(word, "open")) {
        grads_ng_file_t* f;

        if (*args == '\0') {
            fprintf(stderr, "ERROR: 'open' needs a descriptor file.\n\nUsage:\n    open <file.ctl>\n");
            return NG_CMD_ERR;
        }
        if (st->nfiles >= NG_MAX_OPEN) {
            fprintf(stderr, "ERROR: too many open files (max %d).\n", NG_MAX_OPEN);
            return NG_CMD_ERR;
        }
        f = grads_ng_open(st->session, args);
        if (!f) {
            const char* why = grads_ng_session_error(st->session);
            fprintf(stderr, "ERROR: could not open \"%s\": %s.\n",
                    args, why ? why : "unknown error");
            return NG_CMD_ERR;
        }
        strncpy(st->paths[st->nfiles], args, sizeof(st->paths[0]) - 1);
        st->paths[st->nfiles][sizeof(st->paths[0]) - 1] = '\0';
        st->files[st->nfiles] = f;
        st->nfiles++;
        print_file_summary(st->nfiles, args, f);
        return NG_CMD_OK;
    }

    /* `close N` mirrors GrADS: a file number is required, only the last
     * (highest-numbered) open file may be closed, trailing words are
     * ignored. Verified against OpenGrADS 2.2.1.oga.1 (see NG_BASELINE.md):
     *   close      -> "Close Error: Missing file number"
     *   close foo  -> "Close Error: Invalid file number"
     *   close 0/99/non-last -> "Close Error: Only last file may be closed"
     *   close N (last) -> "File N has been closed" */
    if (cmd_word_eq(word, "close")) {
        char* num = args;
        char* tok;
        char* end;
        long n;

        while (*num && isspace((unsigned char)*num)) num++;
        if (*num == '\0') {
            fprintf(stderr, "ERROR: Close Error: Missing file number.\n\nUsage:\n    close N\n");
            return NG_CMD_ERR;
        }
        tok = num;
        while (*tok && !isspace((unsigned char)*tok)) tok++;
        if (*tok) *tok = '\0';  /* trailing words ignored, as in GrADS */
        n = strtol(num, &end, 10);
        if (*num == '\0' || *end != '\0') {
            fprintf(stderr, "ERROR: Close Error: Invalid file number.\n\nUsage:\n    close N\n");
            return NG_CMD_ERR;
        }
        if (st->nfiles == 0 || n != st->nfiles) {
            fprintf(stderr, "ERROR: Close Error: Only last file may be closed.\n");
            return NG_CMD_ERR;
        }
        grads_ng_close(st->files[st->nfiles - 1]);
        st->files[st->nfiles - 1] = NULL;
        st->paths[st->nfiles - 1][0] = '\0';
        st->nfiles--;
        printf("File %ld has been closed\n", n);
        return NG_CMD_OK;
    }

    if (cmd_word_eq(word, "q") || cmd_word_eq(word, "query")) {
        char* qword = args;
        char* qrest;
        long which = 0;

        while (*qword && isspace((unsigned char)*qword)) qword++;
        qrest = qword;
        while (*qrest && !isspace((unsigned char)*qrest)) qrest++;
        if (*qrest) *qrest++ = '\0';
        while (*qrest && isspace((unsigned char)*qrest)) qrest++;
        if (*qrest) which = strtol(qrest, NULL, 10);

        if (cmd_word_eq(qword, "dims")) {
            grads_ng_file_t* f;
            int nx, ny, nz, nt, t, z;
            if (st->nfiles == 0) {
                fprintf(stderr, "ERROR: no files are open.\n\nUse:\n    open <file.ctl>\n");
                return NG_CMD_ERR;
            }
            f = st->files[st->nfiles - 1];
            if (grads_ng_file_dims(f, &nx, &ny, &nz, &nt) != 0 ||
                grads_ng_file_selected(f, &t, &z) != 0) {
                fprintf(stderr, "ERROR: cannot describe the open file.\n");
                return NG_CMD_ERR;
            }
            printf("Default file %d: %s\n", st->nfiles,
                   st->paths[st->nfiles - 1]);
            printf("  X: 1..%d  Y: 1..%d  Z: %d of %d  T: %d of %d\n",
                   nx, ny, z + 1, nz, t + 1, nt);
            return NG_CMD_OK;
        }
        if (!cmd_word_eq(qword, "file")) {
            fprintf(stderr, "ERROR: only 'q file' and 'q dims' are supported in M3.\n\nUsage:\n    q file [n]\n    q dims\n");
            return NG_CMD_ERR;
        }
        if (st->nfiles == 0) {
            fprintf(stderr, "ERROR: no files are open.\n\nUse:\n    open <file.ctl>\n");
            return NG_CMD_ERR;
        }
        if (which > 0) {
            if (which > st->nfiles) {
                fprintf(stderr, "ERROR: only %d file(s) open; there is no file %ld.\n",
                        st->nfiles, which);
                return NG_CMD_ERR;
            }
            print_file_summary((int)which, st->paths[which - 1], st->files[which - 1]);
            return NG_CMD_OK;
        }
        for (i = 0; i < st->nfiles; i++) {
            print_file_summary(i + 1, st->paths[i], st->files[i]);
        }
        return NG_CMD_OK;
    }

    if (cmd_word_eq(word, "d") || cmd_word_eq(word, "display")) {
        grads_ng_file_t* f;
        grads_ng_parser_t* parser;
        grads_ng_ast_node_t* ast;
        const grads_ng_ast_node_t* stmt;
        ng_array_t* arr;
        const char* eerr = NULL;
        int t, z, rc;

        if (*args == '\0') {
            fprintf(stderr, "ERROR: 'd' needs an expression.\n\nUsage:\n    d <expression>\n");
            return NG_CMD_ERR;
        }
        if (st->nfiles == 0) {
            fprintf(stderr, "ERROR: no files are open.\n\nUse:\n    open <file.ctl>\n");
            return NG_CMD_ERR;
        }
        f = st->files[st->nfiles - 1];  /* default file: most recent open */

        parser = grads_ng_parser_create(args);
        if (!parser) {
            fprintf(stderr, "ERROR: out of memory reading the expression.\n");
            return NG_CMD_ERR;
        }
        ast = grads_ng_parser_parse(parser);
        if (!ast) {
            const char* perr = grads_ng_parser_error(parser);
            fprintf(stderr, "ERROR: could not parse expression: %s\n\nExpression:\n    %s\n",
                    perr ? perr : "unknown error", args);
            grads_ng_parser_destroy(parser);
            return NG_CMD_ERR;
        }
        grads_ng_parser_destroy(parser);

        /* Single unknown name keeps the listing error. */
        stmt = (ast->type == AST_PROGRAM) ? ast->left : ast;
        if (stmt && stmt->type == AST_IDENT_EXPR && !stmt->next &&
            !grads_ng_get_var(f, stmt->value.string)) {
            grads_ng_ast_destroy(ast);
            return unknown_var_error(f, st->paths[st->nfiles - 1],
                                     stmt->value.string);
        }

        arr = ng_eval_array(f, ast, &eerr);
        grads_ng_ast_destroy(ast);
        if (!arr) {
            /* An unknown name inside a bigger expression still names it. */
            fprintf(stderr, "ERROR: %s\n\nExpression:\n    %s\n",
                    eerr ? eerr : "evaluation failed", args);
            return NG_CMD_ERR;
        }
        if (grads_ng_file_selected(f, &t, &z) != 0) {
            t = 0;
            z = 0;
        }
        rc = display_array(args, arr, t + 1, z + 1);
        ng_array_free(arr);
        return rc;
    }

    if (cmd_word_eq(word, "set")) {
        grads_ng_file_t* f;
        char* sub;
        char* num;
        char* end;
        long v;
        int t, z;
        char emsg[256];

        if (st->nfiles == 0) {
            fprintf(stderr, "ERROR: no files are open.\n\nUse:\n    open <file.ctl>\n");
            return NG_CMD_ERR;
        }
        sub = args;
        while (*sub && isspace((unsigned char)*sub)) sub++;
        num = sub;
        while (*num && !isspace((unsigned char)*num)) num++;
        if (*num) *num++ = '\0';
        while (*num && isspace((unsigned char)*num)) num++;
        if (!cmd_word_eq(sub, "t") && !cmd_word_eq(sub, "z") &&
            !cmd_word_eq(sub, "lev")) {
            fprintf(stderr, "ERROR: only 'set t N' and 'set z N' are supported in M3.\n\nUsage:\n    set t 2\n    set z 1\n");
            return NG_CMD_ERR;
        }
        if (*num == '\0' || num[strspn(num, "0123456789")] != '\0' ||
            (v = strtol(num, &end, 10), *end != '\0') || v < 1) {
            fprintf(stderr, "ERROR: '%s' needs a positive index.\n\nUsage:\n    set %s N\n",
                    sub, sub);
            return NG_CMD_ERR;
        }
        f = st->files[st->nfiles - 1];
        if (grads_ng_file_selected(f, &t, &z) != 0) {
            t = 0;
            z = 0;
        }
        if (cmd_word_eq(sub, "t")) t = (int)v - 1;
        else z = (int)v - 1;
        if (grads_ng_file_select(f, t, z, emsg, sizeof(emsg)) != 0) {
            fprintf(stderr, "ERROR: %s\n", emsg);
            return NG_CMD_ERR;
        }
        printf("Dimension %s set to %ld\n", sub, v);
        return NG_CMD_OK;
    }

    for (i = 0; ng_future_cmds[i]; i++) {
        if (cmd_word_eq(word, ng_future_cmds[i])) {
            fprintf(stderr, "ERROR: '%s' is not implemented yet.\n\nSupported: open, close, q file, q dims, set, d, help, quit.\n", word);
            return NG_CMD_ERR;
        }
    }

    fprintf(stderr, "ERROR: unknown command \"%s\".\n\nSupported commands:\n    open, close, q file, q dims, set, d, help, quit\n", word);
    return NG_CMD_ERR;
}

/* Read an entire text file into a NUL-terminated buffer (caller frees). */
static char* read_text_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    char* buf;
    long len;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    len = ftell(fp);
    if (len < 0) { fclose(fp); return NULL; }
    rewind(fp);
    buf = malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

static void print_help(const char* prog) {
    printf("GrADS-NG %d.%d.%d - Next Generation GrADS\n",
           GRADS_NG_VERSION_MAJOR, GRADS_NG_VERSION_MINOR, GRADS_NG_VERSION_PATCH);
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -h, --help        Show this help\n");
    printf("  -b, --batch       Batch mode (no GUI)\n");
    printf("  -c, --cmd CMD     Execute command CMD\n");
    printf("  -e, --expr EXPR  Evaluate expression EXPR\n");
    printf("  -f, --file FILE  Open GrADS descriptor FILE\n");
    printf("  -o, --output DIR  Output directory (default: .)\n");
    printf("  -v, --version     Show version\n");
    printf("\nExamples:\n");
    printf("  %s -b -c 'open mydata.ctl'\n", prog);
    printf("  %s -e 'sin(3.14159/4)'\n", prog);
    printf("  %s script.gs\n", prog);
}

int main(int argc, char** argv) {
    grads_ng_config_t config = {0};
    grads_ng_session_t* session;
    int c;
    int batch_mode = 0;
    char* cmd = NULL;
    char* expr = NULL;
    char* ctl_file = NULL;
    char* output_dir = ".";
    
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"batch", no_argument, 0, 'b'},
        {"cmd", required_argument, 0, 'c'},
        {"expr", required_argument, 0, 'e'},
        {"file", required_argument, 0, 'f'},
        {"output", required_argument, 0, 'o'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    /* Parse options */
    while ((c = getopt_long(argc, argv, "hbc:e:f:o:v", 
                          long_options, NULL)) != -1) {
        switch (c) {
            case 'h':
                print_help(argv[0]);
                return 0;
            case 'b':
                batch_mode = 1;
                break;
            case 'c':
                cmd = optarg;
                break;
            case 'e':
                expr = optarg;
                break;
            case 'f':
                ctl_file = optarg;
                break;
            case 'o':
                output_dir = optarg;
                break;
            case 'v':
                printf("GrADS-NG %d.%d.%d\n",
                       GRADS_NG_VERSION_MAJOR, 
                       GRADS_NG_VERSION_MINOR,
                       GRADS_NG_VERSION_PATCH);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }
    
    /* -o is accepted for CLI compatibility; output routing arrives in M5. */
    (void)output_dir;

    /* Initialize GrADS-NG */
    config.headless = batch_mode;
    config.batch_mode = batch_mode;
    config.gaddir = getenv("GADDIR");
    config.gatdir = getenv("GATDIR");
    config.cache_size = 8 * 1024 * 1024;  /* 8MB default */
    config.use_opendap = 0;
    
    session = grads_ng_init(&config);
    if (!session) {
        fprintf(stderr, "Failed to initialize GrADS-NG\n");
        return 1;
    }
    
    ng_cli_state_t st;
    int exit_code = 0;
    int i;

    memset(&st, 0, sizeof(st));
    st.session = session;

    /* Evaluate an expression and print the numeric result */
    if (expr) {
        grads_ng_parser_t* parser = grads_ng_parser_create(expr);

        if (!parser) {
            fprintf(stderr, "ERROR: out of memory while reading the expression.\n");
            grads_ng_destroy(session);
            return 1;
        }
        {
            grads_ng_ast_node_t* ast = grads_ng_parser_parse(parser);
            if (!ast) {
                const char* perr = grads_ng_parser_error(parser);
                fprintf(stderr, "ERROR: could not parse expression: %s\n\nExpression:\n    %s\n",
                        perr ? perr : "unknown error", expr);
            } else {
                double val;
                char emsg[256];

                if (grads_ng_ast_eval(ast, &val, emsg, sizeof(emsg)) != 0) {
                    fprintf(stderr, "ERROR: %s\n\nExpression:\n    %s\n", emsg, expr);
                } else {
                    printf("%.15g\n", val);
                    exit_code = 0;
                    grads_ng_ast_destroy(ast);
                    grads_ng_parser_destroy(parser);
                    grads_ng_destroy(session);
                    return 0;
                }
                grads_ng_ast_destroy(ast);
            }
        }
        grads_ng_parser_destroy(parser);
        grads_ng_destroy(session);
        return 1;
    }

    /* Inspect a descriptor file */
    if (ctl_file) {
        grads_ng_file_t* file = grads_ng_open(session, ctl_file);

        if (!file) {
            fprintf(stderr, "ERROR: could not open \"%s\".\n\nCheck that the path exists and is readable.\n",
                    ctl_file);
            grads_ng_destroy(session);
            return 1;
        }

        printf("Opened: %s\n", ctl_file);
        print_file_summary(1, ctl_file, file);
        grads_ng_close(file);
    }

    /* Execute a single command */
    if (cmd) {
        char buf[4096];

        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (exec_command(&st, buf) == NG_CMD_ERR) exit_code = 1;
    }

    /* Validate a .gs script file given as positional argument.
     * M1 parses only; execution arrives with the script runtime (M6). */
    if (optind < argc && !cmd && !ctl_file && !expr) {
        char* src = read_text_file(argv[optind]);

        if (!src) {
            fprintf(stderr, "ERROR: could not read script \"%s\".\n", argv[optind]);
            exit_code = 1;
        } else {
            grads_ng_parser_t* parser = grads_ng_parser_create(src);

            if (!parser) {
                fprintf(stderr, "ERROR: out of memory while reading \"%s\".\n", argv[optind]);
                exit_code = 1;
            } else {
                grads_ng_ast_node_t* ast = grads_ng_parser_parse(parser);
                if (!ast) {
                    const char* perr = grads_ng_parser_error(parser);
                    fprintf(stderr, "ERROR: could not parse \"%s\": %s\n",
                            argv[optind], perr ? perr : "unknown error");
                    exit_code = 1;
                } else {
                    printf("Script \"%s\" parsed successfully.\n", argv[optind]);
                    printf("(M1 validates scripts only; execution arrives in M6.)\n");
                    grads_ng_ast_destroy(ast);
                }
                grads_ng_parser_destroy(parser);
            }
            free(src);
        }
    }

    /* Interactive mode */
    if (!batch_mode && !cmd && !ctl_file && !expr && optind >= argc) {
        char line[1024];

        printf("GrADS-NG %d.%d.%d interactive mode\n",
               GRADS_NG_VERSION_MAJOR,
               GRADS_NG_VERSION_MINOR,
               GRADS_NG_VERSION_PATCH);
        printf("Type 'help' for commands, 'quit' to exit\n");
        for (;;) {
            int rc;

            printf("ga-> ");
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) {
                printf("\n");
                break;
            }
            rc = exec_command(&st, line);
            if (rc == NG_CMD_QUIT) break;
        }
    }

    /* Close files opened via -c / REPL, then tear down the session. */
    for (i = st.nfiles - 1; i >= 0; i--) {
        grads_ng_close(st.files[i]);
    }
    grads_ng_destroy(session);

    return exit_code;
}