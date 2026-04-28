/*
 * GrADS-NG CLI Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "grads_ng.h"
#include "parser/grads_ng_parser.h"

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
    
    /* Handle expression evaluation */
    if (expr) {
        grads_ng_parser_t* parser;
        
        parser = grads_ng_parser_create(expr);
        if (parser) {
            grads_ng_ast_node_t* ast = grads_ng_parser_parse(parser);
            if (ast) {
                printf("Expression parsed successfully\n");
                grads_ng_ast_destroy(ast);
            } else {
                fprintf(stderr, "Parse error: %s\n", 
                        grads_ng_parser_error(parser));
                grads_ng_parser_destroy(parser);
                grads_ng_destroy(session);
                return 1;
            }
            grads_ng_parser_destroy(parser);
        }
        
        grads_ng_destroy(session);
        return 0;
    }
    
    /* Handle ctl file */
    if (ctl_file) {
        grads_ng_file_t* file;
        
        file = grads_ng_open(session, ctl_file);
        if (!file) {
            fprintf(stderr, "Failed to open %s\n", ctl_file);
            grads_ng_destroy(session);
            return 1;
        }
        
        printf("Opened: %s\n", ctl_file);
        printf("  Type: %s\n", file->type == 1 ? "station" : 
                                  (file->type == 2 ? "bufr" : "grid"));
        printf("  Dimensions: %d x %d x %d x %d\n",
               file->nx, file->ny, file->nz, file->nt);
        
        grads_ng_close(file);
    }
    
    /* Handle command */
    if (cmd) {
        /* Execute command (placeholder) */
        printf("Executing: %s\n", cmd);
    }
    
    /* Interactive mode */
    if (!batch_mode && !cmd && !ctl_file && !expr) {
        printf("GrADS-NG %d.%d.%d interactive mode\n",
               GRADS_NG_VERSION_MAJOR,
               GRADS_NG_VERSION_MINOR,
               GRADS_NG_VERSION_PATCH);
        printf("Type 'quit' or 'exit' to exit\n");
        printf("> ");
        
        /* Simple REPL placeholder */
        char line[1024];
        while (fgets(line, sizeof(line), stdin)) {
            if (strcmp(line, "quit\n") == 0 || 
                strcmp(line, "exit\n") == 0) {
                break;
            }
            printf("> ");
        }
    }
    
    grads_ng_destroy(session);
    
    return 0;
}