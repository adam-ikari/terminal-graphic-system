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

/* ARGB8888 (memory: B,G,R,A) → RGBA for the rectangle (x0,y0,w,h). Only the
 * pixels that are going out are touched — that is the point of the rect path. */
static void argb_to_rgba_rect(uint8_t *dst, const uint8_t *src, int stride,
                              int x0, int y0, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)(y0 + y) * (size_t)stride +
                             (size_t)x0 * 4u;
        uint8_t *d = dst + (size_t)y * (size_t)w * 4u;
        int x;
        for (x = 0; x < w; x++) {
            d[x * 4 + 0] = row[x * 4 + 2];
            d[x * 4 + 1] = row[x * 4 + 1];
            d[x * 4 + 2] = row[x * 4 + 0];
            d[x * 4 + 3] = row[x * 4 + 3];
        }
    }
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

/* Write a base64 payload as one chained transmission: the first APC frame
 * carries the control keys (and m=1 when more follows), continuations carry
 * only m=, the last one has m=0. keys is the complete key list, e.g.
 * "f=100,a=T,q=2" or "i=7,f=100,a=T,q=2,C=1,X=3,Y=4". */
static int write_transmission(int fd, const char *keys, const char *b64,
                              int b64_len)
{
    int off = 0, first = 1;

    while (first || off < b64_len) {
        char ctrl[256];
        int take = b64_len - off > B64_CHUNK ? B64_CHUNK : b64_len - off;
        int more = (off + take < b64_len) ? 1 : 0;
        int cn;

        if (first) {
            cn = snprintf(ctrl, sizeof(ctrl), "\x1b_G%s,m=%d;", keys, more);
            first = 0;
        } else {
            cn = snprintf(ctrl, sizeof(ctrl), "\x1b_Gm=%d;", more);
        }
        if (cn < 0 || cn >= (int)sizeof(ctrl)) return -1;
        if (write(fd, ctrl, (size_t)cn) != (ssize_t)cn ||
            (take > 0 && write(fd, b64 + off, (size_t)take) != (ssize_t)take) ||
            write(fd, "\x1b\\", 2) != 2)
            return -1;
        off += take;
    }
    return 0;
}

/* Deflate `rgba` (w×h, top-down, filter-0 rows) and send it as a
 * transmission with the given control keys. */
static int encode_and_send(int fd, const uint8_t *rgba, int w, int h,
                           const char *keys)
{
    png_buf pb = {0};
    char *b64;
    int ret = -1, b64_len;

    if (png_encode_rgba(&pb, rgba, w, h, w * 4) < 0) goto done;
    b64 = (char *)malloc(((size_t)pb.len + 2) / 3 * 4 + 4);
    if (!b64) goto done;
    b64_len = b64_encode(pb.buf, pb.len, b64);
    ret = write_transmission(fd, keys, b64, b64_len);
    free(b64);
done:
    free(pb.buf);
    return ret;
}

int kitty_encode_frame(int fd, const uint8_t *argb, int w, int h, int stride)
{
    uint8_t *rgba;
    int ret;

    if (!argb || w <= 0 || h <= 0) return -1;

    rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!rgba) return -1;
    argb_to_rgba_rect(rgba, argb, stride, 0, 0, w, h);
    ret = encode_and_send(fd, rgba, w, h, "f=100,a=T,q=2");
    free(rgba);
    return ret;
}

int kitty_encode_rect(int fd, const uint8_t *argb, int fb_w, int fb_h,
                      int stride, int rx, int ry, int rw, int rh,
                      const kitty_place *place)
{
    uint8_t *rgba;
    char keys[96], cup[32];
    int cn, ret;

    if (!argb || !place || fb_w <= 0 || fb_h <= 0) return -1;
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > fb_w) rw = fb_w - rx;
    if (ry + rh > fb_h) rh = fb_h - ry;
    if (rw <= 0 || rh <= 0) return -1;

    rgba = (uint8_t *)malloc((size_t)rw * (size_t)rh * 4u);
    if (!rgba) return -1;
    argb_to_rgba_rect(rgba, argb, stride, rx, ry, rw, rh);

    /* kitty places an image at the cursor's cell: move there first, then
     * transmit with C=1 (no post-placement cursor movement) and the pixel
     * offsets, which the spec requires to be smaller than the cell. */
    cn = snprintf(cup, sizeof(cup), "\x1b[%d;%dH",
                  place->row + 1, place->col + 1);
    if (cn < 0 || write(fd, cup, (size_t)cn) != (ssize_t)cn) {
        free(rgba);
        return -1;
    }
    cn = snprintf(keys, sizeof(keys), "i=%u,f=100,a=T,q=2,C=1,X=%d,Y=%d",
                  place->image_id, place->xoff, place->yoff);
    if (cn < 0 || cn >= (int)sizeof(keys)) {
        free(rgba);
        return -1;
    }
    ret = encode_and_send(fd, rgba, rw, rh, keys);
    free(rgba);
    return ret;
}
