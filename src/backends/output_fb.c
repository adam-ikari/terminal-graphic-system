/*
 * TGS Framebuffer Output Backend
 * Direct /dev/fb0 output for embedded Linux.
 *
 * Buffer contract (same one output_sdl.c implements, see output.h): the LVGL
 * backend owns the draw buffer and publishes it as d->buffer. This backend
 * keeps the mmap'd framebuffer *private* and output_present() is what pushes
 * d->buffer into it — LVGL never writes the mmap itself, so a missing present
 * used to mean a black screen.
 */
#include "output.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int      fd;
    uint8_t *mem;       /* mmap of /dev/fb0 — the real surface */
    size_t   mem_size;
    int      width, height;
    int      fb_bpp;    /* vinfo.bits_per_pixel */
    int      fb_stride; /* finfo.line_length — may exceed width*bpp/8 */
    struct fb_bitfield r, g, b, a;
} fb_priv;

/* Scale an 8-bit channel into a framebuffer channel descriptor. */
static uint32_t pack_channel(uint8_t v, const struct fb_bitfield *bf)
{
    uint32_t len = bf->length > 8 ? 8 : bf->length;
    uint32_t t;

    if (len == 0) return 0;
    t = (len >= 8) ? (uint32_t)v : ((uint32_t)v >> (8 - len));
    return (t & ((1u << len) - 1u)) << bf->offset;
}

/* One LVGL ARGB8888 pixel → the framebuffer's own channel layout. */
static uint32_t pack_pixel(const fb_priv *p, uint32_t argb)
{
    uint32_t v = pack_channel((uint8_t)(argb >> 16), &p->r)
               | pack_channel((uint8_t)(argb >> 8),  &p->g)
               | pack_channel((uint8_t)(argb),       &p->b);

    if (p->a.length) v |= pack_channel(0xFF, &p->a);
    return v;
}

int output_init(tgs_display *d, int width, int height)
{
    (void)width;
    (void)height;

    fb_priv *priv = calloc(1, sizeof(fb_priv));
    if (!priv) return -1;

    priv->fd = open("/dev/fb0", O_RDWR);
    if (priv->fd < 0) {
        fprintf(stderr, "fb: cannot open /dev/fb0\n");
        free(priv);
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(priv->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(priv->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("fb: ioctl");
        close(priv->fd);
        free(priv);
        return -1;
    }

    priv->mem_size = finfo.smem_len;
    priv->mem = mmap(NULL, priv->mem_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, priv->fd, 0);
    if (priv->mem == MAP_FAILED) {
        perror("fb: mmap");
        close(priv->fd);
        free(priv);
        return -1;
    }

    priv->width     = vinfo.xres;
    priv->height    = vinfo.yres;
    priv->fb_bpp    = vinfo.bits_per_pixel;
    priv->fb_stride = finfo.line_length;
    priv->r = vinfo.red;
    priv->g = vinfo.green;
    priv->b = vinfo.blue;
    priv->a = vinfo.transp;

    if ((size_t)priv->fb_stride * (size_t)priv->height > priv->mem_size) {
        fprintf(stderr, "fb: smem_len %zu too small for %dx%d stride %d\n",
                priv->mem_size, priv->width, priv->height, priv->fb_stride);
        munmap(priv->mem, priv->mem_size);
        close(priv->fd);
        free(priv);
        return -1;
    }

    /* width/height/bpp/stride describe the LVGL draw buffer, not the panel;
     * the panel's format lives in this file's private struct. */
    d->width        = priv->width;
    d->height       = priv->height;
    d->bpp          = 32;
    d->stride       = priv->width * 4;
    d->buffer       = NULL; /* LVGL backend provides it */
    d->backend_priv = priv;

    fprintf(stderr, "fb: %dx%d %dbpp stride=%d (draw buffer 32bpp)\n",
            d->width, d->height, priv->fb_bpp, priv->fb_stride);
    return 0;
}

void output_present(tgs_display *d)
{
    fb_priv *p = (fb_priv *)d->backend_priv;
    if (!p || !d->buffer) return;

    /* Fast path: the panel already stores 32bpp with LVGL's channel order
     * (BGRA/ARGB8888 little-endian; alpha bits are ignored). */
    const int byte_copy = (p->fb_bpp == 32 &&
                           p->r.offset == 16 && p->r.length == 8 &&
                           p->g.offset == 8  && p->g.length == 8 &&
                           p->b.offset == 0  && p->b.length == 8);
    const int row_bytes = p->width * 4;

    for (int y = 0; y < p->height; y++) {
        /* Source stride is the draw buffer's; the panel may pad rows wider. */
        const uint8_t *src = d->buffer + (size_t)y * d->stride;
        uint8_t *dst = p->mem + (size_t)y * p->fb_stride;

        if (byte_copy) {
            memcpy(dst, src, (size_t)row_bytes);
            continue;
        }

        switch (p->fb_bpp) {
        case 32:
            for (int x = 0; x < p->width; x++)
                ((uint32_t *)dst)[x] = pack_pixel(p, ((const uint32_t *)src)[x]);
            break;

        case 24:
            for (int x = 0; x < p->width; x++) {
                uint32_t v = pack_pixel(p, ((const uint32_t *)src)[x]);
                dst[x * 3 + 0] = (uint8_t)(v);
                dst[x * 3 + 1] = (uint8_t)(v >> 8);
                dst[x * 3 + 2] = (uint8_t)(v >> 16);
            }
            break;

        case 16:
            for (int x = 0; x < p->width; x++)
                ((uint16_t *)dst)[x] =
                    (uint16_t)pack_pixel(p, ((const uint32_t *)src)[x]);
            break;

        default: {
            /* Anything else (paletted, YUV, ...) is not handled. */
            static int warned;
            if (!warned++) {
                fprintf(stderr, "fb: unsupported %dbpp — display not updated\n",
                        p->fb_bpp);
            }
            return;
        }
        }
    }
}

void output_cleanup(tgs_display *d)
{
    fb_priv *priv = d->backend_priv;
    if (!priv) return;

    if (priv->mem && priv->mem != MAP_FAILED)
        munmap(priv->mem, priv->mem_size);
    if (priv->fd >= 0)
        close(priv->fd);
    free(priv);
    d->backend_priv = NULL;
    d->buffer = NULL;
}
