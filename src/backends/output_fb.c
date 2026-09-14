/*
 * TGS Framebuffer Output Backend
 * Direct /dev/fb0 output for embedded Linux.
 * mmap'd buffer — LVGL writes directly, no present needed.
 */
#include "output.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int      fd;
    uint8_t *mem;
    size_t   mem_size;
} fb_priv;

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

    d->width       = vinfo.xres;
    d->height      = vinfo.yres;
    d->bpp         = vinfo.bits_per_pixel;
    d->stride      = finfo.line_length;
    d->buffer      = priv->mem;
    d->backend_priv = priv;

    fprintf(stderr, "fb: %dx%d %dbpp stride=%d\n",
            d->width, d->height, d->bpp, d->stride);
    return 0;
}

void output_present(tgs_display *d)
{
    (void)d;
    /* mmap'd /dev/fb0 — writes are immediately visible. */
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
}
