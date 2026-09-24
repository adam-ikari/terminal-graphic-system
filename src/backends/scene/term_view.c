/*
 * term_view.c — the character base, painted through SDL2_ttf.
 *
 * Draws the tgs_term cell grid
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

/* Blend a glyph pixel over what the cell already holds. The view buffer is
 * filled opaque before every draw, so the result stays opaque — a terminal's
 * glyph is antialiased against its cell background, never composited later.
 * `a` is the glyph coverage (0..255).
 *
 * v/255 is read from a table filled with the very same C division: the
 * rounding is part of the pixel contract (a reciprocal approximation can
 * move an antialiased edge by one), while the division itself was the
 * blend's dominant per-pixel cost. Numerators are bounded — fr*a +
 * dr*(255-a) ≤ 255*a + 255*(255-a) = 65025 — so 65536 entries cover it. */
static uint8_t g_div255[65536];

static void div255_init(void)
{
    unsigned i;
    for (i = 0; i < sizeof(g_div255); i++) g_div255[i] = (uint8_t)(i / 255u);
}

static void blend_cp(term_view *tv, int x, int y, uint32_t fg, uint32_t a)
{
    uint32_t dst;
    unsigned fr, fgn, fb, dr, dg, db;

    if (x < 0 || y < 0 || x >= tv->w || y >= tv->h) return;
    dst = tv->px[(size_t)y * tv->w + x];
    fr = (fg >> 16) & 0xFF;
    fgn = (fg >> 8) & 0xFF;
    fb = fg & 0xFF;
    dr = (dst >> 16) & 0xFF;
    dg = (dst >> 8) & 0xFF;
    db = dst & 0xFF;
    tv->px[(size_t)y * tv->w + x] = 0xFF000000u |
        ((uint32_t)g_div255[fr * a + dr * (255 - a)] << 16) |
        ((uint32_t)g_div255[fgn * a + dg * (255 - a)] << 8) |
        ((uint32_t)g_div255[fb * a + db * (255 - a)]);
}

/* Glyph rasterisation cache. A full-grid redraw renders every non-space cell
 * through TTF_RenderUTF8_Blended — thousands of freetype rasterisations per
 * tick, and the view's dominant redraw cost. The render is deterministic for
 * a fixed font/text/colour, so each (codepoint, fg) is rasterised once and
 * its surface reused: the blend reads exactly the bytes a fresh render would
 * produce, only the rasterisation is saved. Open addressing; a full probe
 * evicts the hashed slot, but the live working set is a few dozen glyphs.
 * key 0 marks an empty slot and can never collide (cp >= 1 is guarded). */
#define GLYPH_SLOTS 1024
typedef struct { uint64_t key; SDL_Surface *s; } glyph_slot;
static glyph_slot g_glyphs[GLYPH_SLOTS];

static uint32_t glyph_hash(uint32_t cp, uint32_t fg)
{
    return (cp * 2654435761u) ^ (fg * 40503u);
}

static SDL_Surface *glyph_get(uint32_t cp, uint32_t fg)
{
    uint64_t key = ((uint64_t)cp << 24) | (fg & 0xFFFFFFu);
    uint32_t i = glyph_hash(cp, fg) & (GLYPH_SLOTS - 1);
    uint32_t steps;

    for (steps = 0; steps < GLYPH_SLOTS; steps++) {
        if (g_glyphs[i].key == 0) return NULL;
        if (g_glyphs[i].key == key) return g_glyphs[i].s;
        i = (i + 1) & (GLYPH_SLOTS - 1);
    }
    return NULL;
}

static void glyph_put(uint32_t cp, uint32_t fg, SDL_Surface *s)
{
    uint64_t key = ((uint64_t)cp << 24) | (fg & 0xFFFFFFu);
    uint32_t start = glyph_hash(cp, fg) & (GLYPH_SLOTS - 1);
    uint32_t i = start;
    uint32_t steps = 0;

    while (g_glyphs[i].key != 0 && g_glyphs[i].key != key &&
           steps++ < GLYPH_SLOTS)
        i = (i + 1) & (GLYPH_SLOTS - 1);
    if (g_glyphs[i].s && g_glyphs[i].s != s) SDL_FreeSurface(g_glyphs[i].s);
    g_glyphs[i].key = key;
    g_glyphs[i].s = s;
}

static void glyph_cache_clear(void)
{
    int i;
    for (i = 0; i < GLYPH_SLOTS; i++) {
        if (g_glyphs[i].s) SDL_FreeSurface(g_glyphs[i].s);
        g_glyphs[i].s = NULL;
        g_glyphs[i].key = 0;
    }
}

/* Real glyph entry: one codepoint, rasterised at most once per colour. */
static void blit_cp(term_view *tv, int px, int py, uint32_t cp, uint32_t fg)
{
    SDL_Surface *s;
    SDL_Color col;
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
    s = glyph_get(cp, fg);
    if (!s) {
        col.r = (fg >> 16) & 0xFF;
        col.g = (fg >> 8) & 0xFF;
        col.b = fg & 0xFF;
        col.a = 255;
        s = TTF_RenderUTF8_Blended(g_font, utf8, col);
        if (!s) return;
        glyph_put(cp, fg, s);
    }
    /* TTF_RenderUTF8_Blended yields a 32-bit RGBA surface — glyph color in
     * RGB, coverage in alpha. (This path used to check BytesPerPixel == 1
     * only: the glyph was rendered and then thrown away, so the character
     * grid changed while the canvas never did.) Taking the coverage and
     * blending fg over the cell is independent of the channel layout; the
     * 8-bit case is kept for builds where SDL_ttf quantizes. */
    if (s->format->BytesPerPixel == 4) {
        for (row = 0; row < s->h; row++) {
            const uint8_t *src = (const uint8_t *)s->pixels +
                                 (size_t)row * (size_t)s->pitch;
            for (colb = 0; colb < s->w; colb++) {
                uint32_t a = src[(size_t)colb * 4 + 3];
                if (a > 8) blend_cp(tv, px + colb, py + row, fg, a);
            }
        }
    } else if (s->format->BytesPerPixel == 1) {
        const uint8_t *pix = (const uint8_t *)s->pixels;
        for (row = 0; row < s->h; row++) {
            for (colb = 0; colb < s->w; colb++) {
                uint32_t a = pix[(size_t)row * s->pitch + colb];
                if (a > 8) blend_cp(tv, px + colb, py + row, fg, a);
            }
        }
    }
    /* The cache owns the surface now — no SDL_FreeSurface here. */
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
    div255_init();
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

/* The prefill covers exactly [0, w-1] x [0, h-1] = the whole buffer, so the
 * bounds-checked per-pixel rect() call per pixel (473k guarded calls a
 * redraw) buys nothing: a plain run of stores is the same bytes, ~10x
 * cheaper. DEF_BG's four bytes differ, so this cannot be a memset. */
static void fill_bg(term_view *tv)
{
    uint32_t *p = tv->px;
    uint32_t *end = p + (size_t)tv->w * (size_t)tv->h;

    while (p < end) *p++ = DEF_BG;
}

void tgs_term_view_draw(void *view, const tgs_term *t)
{
    term_view *tv = (term_view *)view;
    int r, c2;

    if (!tv || !t) return;
    fill_bg(tv);
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
    glyph_cache_clear();
    free(tv->px);
    free(tv);
}
