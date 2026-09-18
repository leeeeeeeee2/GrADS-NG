/*
 * ng/src/render/png.c
 * Minimal byte-deterministic PNG writer (see png.h).
 */

#include "png.h"

#include <stdio.h>
#include <string.h>

static const unsigned char k_sig[8] = {
    137, 80, 78, 71, 13, 10, 26, 10
};

static uint32_t crc_tab[256];
static int crc_ready = 0;

static void crc_init(void) {
    int n, b;
    if (crc_ready) return;
    for (n = 0; n < 256; n++) {
        uint32_t v = (uint32_t)n;
        for (b = 0; b < 8; b++)
            v = (v & 1u) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
        crc_tab[n] = v;
    }
    crc_ready = 1;
}

static uint32_t crc_feed(uint32_t crc, unsigned char byte) {
    return crc_tab[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
}

uint32_t ng_png_crc32(const unsigned char *buf, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    size_t k;
    crc_init();
    for (k = 0; k < len; k++) c = crc_feed(c, buf[k]);
    return c ^ 0xFFFFFFFFu;
}

uint32_t ng_png_adler32(const unsigned char *buf, size_t len) {
    uint32_t a = 1u, b = 0u;
    size_t k;
    for (k = 0; k < len; k++) {
        a = (a + buf[k]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* A chunk under construction: length patched on close (seekable FILE*). */
typedef struct {
    FILE *fp;
    uint32_t crc;   /* running, pre-xor */
    long data_len;
    int failed;
} chunk_t;

static void chunk_begin(chunk_t *c, const char *type) {
    static const unsigned char zero[4] = {0, 0, 0, 0};
    int i;
    c->crc = 0xFFFFFFFFu;
    c->data_len = 0;
    if (fwrite(zero, 1, 4, c->fp) != 4) c->failed = 1;
    if (fwrite(type, 1, 4, c->fp) != 4) c->failed = 1;
    for (i = 0; i < 4; i++) c->crc = crc_feed(c->crc, (unsigned char)type[i]);
}

static void chunk_data(chunk_t *c, const unsigned char *buf, size_t len) {
    size_t k;
    if (len && fwrite(buf, 1, len, c->fp) != len) c->failed = 1;
    for (k = 0; k < len; k++) c->crc = crc_feed(c->crc, buf[k]);
    c->data_len += (long)len;
}

static int chunk_end(chunk_t *c) {
    unsigned char tmp[4];
    long end = ftell(c->fp);
    if (end < 0) return -1;
    put_u32(tmp, c->crc ^ 0xFFFFFFFFu);
    if (fwrite(tmp, 1, 4, c->fp) != 4) return -1;
    /* patch length: [len][type][data][crc], end sits past crc */
    if (fseek(c->fp, end - c->data_len - 8, SEEK_SET) != 0) return -1;
    put_u32(tmp, (uint32_t)c->data_len);
    if (fwrite(tmp, 1, 4, c->fp) != 4) return -1;
    if (fseek(c->fp, end + 4, SEEK_SET) != 0) return -1;
    return c->failed ? -1 : 0;
}

int ng_png_write_rgb(const char *path, const char *label,
                     const unsigned char *rgb, int w, int h) {
    FILE *fp;
    chunk_t c;
    unsigned char hdr[13];
    size_t row;
    size_t stride;      /* filter byte + pixels per scanline */
    uint32_t adler_a = 1u, adler_b = 0u;

    if (!path || !rgb || w <= 0 || h <= 0) return -1;
    crc_init();
    fp = fopen(path, "wb");
    if (!fp) return -1;
    c.fp = fp;
    c.failed = 0;
    if (fwrite(k_sig, 1, 8, fp) != 8) {
        fclose(fp);
        return -1;
    }

    /* IHDR: w, h, 8-bit, truecolor(2), deflate(0), no filter(0), nolace(0) */
    put_u32(hdr, (uint32_t)w);
    put_u32(hdr + 4, (uint32_t)h);
    hdr[8] = 8;
    hdr[9] = 2;
    hdr[10] = 0;
    hdr[11] = 0;
    hdr[12] = 0;
    chunk_begin(&c, "IHDR");
    chunk_data(&c, hdr, 13);
    if (chunk_end(&c) != 0) {
        fclose(fp);
        return -1;
    }

    if (label && *label) {
        static const unsigned char nul = 0;
        chunk_begin(&c, "tEXt");
        chunk_data(&c, (const unsigned char *)"Comment", 7);
        chunk_data(&c, &nul, 1);
        chunk_data(&c, (const unsigned char *)label, strlen(label));
        if (chunk_end(&c) != 0) {
            fclose(fp);
            return -1;
        }
    }

    /* IDAT: zlib header, one stored block per 65535-byte slice of the
     * filter-zero scanline stream, Adler-32 trailer. */
    stride = (size_t)w * 3 + 1;
    chunk_begin(&c, "IDAT");
    {
        static const unsigned char zh[2] = {0x78, 0x01};
        chunk_data(&c, zh, 2);
    }
    for (row = 0; row < (size_t)h; row++) {
        /* logical byte stream of this scanline: filter 0, then pixels */
        size_t total = stride, done = 0;
        while (done < total) {
            /* one stored block */
            size_t take = total - done;
            int last;
            unsigned char blk[5];
            size_t k;
            if (take > 65535) take = 65535;
            last = (row == (size_t)h - 1) && (done + take == total);
            blk[0] = (unsigned char)(last ? 1 : 0); /* BFINAL, BTYPE=00 */
            blk[1] = (unsigned char)(take & 0xFFu);
            blk[2] = (unsigned char)((take >> 8) & 0xFFu);
            blk[3] = (unsigned char)(~take & 0xFFu);
            blk[4] = (unsigned char)((~take >> 8) & 0xFFu);
            chunk_data(&c, blk, 5);
            for (k = 0; k < take; k++) {
                /* byte `done` of the scanline: 0 is the filter byte */
                unsigned char b = (done == 0)
                    ? 0
                    : rgb[row * (size_t)w * 3 + (done - 1)];
                chunk_data(&c, &b, 1);
                adler_a = (adler_a + b) % 65521u;
                adler_b = (adler_b + adler_a) % 65521u;
                done++;
            }
        }
    }
    {
        unsigned char ta[4];
        put_u32(ta, (adler_b << 16) | adler_a);
        chunk_data(&c, ta, 4);
    }
    if (chunk_end(&c) != 0) {
        fclose(fp);
        return -1;
    }

    chunk_begin(&c, "IEND");
    if (chunk_end(&c) != 0) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) return -1;
    return 0;
}
