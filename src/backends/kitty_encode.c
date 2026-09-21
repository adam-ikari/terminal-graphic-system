/*
 * kitty_encode.c — kitty graphics protocol encoder (shared).
 *
 * Encodes the framebuffer as a kitty graphics transmission over the APC
 * channel:
 *
 *   ESC _ Gf=100,a=T,q=2,m=<0|1>;<base64 PNG> ESC \   (chunked)
 *
 * The payload is a PNG (f=100). PNG encoding uses system zlib at level 1:
 * stb's own deflate measured ~25 encodes/s on an 800x600 RGBA canvas —
 * 3x too slow for 60 fps, 5x for 120; zlib level 1 measures ~180/s
 * (~5.5 ms) with comparable size for UI-like content. zlib is a baseline
 * component on every platform TGS targets. base64 and APC chunking are
 * C99 self-contained.
 *
 * C99. Shared by the presentation channel (output_kitty.c) and any client
 * that wants to place pixels directly.
 */
#define _POSIX_C_SOURCE 200809L
#include "kitty_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* --- PNG collection buffer --- */
typedef struct {
    uint8_t *buf;
    size_t len, cap;
} png_buf;

static int png_put(png_buf *b, const void *data, size_t size)
{
    if (b->len + size > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 65536;
        uint8_t *nb;
        while (ncap < b->len + size) ncap *= 2;
        nb = (uint8_t *)realloc(b->buf, ncap);
        if (!nb) return -1;
        b->buf = nb;
        b->cap = ncap;
    }
    memcpy(b->buf + b->len, data, size);
    b->len += size;
    return 0;
}

static int png_put_u32be(png_buf *b, uint32_t v)
{
    uint8_t t[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 8), (uint8_t)v };
    return png_put(b, t, 4);
}

static uint32_t crc32_byte(uint32_t c, uint8_t b)
{
    int k;
    c ^= b;
    for (k = 0; k < 8; k++)
        c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    return c;
}

/* PNG chunk: length, type, data, CRC-32 over type+data. */
static int png_chunk(png_buf *b, const char type[4], const uint8_t *data,
                     size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    if (png_put_u32be(b, (uint32_t)len) < 0) return -1;
    if (png_put(b, type, 4) < 0) return -1;
    if (len && png_put(b, data, len) < 0) return -1;
    for (i = 0; i < 4; i++) crc = crc32_byte(crc, (uint8_t)type[i]);
    for (i = 0; i < len; i++) crc = crc32_byte(crc, data[i]);
    return png_put_u32be(b, crc ^ 0xFFFFFFFFu);
}

/* Encode a top-down RGBA8888 buffer as PNG (color type 6, filter 0 rows)
 * with zlib level-1 deflate. Returns 0 on success. */
static int png_encode_rgba(png_buf *out, const uint8_t *rgba, int w, int h,
                           int stride)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t ihdr[13];
    uint8_t *raw = NULL, *zbuf = NULL;
    uLongf zlen;
    size_t raw_len = (size_t)h * ((size_t)w * 4u + 1u);
    size_t y;
    int ret = -1;

    raw = (uint8_t *)malloc(raw_len);
    zbuf = (uint8_t *)malloc(compressBound((uLong)raw_len));
    if (!raw || !zbuf) goto done;

    /* Filter byte 0 (None) per row. */
    for (y = 0; y < (size_t)h; y++) {
        raw[y * ((size_t)w * 4u + 1u)] = 0;
        memcpy(raw + y * ((size_t)w * 4u + 1u) + 1,
               rgba + y * (size_t)stride, (size_t)w * 4u);
    }

    zlen = compressBound((uLong)raw_len);
    if (compress2(zbuf, &zlen, raw, (uLong)raw_len, 1) != Z_OK) goto done;

    if (png_put(out, sig, 8) < 0) goto done;
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 6;   /* color type: RGBA */
    ihdr[10] = 0;  /* compression: deflate */
    ihdr[11] = 0;  /* filter: adaptive (we emit only type 0) */
    ihdr[12] = 0;  /* interlace: none */
    if (png_chunk(out, "IHDR", ihdr, 13) < 0) goto done;
    if (png_chunk(out, "IDAT", zbuf, zlen) < 0) goto done;
    if (png_chunk(out, "IEND", NULL, 0) < 0) goto done;
    ret = 0;
done:
    free(raw);
    free(zbuf);
    return ret;
}

/* --- base64 (C99, no deps) --- */
static int b64_encode(const uint8_t *in, size_t in_len, char *out)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0, i = 0;

    while (i + 2 < in_len) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) |
                     in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = tbl[(v >> 6) & 63];
        out[o++] = tbl[v & 63];
        i += 3;
    }
    if (i < in_len) {
        uint32_t v = (uint32_t)in[i] << 16;
        int rem = (int)(in_len - i);
        if (rem == 2) v |= (uint32_t)in[i + 1] << 8;
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = rem == 2 ? tbl[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    return (int)o;
}

#define B64_CHUNK 4096

int kitty_encode_frame(int fd, const uint8_t *argb, int w, int h, int stride)
{
    png_buf pb = {0};
    uint8_t *rgba;
    char *b64;
    int b64_len, y, off, first = 1;

    if (!argb || w <= 0 || h <= 0) return -1;

    /* ARGB8888 (memory: B,G,R,A) → RGBA. */
    rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!rgba) return -1;
    for (y = 0; y < h; y++) {
        const uint8_t *row = argb + (size_t)y * (size_t)stride;
        uint8_t *dst = rgba + (size_t)y * (size_t)w * 4u;
        int x;
        for (x = 0; x < w; x++) {
            dst[x * 4 + 0] = row[x * 4 + 2];
            dst[x * 4 + 1] = row[x * 4 + 1];
            dst[x * 4 + 2] = row[x * 4 + 0];
            dst[x * 4 + 3] = row[x * 4 + 3];
        }
    }

    if (png_encode_rgba(&pb, rgba, w, h, w * 4) < 0) {
        free(rgba);
        free(pb.buf);
        return -1;
    }
    free(rgba);

    b64 = (char *)malloc(((size_t)pb.len + 2) / 3 * 4 + 4);
    if (!b64) {
        free(pb.buf);
        return -1;
    }
    b64_len = b64_encode(pb.buf, pb.len, b64);

    /* Chunked transmission: first frame carries f=100,a=T,q=2,m=<n>;data,
     * continuations carry m=<n>;data, final m=0. */
    for (off = 0; off < b64_len || first; ) {
        char ctrl[64];
        int cn, take = b64_len - off > B64_CHUNK ? B64_CHUNK
                                                 : b64_len - off;
        int more = (off + take < b64_len) ? 1 : 0;

        if (first) {
            cn = snprintf(ctrl, sizeof(ctrl),
                          "\x1b_Gf=100,a=T,q=2,m=%d;", more);
            first = 0;
        } else {
            cn = snprintf(ctrl, sizeof(ctrl), "\x1b_Gm=%d;", more);
        }
        if (write(fd, ctrl, (size_t)cn) != (ssize_t)cn ||
            write(fd, b64 + off, (size_t)take) != (ssize_t)take ||
            write(fd, "\x1b\\", 2) != 2) {
            free(b64);
            free(pb.buf);
            return -1;
        }
        off += take;
    }

    free(b64);
    free(pb.buf);
    return 0;
}
