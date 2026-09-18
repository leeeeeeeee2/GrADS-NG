/*
 * ng/src/render/png.h
 * M5 slice 2: dependency-free PNG output.
 *
 * 8-bit truecolor (RGB) only — exactly what the shaded+contour raster
 * produces. Byte-deterministic by construction: fixed zlib stored blocks
 * (no compression search, no timestamps), so identical rasters yield
 * identical files and rendered output stays CTest-comparable. Small-file
 * compression is a later performance slice, not a correctness one.
 */

#ifndef NG_PNG_H
#define NG_PNG_H

#include <stddef.h>
#include <stdint.h>

/* ISO 3309 CRC-32 over buf (PNG chunk checksums). */
uint32_t ng_png_crc32(const unsigned char *buf, size_t len);

/* Adler-32 (zlib stream checksum). */
uint32_t ng_png_adler32(const unsigned char *buf, size_t len);

/* Write rgb (w*h*3 bytes, row-major, top row first) as PNG with a tEXt
 * label chunk for traceability. Returns 0 or -1 (errno left set). */
int ng_png_write_rgb(const char *path, const char *label,
                     const unsigned char *rgb, int w, int h);

#endif /* NG_PNG_H */
