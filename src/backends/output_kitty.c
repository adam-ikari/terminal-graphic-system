/*
 * output_kitty.c — kitty graphics protocol presentation channel.
 *
 * TGS's primary presentation path: the rendered framebuffer is encoded as
 * kitty graphics APC frames and written to stdout, so the canvas displays
 * inside a kitty-protocol terminal. No window system involved — the
 * terminal IS the display.
 *
 * Same output.h interface as output_sdl.c (debug) and output_fb.c
 * (embedded). kitty is mandatory in the build, not an option.
 */
#include "../compositor/output.h"
#include "kitty_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

/* Kitty presentation state: the canvas buffer + dimensions. */
typedef struct {
    uint8_t *fb;
    int w, h, stride;
} kitty_priv;

static void kitty_home_cursor(int fd)
{
    /* Move cursor to origin so the transmitted image lands top-left. */
    const char home[] = "\x1b[H";
    (void)write(fd, home, sizeof(home) - 1);
}

int output_init(tgs_display *d, int width, int height)
{
    kitty_priv *priv;

    priv = (kitty_priv *)calloc(1, sizeof(*priv));
    if (!priv) return -1;

    priv->w = width;
    priv->h = height;
    priv->stride = width * 4;
    priv->fb = (uint8_t *)calloc(1, (size_t)priv->stride * (size_t)height);
    if (!priv->fb) {
        free(priv);
        return -1;
    }

    d->width = width;
    d->height = height;
    d->bpp = 32;
    d->stride = priv->stride;
    d->buffer = priv->fb;
    d->backend_priv = priv;

    /* Enter the alt screen so the canvas does not scroll the shell. */
    {
        const char smcup[] = "\x1b[?1049h";
        (void)!write(STDOUT_FILENO, smcup, sizeof(smcup) - 1);
    }
    return 0;
}

/* Present pacing: a full-canvas transmission is ~2MB; without a rate cap
 * the compositor's poll loop (5ms) would push 600 fps ≈ 1.2 GB/s into the
 * terminal. 30 fps is the reference presentation cadence; dirty-region
 * transmission is the later optimization. */
#define KITTY_PRESENT_INTERVAL_MS 33

void output_present(tgs_display *d)
{
    static struct timespec last;
    static int have_last;
    struct timespec now;
    kitty_priv *priv = (kitty_priv *)d->backend_priv;

    if (!priv || !d->buffer) return;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (have_last) {
        long ms = (now.tv_sec - last.tv_sec) * 1000L +
                  (now.tv_nsec - last.tv_nsec) / 1000000L;
        if (ms < KITTY_PRESENT_INTERVAL_MS) return;
    }
    last = now;
    have_last = 1;

    kitty_home_cursor(STDOUT_FILENO);
    kitty_encode_frame(STDOUT_FILENO, (const uint8_t *)d->buffer,
                       d->width, d->height, d->stride);
}

int output_resize(tgs_display *d, int width, int height)
{
    kitty_priv *priv = (kitty_priv *)d->backend_priv;
    uint8_t *nfb;

    if (!priv || width < 1 || height < 1) return -1;
    if (width == d->width && height == d->height) return 0;

    nfb = (uint8_t *)calloc(1, (size_t)width * 4 * (size_t)height);
    if (!nfb) return -1;
    free(priv->fb);
    priv->fb = nfb;
    priv->w = width;
    priv->h = height;
    priv->stride = width * 4;

    d->width = width;
    d->height = height;
    d->stride = priv->stride;
    d->buffer = priv->fb;
    return 0;
}

void output_cleanup(tgs_display *d)
{
    kitty_priv *priv = (kitty_priv *)d->backend_priv;

    if (!priv) return;
    /* Leave the alt screen. */
    {
        const char rmcup[] = "\x1b[?1049l";
        (void)!write(STDOUT_FILENO, rmcup, sizeof(rmcup) - 1);
    }
    free(priv->fb);
    free(priv);
    d->backend_priv = NULL;
    d->buffer = NULL;
}
