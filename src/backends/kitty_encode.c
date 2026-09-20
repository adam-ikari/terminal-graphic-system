/*
 * kitty_encode.c — kitty graphics protocol encoder (shared).
 *
 * Encodes the framebuffer as a kitty graphics transmission over the APC
 * channel:
 *
 *   ESC _ Gf=100,a=T,q=2,m=<0|1>;<base64 PNG> ESC \   (chunked)
 *
 * The payload is a PNG (f=100) — raw RGBA costs 25x the bytes on the wire,
 * which swamps a terminal at 30 fps. PNG via stb_image_write (vendored in
 * deps/libsixel); the base64 chunks are C99 self-contained.
 *
 * C99. Shared by the presentation channel (output_kitty.c) and any client
 * that wants to place pixels directly.
 */
#define _POSIX_C_SOURCE 200809L
#include "kitty_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- stb_image_write (single compilation unit; vendored) --- */
#ifdef __cplusplus
extern "C" {
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_NO_ZLIB 0
#include "stb_image_write.h"
#ifdef __cplusplus
}
#endif

/* --- PNG collection callback --- */
typedef struct {
    uint8_t *buf;
    size_t len, cap;
} png_buf;

static void png_append(void *ctx, void *data, int size)
{
    png_buf *b = (png_buf *)ctx;

    if (b->len + (size_t)size > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 65536;
        while (ncap < b->len + (size_t)size) ncap *= 2;
        b->buf = (uint8_t *)realloc(b->buf, ncap);
        if (!b->buf) return;
        b->cap = ncap;
    }
    memcpy(b->buf + b->len, data, (size_t)size);
    b->len += (size_t)size;
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
        size_t rem = in_len - i;
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

    /* ARGB8888 (memory: B,G,R,A) → RGBA for stb. */
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

    stbi_write_png_compression_level = 6;
    if (!stbi_write_png_to_func(png_append, &pb, w, h, 4, rgba,
                                (int)w * 4)) {
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
        if (write(fd, ctrl, (size_t)cn) != (ssize_t)cn) {
            free(b64);
            free(pb.buf);
            return -1;
        }
        if (write(fd, b64 + off, (size_t)take) != (ssize_t)take) {
            free(b64);
            free(pb.buf);
            return -1;
        }
        if (write(fd, "\x1b\\", 2) != 2) {
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
