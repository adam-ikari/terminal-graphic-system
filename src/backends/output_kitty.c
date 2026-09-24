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

/* Kitty presentation state: the canvas buffer, the last frame that actually
 * reached the host (the diff baseline), and the host's cell geometry.
 *
 * The buffer that gets *presented* is always d->buffer — whoever publishes
 * it owns it. The scene backend allocates its own fb and republishes it
 * (backend_init, backend_set_size), superseding the buffer allocated here;
 * a test or a bare output.c user keeps this one. `fb` is therefore only the
 * fallback allocation — presenting from it directly would transmit a canvas
 * nothing paints into. */
typedef struct {
    uint8_t *fb;
    uint8_t *prev;        /* last presented pixels — the dirty baseline */
    int w, h, stride;
    int cell_w, cell_h;   /* host cell size in px; 0 = unknown */
    int tiles_x, tiles_y;
    uint32_t ids_used;    /* tile image ids handed out (for deletion) */
    int presented;        /* the host has a first complete canvas */
} kitty_priv;

/* The canvas is transmitted as a fixed grid of tiles, each tile a persistent
 * kitty image id. A present sends only the tiles whose bytes changed.
 *
 * Why tiles rather than one dirty rectangle: kitty deletes an image's data
 * together with its placements, so re-transmitting "the changed rect" under
 * one id would blank whatever that id previously covered. A non-overlapping
 * partition is the shape that is both bounded (live image data = exactly one
 * canvas) and self-consistent (tiles never overlap, so z-order never matters).
 * An idle frame writes zero bytes. */
#define TILE 64
/* Tile ids live in their own range ("tg" in ASCII) so a collision with a
 * stray icat id is unlikely; reusing an id is what makes updates replace. */
#define TILE_ID_BASE 0x74670000u

static void kitty_home_cursor(int fd)
{
    /* Move cursor to origin so the transmitted image lands top-left. */
    const char home[] = "\x1b[H";
    (void)write(fd, home, sizeof(home) - 1);
}

/* The placement of a pixel rect needs the host's cell size: kitty anchors an
 * image at a cell and allows only sub-cell pixel offsets. Ask the terminal —
 * TIOCGWINSZ carries ws_xpixel/ws_ypixel (kitty and xterm report them) — or
 * take TGS_CELL_W/TGS_CELL_H, which is how a capture harness states the
 * geometry it will decode with. Without either, presentation falls back to
 * full-canvas frames, which need no cell geometry. */
static void kitty_detect_cell(kitty_priv *p)
{
    const char *ew = getenv("TGS_CELL_W");
    const char *eh = getenv("TGS_CELL_H");

    p->cell_w = p->cell_h = 0;
    if (ew && eh && *ew && *eh) {
        p->cell_w = atoi(ew);
        p->cell_h = atoi(eh);
    } else {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
            ws.ws_col > 0 && ws.ws_row > 0 &&
            ws.ws_xpixel >= ws.ws_col && ws.ws_ypixel >= ws.ws_row) {
            p->cell_w = ws.ws_xpixel / ws.ws_col;
            p->cell_h = ws.ws_ypixel / ws.ws_row;
        }
    }
    if (p->cell_w < 1 || p->cell_h < 1) p->cell_w = p->cell_h = 0;
}

static int kitty_tiles_ready(const kitty_priv *p)
{
    return p->cell_w > 0 && p->cell_h > 0;
}

/* Drop every tile image this presenter owns (resize or shutdown). d=R frees
 * the data for ids in [x,y]; older kitty (pre-0.33) ignores it and simply
 * keeps the stale tiles until their id is reused, which the next first frame
 * does anyway. */
static void kitty_delete_tiles(const kitty_priv *p)
{
    char buf[128];
    int n;

    if (p->ids_used == 0) return;
    n = snprintf(buf, sizeof(buf), "\x1b_Ga=d,d=R,x=%u,y=%u\x1b\\",
                 (unsigned)TILE_ID_BASE,
                 (unsigned)(TILE_ID_BASE + p->ids_used - 1));
    if (n > 0) (void)!write(STDOUT_FILENO, buf, (size_t)n);
}

static void kitty_grid_reset(kitty_priv *p)
{
    p->tiles_x = (p->w + TILE - 1) / TILE;
    p->tiles_y = (p->h + TILE - 1) / TILE;
}

/* Re-point the presenter at a canvas of new geometry: free the host's tiles
 * (their layout no longer matches), baseline the new size, and force a
 * complete first frame. `fb` is untouched — the published buffer is the
 * caller's (d->buffer), not ours to reallocate. */
static void grid_resize(kitty_priv *p, int w, int h, int stride)
{
    kitty_delete_tiles(p);
    p->ids_used = 0;
    free(p->prev);
    p->prev = (uint8_t *)calloc(1, (size_t)h * (size_t)stride);
    p->w = w;
    p->h = h;
    p->stride = stride;
    kitty_grid_reset(p);
    p->presented = 0;
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
    /* calloc: a tile that fails to encode keeps its baseline, and a garbage
     * baseline would read as "changed" against the very first diff. */
    priv->prev = (uint8_t *)calloc(1, (size_t)priv->stride * (size_t)height);
    if (!priv->fb || !priv->prev) {
        free(priv->fb);
        free(priv->prev);
        free(priv);
        return -1;
    }

    d->width = width;
    d->height = height;
    d->bpp = 32;
    d->stride = priv->stride;
    d->buffer = priv->fb;
    d->backend_priv = priv;

    kitty_detect_cell(priv);
    kitty_grid_reset(priv);

    /* Enter the alt screen so the canvas does not scroll the shell. */
    {
        const char smcup[] = "\x1b[?1049h";
        (void)!write(STDOUT_FILENO, smcup, sizeof(smcup) - 1);
    }
    return 0;
}

/* Present pacing: the gate caps how often a present may transmit — the
 * compositor's 1ms poll loop would otherwise encode and write the dirty
 * tiles on every iteration. Cadence comes from TGS_FPS (default 60, cap
 * 240; integer ms, so 120 → 8ms → a 125Hz ceiling). The tile path costs
 * only what changed, so the gate is a bandwidth/CPU ceiling rather than
 * an encode deadline — the full-canvas fallback still needs ~7ms a frame
 * and is what makes the old 102fps ceiling. */
static long present_interval_ms(void)
{
    static long cached = -1;

    if (cached < 0) {
        const char *env = getenv("TGS_FPS");
        long fps = env ? atol(env) : 60;
        if (fps < 1) fps = 60;
        if (fps > 240) fps = 240;
        cached = 1000L / fps;
    }
    return cached;
}

/* Full-canvas transmission — when the host reports no cell geometry (a
 * file-captured stdout, a terminal that does not advertise ws_xpixel).
 * The honest fallback: it needs no placement math. */
static void present_full(kitty_priv *priv, const uint8_t *src)
{
    kitty_home_cursor(STDOUT_FILENO);
    kitty_encode_frame(STDOUT_FILENO, src, priv->w, priv->h, priv->stride);
    memcpy(priv->prev, src, (size_t)priv->stride * (size_t)priv->h);
}

/* A tile is a rectangle, not a contiguous block: its rows are `stride`
 * apart. Comparing or storing it as one run of bytes would instead touch a
 * horizontal strip crossing neighbouring tiles — which syncs a change into
 * the baseline before the tile that owns it is even examined. */
static int tile_differs(const uint8_t *fb, const uint8_t *prev, int stride,
                        int tw, int th)
{
    int y;
    for (y = 0; y < th; y++)
        if (memcmp(fb + (size_t)y * (size_t)stride,
                   prev + (size_t)y * (size_t)stride,
                   (size_t)tw * 4u) != 0)
            return 1;
    return 0;
}

static void tile_store(uint8_t *prev, const uint8_t *fb, int stride,
                       int tw, int th)
{
    int y;
    for (y = 0; y < th; y++)
        memcpy(prev + (size_t)y * (size_t)stride,
               fb + (size_t)y * (size_t)stride,
               (size_t)tw * 4u);
}

/* Transmit only the tiles whose bytes differ from the frame the host last
 * received (`force` = send everything: nothing is on the host yet). `src` is
 * the published canvas (d->buffer), not a buffer this module owns. Returns
 * the number of tiles sent; zero means the caller writes nothing at all. */
static int present_tiles(kitty_priv *priv, const uint8_t *src, int force)
{
    int ty, tx, sent = 0;

    for (ty = 0; ty < priv->tiles_y; ty++) {
        int y = ty * TILE;
        int th = (priv->h - y < TILE) ? (priv->h - y) : TILE;
        for (tx = 0; tx < priv->tiles_x; tx++) {
            int x = tx * TILE;
            int tw = (priv->w - x < TILE) ? (priv->w - x) : TILE;
            size_t off = (size_t)y * priv->stride + (size_t)x * 4;
            kitty_place pl;

            if (!force &&
                !tile_differs(src + off, priv->prev + off, priv->stride,
                              tw, th))
                continue;

            /* Pixel rect → (cell, sub-cell offset): kitty's placement unit
             * is the cell, and X/Y must stay inside it. */
            pl.image_id = TILE_ID_BASE +
                          (uint32_t)(ty * priv->tiles_x + tx);
            pl.col = x / priv->cell_w;
            pl.row = y / priv->cell_h;
            pl.xoff = x % priv->cell_w;
            pl.yoff = y % priv->cell_h;

            if (kitty_encode_rect(STDOUT_FILENO, src, priv->w, priv->h,
                                  priv->stride, x, y, tw, th, &pl) == 0) {
                tile_store(priv->prev + off, src + off, priv->stride,
                           tw, th);
                sent++;
            }
        }
    }
    /* Cursor back to origin once the batch is in (3 bytes; it also marks a
     * non-empty present in a captured stream). */
    if (sent) kitty_home_cursor(STDOUT_FILENO);
    return sent;
}

void output_present(tgs_display *d)
{
    static struct timespec last;
    static int have_last;
    struct timespec now;
    kitty_priv *priv = (kitty_priv *)d->backend_priv;
    const uint8_t *src;

    if (!priv || !d->buffer) return;

    /* The published canvas is authoritative: if its geometry moved without
     * output_resize (the scene backend republishes its own fb on set_size),
     * re-baseline the grid rather than diffing mismatched strides. */
    if (d->width != priv->w || d->height != priv->h ||
        d->stride != priv->stride) {
        if (d->width < 1 || d->height < 1 || d->stride < d->width * 4)
            return;
        grid_resize(priv, d->width, d->height, d->stride);
    }
    /* grid_resize's calloc can fail, leaving no diff baseline — presenting
     * would diff/store against nothing. Retry the baseline at the current
     * geometry (which also forces a complete first frame, the host's tiles
     * having just been dropped); if memory is still not there, present
     * nothing until it is. */
    if (!priv->prev) {
        grid_resize(priv, priv->w, priv->h, priv->stride);
        if (!priv->prev) return;
    }
    src = (const uint8_t *)d->buffer;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (have_last) {
        long ms = (now.tv_sec - last.tv_sec) * 1000L +
                  (now.tv_nsec - last.tv_nsec) / 1000000L;
        if (ms < present_interval_ms()) return;
    }
    last = now;
    have_last = 1;

    if (!priv->presented) {
        if (kitty_tiles_ready(priv)) {
            priv->ids_used = (uint32_t)priv->tiles_x *
                             (uint32_t)priv->tiles_y;
            present_tiles(priv, src, 1);
        } else {
            present_full(priv, src);
        }
        priv->presented = 1;
        return;
    }

    if (kitty_tiles_ready(priv)) {
        present_tiles(priv, src, 0);         /* idle frame → 0 bytes */
    } else if (memcmp(src, priv->prev,
                      (size_t)priv->stride * (size_t)priv->h) != 0) {
        present_full(priv, src);
    }
}

int output_resize(tgs_display *d, int width, int height)
{
    kitty_priv *priv = (kitty_priv *)d->backend_priv;

    if (!priv || width < 1 || height < 1) return -1;
    /* Compare against *our* grid, not d->width: term_resize_to() runs
     * be->set_size() first, which already moved d->width — comparing there
     * would always early-return and leave a stale grid over a fresh buffer. */
    if (width == priv->w && height == priv->h) return 0;

    /* The grid is gone from the host's point of view: free the old tile
     * ids, then force a complete first frame at the new geometry. */
    grid_resize(priv, width, height, width * 4);
    if (!priv->prev) return -1;

    /* If nobody superseded our buffer (no scene backend), it is still the
     * published one — reallocate it so the caller can keep painting into
     * d->buffer at the new size. When the scene backend owns d->buffer it
     * has already resized it via set_size; we only adopt the geometry. */
    if (d->buffer == priv->fb) {
        uint8_t *nfb = (uint8_t *)calloc(1, (size_t)priv->stride *
                                           (size_t)height);
        if (!nfb) return -1;
        free(priv->fb);
        priv->fb = nfb;
        d->buffer = nfb;
    }

    d->width = width;
    d->height = height;
    d->stride = priv->stride;
    return 0;
}

void output_cleanup(tgs_display *d)
{
    kitty_priv *priv = (kitty_priv *)d->backend_priv;

    if (!priv) return;
    /* Leave the alt screen — and take our images with us. */
    kitty_delete_tiles(priv);
    {
        const char rmcup[] = "\x1b[?1049l";
        (void)!write(STDOUT_FILENO, rmcup, sizeof(rmcup) - 1);
    }
    free(priv->fb);
    free(priv->prev);
    free(priv);
    d->backend_priv = NULL;
    /* d->buffer may be the scene backend's canvas — clearing the pointer is
     * all we do here; it is not ours to free. */
    d->buffer = NULL;
}
