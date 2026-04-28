/* grads_ng.c - GrADS-NG Main Program */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gs_parser.h"
#include "gx_virtual.h"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

int main(int argc, char *argv[]) {
    printf("GrADS-NG v2.2.1 - Modernized GrADS with Virtual Display\n");
    printf("Copyright (C) 2026 GrADS-NG project\n\n");

    /* Parse command line arguments */
    char *script_file = NULL;
    int batch_mode = 0;
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0) {
            batch_mode = 1;
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            /* Parse geometry: WIDTHxHEIGHT */
            char *geom = argv[++i];
            if (sscanf(geom, "%dx%d", &width, &height) != 2) {
                fprintf(stderr, "Invalid geometry format: %s\n", geom);
                return 1;
            }
        } else if (argv[i][0] != '-') {
            script_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [-b] [-g WIDTHxHEIGHT] [script_file]\n", argv[0]);
            return 1;
        }
    }

    /* Initialize virtual display */
    gx_virtual_t display;
    if (gx_virtual_init(&display, width, height) != 0) {
        fprintf(stderr, "Failed to initialize virtual display\n");
        return 1;
    }

    printf("Virtual display: %dx%d pixels\n", width, height);

    /* Initialize script context */
    gs_context_t context;
    if (gs_context_init(&context) != 0) {
        fprintf(stderr, "Failed to initialize script context\n");
        gx_virtual_cleanup(&display);
        return 1;
    }

    int result = 0;

    if (script_file) {
        /* Execute script file */
        printf("Executing script: %s\n", script_file);
        result = gs_execute_script(&context, script_file);
    } else if (!batch_mode) {
        /* Interactive mode */
        printf("Interactive mode (not yet implemented)\n");
        printf("Use -b for batch mode or specify a script file\n");
    }

    /* Export result if in batch mode */
    if (batch_mode) {
        printf("Batch mode: exporting results...\n");

        /* Export as PNG */
        char png_file[256];
        snprintf(png_file, sizeof(png_file), "grads_output.png");
        if (gx_virtual_export_png(&display, png_file) == 0) {
            printf("Output saved to: %s\n", png_file);
        } else {
            printf("Failed to export PNG\n");
        }

        /* Export as SVG */
        char svg_file[256];
        snprintf(svg_file, sizeof(svg_file), "grads_output.svg");
        if (gx_virtual_export_svg(&display, svg_file) == 0) {
            printf("Vector output saved to: %s\n", svg_file);
        } else {
            printf("Failed to export SVG\n");
        }
    }

    /* Cleanup */
    gs_context_cleanup(&context);
    gx_virtual_cleanup(&display);

    printf("GrADS-NG execution completed\n");
    return result;
}