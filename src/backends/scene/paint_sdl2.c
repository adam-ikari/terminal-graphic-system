/*
 * paint_sdl2.c — SDL2 paint port (software raster into the scene fb).
 *
 * Draws boxes with a hand-rolled rasterizer (rects, rounded corners via
 * corner span tests, 1px border) directly into the published ARGB8888
 * buffer, and text via SDL2_ttf over a small font cache keyed by pixel
 * size. No SDL_Renderer here: the scene fb IS the SDL texture source,
 * presented by output_present() like every other backend.
 *
 * Pixels are 0xAARRGGBB in memory as bytes R,G,B,A (little-endian), which
 * matches the published-buffer byte order (BGRA in memory).
 */
#include "tgs_scene.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t *fb;
    int w, h;
    int clip_x, clip_y, clip_w, clip_h;   /* active clip; w|h==0 = full */
    TTF_Font *fonts[SCENE_FONT_SIZES];    /* lazily opened per pixel size */
    int font_px[SCENE_FONT_SIZES];
} sdl2_paint;

/* DejaVu Sans ships on every dev box and embeds on the target. */
static const char *FONT_PATHS[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    NULL,
};

static TTF_Font *font_for(sdl2_paint *pp, int px)
{
    int slot = -1, i;

    for (i = 0; i < SCENE_FONT_SIZES; i++) {
        if (pp->fonts[i] && pp->font_px[i] == px) return pp->fonts[i];
        if (!pp->fonts[i] && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;               /* evict; 4 sizes cover the UI */
    for (i = 0; FONT_PATHS[i]; i++) {
        TTF_Font *f = TTF_OpenFont(FONT_PATHS[i], px);
        if (f) {
            if (pp->fonts[slot]) TTF_CloseFont(pp->fonts[slot]);
            pp->fonts[slot] = f;
            pp->font_px[slot] = px;
            TTF_SetFontStyle(f, TTF_STYLE_NORMAL);
            return f;
        }
    }
    return NULL;
}

static void px_set(sdl2_paint *pp, int x, int y, uint32_t c)
{
    if (x < pp->clip_x || y < pp->clip_y) return;
    if (pp->clip_w > 0 && (x >= pp->clip_x + pp->clip_w)) return;
    if (pp->clip_h > 0 && (y >= pp->clip_y + pp->clip_h)) return;
    if (x < 0 || y < 0 || x >= pp->w || y >= pp->h) return;
    pp->fb[(size_t)y * pp->w + x] = c;
}

static void fill_row(sdl2_paint *pp, int x0, int x1, int y, uint32_t c)
{
    int x;

    if (y < pp->clip_y) return;
    if (pp->clip_h > 0 && y >= pp->clip_y + pp->clip_h) return;
    if (x0 < pp->clip_x) x0 = pp->clip_x;
    if (pp->clip_w > 0 && x1 > pp->clip_x + pp->clip_w) x1 = pp->clip_x + pp->clip_w;
    if (x0 < 0) x0 = 0;
    if (x1 > pp->w) x1 = pp->w;
    for (x = x0; x < x1; x++) pp->fb[(size_t)y * pp->w + x] = c;
}

/* Rounded-corner coverage: is (dx,dy) inside the corner arc? dx,dy measured
 * from the box edge, r = radius. A pixel is outside iff it sits in the
 * corner square and its distance from the arc center exceeds r. */
static int corner_inside(int dx, int dy, int r)
{
    int cx, cy;

    if (r <= 0) return 1;
    if (dx >= r && dy >= r) return 1;     /* not in a corner square */
    cx = dx < r ? r - dx : 0;
    cy = dy < r ? r - dy : 0;
    (void)cx; (void)cy;
    /* distance test against the arc center placed (r,r) inside the corner */
    {
        int ax = dx - r + (dx < r ? r : 0);
        int ay = dy - r + (dy < r ? r : 0);
        int qx = dx < r ? r - dx : 0;     /* offset from corner origin */
        int qy = dy < r ? r - dy : 0;
        int ex = dx - (dx < r ? 0 : 0);
        int er2, ex2, ey2;
        (void)ax; (void)ay; (void)qx; (void)qy; (void)ex;
        ex2 = (dx < r) ? (r - dx) * (r - dx) : 0;
        ey2 = (dy < r) ? (r - dy) * (r - dy) : 0;
        er2 = ex2 + ey2;
        return er2 <= r * r;
    }
}

static void paint_box(tgs_paint *p, int x, int y, int w, int h, int radius,
                      uint32_t fill, int has_fill,
                      uint32_t border, int border_w)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;
    int r = radius;
    int iy, ix;

    if (!pp || w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (has_fill) {
        for (iy = 0; iy < h; iy++) {
            int dy = iy < r ? r - iy : (iy >= h - r ? iy - (h - r - 1) : 0);
            int x0 = x, x1 = x + w;
            if (dy > 0) {
                /* shrink the row spans by the corner cutout */
                for (ix = 0; ix < r; ix++) {
                    if (!corner_inside(ix, dy - 1 < 0 ? 0 : dy - 1, r) &&
                        !corner_inside(ix, iy < h / 2 ? iy : h - 1 - iy, r))
                        break;
                }
                x0 = x + ix;
                x1 = x + w - ix;
            }
            fill_row(pp, x0, x1, y + iy, fill);
        }
    }

    if (border_w > 0) {
        int t = border_w;
        /* top and bottom edges */
        for (ix = 0; ix < w; ix++) {
            for (iy = 0; iy < t; iy++) {
                if (corner_inside(ix < r ? r - ix : 0,
                                  iy < r ? r - iy : 0, r) ||
                    (ix >= r && ix < w - r))
                    px_set(pp, x + ix, y + iy, border);
                if (corner_inside(ix < r ? r - ix : 0,
                                  iy < r ? r - iy : 0, r) ||
                    (ix >= r && ix < w - r))
                    px_set(pp, x + ix, y + h - 1 - iy, border);
            }
        }
        /* left and right edges */
        for (iy = 0; iy < h; iy++) {
            for (ix = 0; ix < t; ix++) {
                if (corner_inside(ix < r ? r - ix : 0,
                                  iy < r ? r - iy : 0, r) ||
                    (iy >= r && iy < h - r))
                    px_set(pp, x + ix, y + iy, border);
                if (corner_inside(ix < r ? r - ix : 0,
                                  iy < r ? r - iy : 0, r) ||
                    (iy >= r && iy < h - r))
                    px_set(pp, x + w - 1 - ix, y + iy, border);
            }
        }
    }
}

static void paint_text(tgs_paint *p, int x, int y, int w, int h,
                       const char *s, int font_size, uint32_t color, int align)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;
    TTF_Font *f;
    SDL_Color col;
    SDL_Surface *surf;
    int tw, th, tx, ty, row, colb;
    uint8_t *pix;
    uint32_t rgb = color & 0x00FFFFFF;

    if (!pp || !s || !s[0]) return;
    if (font_size <= 0) font_size = 14;
    f = font_for(pp, font_size);
    if (!f) return;

    col.r = (color >> 16) & 0xFF;
    col.g = (color >> 8) & 0xFF;
    col.b = color & 0xFF;
    col.a = 255;
    surf = TTF_RenderUTF8_Blended(f, s, col);
    if (!surf) return;

    tw = surf->w; th = surf->h;
    tx = x;
    if (align == 1) tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    ty = y + (h - th) / 2;
    if (ty < y) ty = y;

    pix = (uint8_t *)surf->pixels;
    if (surf->format->BytesPerPixel == 1) {
        for (row = 0; row < th; row++) {
            for (colb = 0; colb < tw; colb++) {
                uint32_t a = pix[(size_t)row * surf->pitch + colb];
                if (a > 8) px_set(pp, tx + colb, ty + row,
                                  rgb | ((uint32_t)a << 24));
            }
        }
    } else if (surf->format->BytesPerPixel == 4) {
        /* Blended surfaces ship as ARGB on this SDL2_ttf build: the alpha
         * byte carries coverage; composite over what is already there. */
        for (row = 0; row < th; row++) {
            const uint32_t *sp = (const uint32_t *)(pix +
                (size_t)row * surf->pitch);
            for (colb = 0; colb < tw; colb++) {
                uint32_t a = (sp[colb] >> 24) & 0xFF;
                if (a < 8) continue;
                int dx = tx + colb, dy = ty + row;
                if (dx < 0 || dy < 0 || dx >= pp->w || dy >= pp->h) continue;
                if (a >= 0xF8) {
                    px_set(pp, dx, dy, rgb | 0xFF000000u);
                } else {
                    uint32_t *dst = &pp->fb[(size_t)dy * pp->w + dx];
                    uint32_t dr = *dst & 0xFF, dg = (*dst >> 8) & 0xFF;
                    uint32_t db = (*dst >> 16) & 0xFF;
                    uint32_t r = rgb & 0xFF, g = (rgb >> 8) & 0xFF;
                    uint32_t b = (rgb >> 16) & 0xFF;
                    uint32_t nr = (r * a + dr * (255 - a)) / 255;
                    uint32_t ng = (g * a + dg * (255 - a)) / 255;
                    uint32_t nb = (b * a + db * (255 - a)) / 255;
                    *dst = 0xFF000000u | (nb << 16) | (ng << 8) | nr;
                }
            }
        }
    }
    SDL_FreeSurface(surf);
}

static int paint_measure(tgs_paint *p, const char *s, int font_size,
                         int *w, int *h)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;
    TTF_Font *f;
    int tw, th;

    if (!pp || !s) return -1;
    if (font_size <= 0) font_size = 14;
    f = font_for(pp, font_size);
    if (!f) return -1;
    if (TTF_SizeUTF8(f, s, &tw, &th) != 0) return -1;
    if (w) *w = tw;
    if (h) *h = th;
    return 0;
}

static void paint_underlay(tgs_paint *p, const uint32_t *px, int w, int h)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;
    size_t n, i;

    if (!pp) return;
    if (!px || w <= 0 || h <= 0) {
        memset(pp->fb, 0, (size_t)pp->w * (size_t)pp->h * 4u);
        return;
    }
    n = (size_t)w * (size_t)h;
    if (w == pp->w && h == pp->h) {
        memcpy(pp->fb, px, n * 4u);
        return;
    }
    /* Different size: top-left copy, zero the rest (reference behavior). */
    for (i = 0; i < (size_t)h && i < (size_t)pp->h; i++) {
        size_t cw = ((size_t)w < (size_t)pp->w ? (size_t)w : (size_t)pp->w) * 4u;
        memcpy(pp->fb + (size_t)i * pp->w, px + (size_t)i * w, cw);
        if (cw < (size_t)pp->w * 4u)
            memset(pp->fb + (size_t)i * pp->w + cw / 4u, 0,
                   (size_t)pp->w * 4u - cw);
    }
    for (; i < (size_t)pp->h; i++)
        memset(pp->fb + (size_t)i * pp->w, 0, (size_t)pp->w * 4u);
}

static void paint_clip(tgs_paint *p, int x, int y, int w, int h)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;

    pp->clip_x = x; pp->clip_y = y;
    if (w <= 0 || h <= 0) { pp->clip_w = 0; pp->clip_h = 0; return; }
    pp->clip_w = w; pp->clip_h = h;
}

static void paint_deinit(tgs_paint *p)
{
    sdl2_paint *pp = (sdl2_paint *)p->priv;
    int i;

    if (!pp) return;
    for (i = 0; i < SCENE_FONT_SIZES; i++) {
        if (pp->fonts[i]) TTF_CloseFont(pp->fonts[i]);
    }
    free(pp);
    p->priv = NULL;
}

void paint_sdl2_init(tgs_paint *p, uint32_t *fb, int w, int h)
{
    sdl2_paint *pp = (sdl2_paint *)calloc(1, sizeof(*pp));
    static int ttf_ready;

    if (!ttf_ready) { TTF_Init(); ttf_ready = 1; }
    memset(p, 0, sizeof(*p));
    if (!pp) return;
    pp->fb = fb;
    pp->w = w;
    pp->h = h;
    p->priv = pp;
    p->box = paint_box;
    p->text = paint_text;
    p->measure = paint_measure;
    p->underlay = paint_underlay;
    p->clip = paint_clip;
    p->deinit = paint_deinit;
}
