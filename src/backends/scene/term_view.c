/*
 * term_view.c — the character base, painted through SDL2_ttf.
 *
 * Replaces the LVGL canvas view (lvgl_term.c): draws the tgs_term cell grid
 * into an ARGB8888 buffer that the compositor blits under the widget scene.
 * Reference implementation: one monospace face, box-drawing/block glyphs
 * synthesised as rects — exact at any cell size and font-coverage-free.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

#include "../../compositor/term.h"

#define CELL_W 8
#define CELL_H 16
#define DEF_FG 0xFFCCCCCCu
#define DEF_BG 0xFF101014u

typedef struct {
    int cols, rows, w, h;
    uint32_t *px;                    /* ARGB8888, w*h */
} term_view;

static TTF_Font *g_font;
static int g_ttf;

static void ensure_font(void)
{
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL,
    };
    int i;

    if (!g_ttf) { TTF_Init(); g_ttf = 1; }
    if (g_font) return;
    for (i = 0; paths[i]; i++) {
        g_font = TTF_OpenFont(paths[i], CELL_H - 3);
        if (g_font) return;
    }
}

int tgs_term_view_cell_w(void) { return CELL_W; }
int tgs_term_view_cell_h(void) { return CELL_H; }

static void px_set(term_view *tv, int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= tv->w || y >= tv->h) return;
    tv->px[(size_t)y * tv->w + x] = c;
}

static void rect(term_view *tv, int x0, int y0, int x1, int y1, uint32_t c)
{
    int x, y;

    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            px_set(tv, x, y, c);
}

/* Real glyph entry: renders one codepoint. */
static void blit_cp(term_view *tv, int px, int py, uint32_t cp, uint32_t fg)
{
    SDL_Surface *s;
    SDL_Color col;
    uint8_t *pix;
    char utf8[5];
    int n, row, colb;

    if (!g_font || cp == 0 || cp == ' ') return;
    n = 0;
    if (cp < 0x80) {
        utf8[n++] = (char)cp;
    } else if (cp < 0x800) {
        utf8[n++] = (char)(0xC0 | (cp >> 6));
        utf8[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        utf8[n++] = (char)(0xE0 | (cp >> 12));
        utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[n++] = (char)(0x80 | (cp & 0x3F));
    }
    utf8[n] = 0;
    col.r = (fg >> 16) & 0xFF;
    col.g = (fg >> 8) & 0xFF;
    col.b = fg & 0xFF;
    col.a = 255;
    s = TTF_RenderUTF8_Blended(g_font, utf8, col);
    if (!s) return;
    if (s->format->BytesPerPixel == 1) {
        pix = (uint8_t *)s->pixels;
        for (row = 0; row < s->h; row++) {
            for (colb = 0; colb < s->w; colb++) {
                uint32_t a = pix[(size_t)row * s->pitch + colb];
                if (a > 8) px_set(tv, px + colb, py + row, (fg & 0xFFFFFF) | (a << 24));
            }
        }
    }
    SDL_FreeSurface(s);
}

/* Box-drawing synthesis: line chars draw as rects (exact at any cell size). */
static void draw_box_glyph(term_view *tv, int px, int py, uint32_t cp, uint32_t fg)
{
    int mx = px + CELL_W / 2;
    int my = py + CELL_H / 2;

    switch (cp) {
    case 0x2500: rect(tv, px, my, px + CELL_W - 1, my, fg); break;           /* ─ */
    case 0x2502: rect(tv, mx, py, mx, py + CELL_H - 1, fg); break;           /* │ */
    case 0x250C: rect(tv, mx, my, px + CELL_W - 1, my, fg);
                 rect(tv, mx, my, mx, py + CELL_H - 1, fg); break;           /* ┌ */
    case 0x2510: rect(tv, px, my, mx, my, fg);
                 rect(tv, mx, my, mx, py + CELL_H - 1, fg); break;           /* ┐ */
    case 0x2514: rect(tv, mx, py, mx, my, fg);
                 rect(tv, mx, my, px + CELL_W - 1, my, fg); break;           /* └ */
    case 0x2518: rect(tv, mx, py, mx, my, fg);
                 rect(tv, px, my, mx, my, fg); break;                        /* ┘ */
    default: return;                   /* unhandled: caller falls back */
    }
}

static void draw_cell(term_view *tv, int col, int row, const tgs_term_cell *c)
{
    int px = col * CELL_W, py = row * CELL_H;
    uint32_t fg = c->fg ? c->fg : DEF_FG;
    uint32_t bg = c->bg ? c->bg : DEF_BG;
    uint32_t drawn;

    if (c->attr & TGS_ATTR_REVERSE) {
        uint32_t t = fg; fg = bg; bg = t;
    }
    if (c->attr & TGS_ATTR_BOLD) fg = ((fg & 0xFEFEFE) >> 1) + (fg & 0xFEFEFE) > 0xFFFFFF
                                      ? 0xFFFFFF : fg + ((fg & 0xFEFEFE) >> 1);
    if (bg != DEF_BG)
        rect(tv, px, py, px + CELL_W - 1, py + CELL_H - 1, bg);

    if (c->cp == 0 || c->cp == ' ') return;
    drawn = 0;
    if (c->cp >= 0x2500 && c->cp <= 0x256C) {
        draw_box_glyph(tv, px, py, c->cp, fg);
        drawn = 1;                     /* boxes never fall through to the font */
    }
    if (!drawn) blit_cp(tv, px, py + 1, c->cp, fg);
    if (c->attr & TGS_ATTR_UNDERLINE)
        rect(tv, px, py + CELL_H - 2, px + CELL_W - 1, py + CELL_H - 2, fg);
}

void *tgs_term_view_create(int cols, int rows)
{
    term_view *tv;

    if (cols < 1 || rows < 1) return NULL;
    ensure_font();
    tv = (term_view *)calloc(1, sizeof(*tv));
    if (!tv) return NULL;
    tv->cols = cols;
    tv->rows = rows;
    tv->w = cols * CELL_W;
    tv->h = rows * CELL_H;
    tv->px = (uint32_t *)malloc((size_t)tv->w * tv->h * 4);
    if (!tv->px) { free(tv); return NULL; }
    return tv;
}

void tgs_term_view_draw(void *view, const tgs_term *t)
{
    term_view *tv = (term_view *)view;
    int r, c2;

    if (!tv || !t) return;
    rect(tv, 0, 0, tv->w - 1, tv->h - 1, DEF_BG);
    for (r = 0; r < tv->rows; r++) {
        const tgs_term_cell *line = tgs_term_view_line(t, r);
        if (!line) continue;
        for (c2 = 0; c2 < tv->cols; c2++)
            draw_cell(tv, c2, r, &line[c2]);
    }
}

const uint32_t *tgs_term_view_pixels(void *view)
{
    term_view *tv = (term_view *)view;
    return tv ? tv->px : NULL;
}

void tgs_term_view_destroy(void *view)
{
    term_view *tv = (term_view *)view;

    if (!tv) return;
    free(tv->px);
    free(tv);
}
