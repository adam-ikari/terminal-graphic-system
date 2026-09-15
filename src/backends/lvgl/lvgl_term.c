/*
 * Terminal view — draws a tgs_term cell grid into an LVGL canvas.
 *
 * The bitmap font in use (unscii-8) covers ASCII only, and every TUI draws its
 * frames from the box-drawing and block ranges. Rather than pull in a larger
 * font, those glyphs are synthesised as rectangles: exact at any cell size, and
 * it keeps the font tiny.
 */
#include "lvgl_term.h"

#include <stdlib.h>
#include <string.h>

extern const lv_font_t lv_font_unscii_8;

/* The cell is what the font actually advances, read from the font rather than
 * assumed: guessing a cell size is how every glyph ends up drawn across its
 * neighbour. unscii-8 is a true bitmap terminal font, which keeps a normal
 * terminal's column count (100+ at 800 px) instead of the ~50 a double-width
 * font gives. */
static int g_cell_w;
static int g_cell_h;

static void ensure_cell_size(void)
{
    if (g_cell_w > 0) return;
    g_cell_w = (int)lv_font_get_glyph_width(&lv_font_unscii_8, 'M', 'M');
    g_cell_h = (int)lv_font_get_line_height(&lv_font_unscii_8);
    if (g_cell_w < 1) g_cell_w = 8;
    if (g_cell_h < 1) g_cell_h = 9;
}

int tgs_term_view_cell_w(void) { ensure_cell_size(); return g_cell_w; }
int tgs_term_view_cell_h(void) { ensure_cell_size(); return g_cell_h; }

#define DEF_FG 0xFFE0E0E0u
#define DEF_BG 0xFF101014u

typedef struct {
    uint8_t *buf;   /* ARGB8888, w*h */
    int w, h;
} term_view;

static uint32_t rgb_of(uint32_t c) { return c & 0x00FFFFFFu; }

static uint32_t scale(uint32_t c, int num, int den)
{
    uint32_t r = (rgb_of(c) >> 16) & 0xFFu;
    uint32_t g = (rgb_of(c) >> 8) & 0xFFu;
    uint32_t b = rgb_of(c) & 0xFFu;
    /* Brightening must not wrap: 224 * 5 / 4 is 280, and a truncated 24 is
     * blacker than the colour it was meant to lift. */
    r = r * (uint32_t)num / (uint32_t)den; if (r > 255u) r = 255u;
    g = g * (uint32_t)num / (uint32_t)den; if (g > 255u) g = 255u;
    b = b * (uint32_t)num / (uint32_t)den; if (b > 255u) b = 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void rect(lv_layer_t *L, int x0, int y0, int x1, int y1, uint32_t argb)
{
    lv_draw_rect_dsc_t d;
    lv_area_t a;
    if (x1 < x0 || y1 < y0) return;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(rgb_of(argb));
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    d.shadow_width = 0;
    a.x1 = x0; a.y1 = y0; a.x2 = x1; a.y2 = y1;
    lv_draw_rect(L, &d, &a);
}

/* ------------------------------------------------------------------ */
/* Box drawing                                                         */
/* ------------------------------------------------------------------ */

/* Which cell edges the glyph joins: up/down/left/right, and whether the
 * stroke is doubled. Everything in U+2500..U+256C is a combination of these. */
static int box_segments(uint32_t cp, int *up, int *down, int *left, int *right, int *dbl)
{
    *dbl = 0;
    switch (cp) {
    case 0x2500: *up=0; *down=0; *left=1; *right=1; return 1;   /* ─ */
    case 0x2502: *up=1; *down=1; *left=0; *right=0; return 1;   /* │ */
    case 0x250C: *up=1; *down=0; *left=0; *right=1; return 1;   /* ┌ */
    case 0x2510: *up=1; *down=0; *left=1; *right=0; return 1;   /* ┐ */
    case 0x2514: *up=0; *down=1; *left=0; *right=1; return 1;   /* └ */
    case 0x2518: *up=0; *down=1; *left=1; *right=0; return 1;   /* ┘ */
    case 0x251C: *up=1; *down=1; *left=0; *right=1; return 1;   /* ├ */
    case 0x2524: *up=1; *down=1; *left=1; *right=0; return 1;   /* ┤ */
    case 0x252C: *up=0; *down=1; *left=1; *right=1; return 1;   /* ┬ */
    case 0x2534: *up=1; *down=0; *left=1; *right=1; return 1;   /* ┴ */
    case 0x253C: *up=1; *down=1; *left=1; *right=1; return 1;   /* ┼ */
    case 0x2550: *up=0; *down=0; *left=1; *right=1; *dbl=1; return 1;  /* ═ */
    case 0x2551: *up=1; *down=1; *left=0; *right=0; *dbl=1; return 1;  /* ║ */
    case 0x2554: *up=1; *down=0; *left=0; *right=1; *dbl=1; return 1;  /* ╔ */
    case 0x2557: *up=1; *down=0; *left=1; *right=0; *dbl=1; return 1;  /* ╗ */
    case 0x255A: *up=0; *down=1; *left=0; *right=1; *dbl=1; return 1;  /* ╚ */
    case 0x255D: *up=0; *down=1; *left=1; *right=0; *dbl=1; return 1;  /* ╝ */
    case 0x2560: *up=1; *down=1; *left=0; *right=1; *dbl=1; return 1;  /* ╠ */
    case 0x2563: *up=1; *down=1; *left=1; *right=0; *dbl=1; return 1;  /* ╣ */
    case 0x2566: *up=0; *down=1; *left=1; *right=1; *dbl=1; return 1;  /* ╦ */
    case 0x2569: *up=1; *down=0; *left=1; *right=1; *dbl=1; return 1;  /* ╩ */
    case 0x256C: *up=1; *down=1; *left=1; *right=1; *dbl=1; return 1;  /* ╬ */
    default: return 0;
    }
}

static void draw_box(lv_layer_t *L, int x, int y, uint32_t cp, uint32_t fg)
{
    int up, down, left, right, dbl;
    int cx = x + g_cell_w / 2, cy = y + g_cell_h / 2;
    int x0 = x, x1 = x + g_cell_w - 1, y0 = y, y1 = y + g_cell_h - 1;

    if (!box_segments(cp, &up, &down, &left, &right, &dbl)) return;

    if (dbl) {
        if (left)  { rect(L, x0, cy - 2, cx, cy - 2, fg); rect(L, x0, cy + 1, cx, cy + 1, fg); }
        if (right) { rect(L, cx, cy - 2, x1, cy - 2, fg); rect(L, cx, cy + 1, x1, cy + 1, fg); }
        if (up)    { rect(L, cx - 2, y0, cx - 2, cy, fg); rect(L, cx + 1, y0, cx + 1, cy, fg); }
        if (down)  { rect(L, cx - 2, cy, cx - 2, y1, fg); rect(L, cx + 1, cy, cx + 1, y1, fg); }
    } else {
        if (left)  rect(L, x0, cy - 1, cx, cy, fg);
        if (right) rect(L, cx, cy - 1, x1, cy, fg);
        if (up)    rect(L, cx - 1, y0, cx, cy, fg);
        if (down)  rect(L, cx - 1, cy, cx, y1, fg);
    }
}

/* ------------------------------------------------------------------ */
/* Block elements                                                      */
/* ------------------------------------------------------------------ */

static int draw_block(lv_layer_t *L, int x, int y, uint32_t cp, uint32_t fg, uint32_t bg)
{
    int x0 = x, x1 = x + g_cell_w - 1, y0 = y, y1 = y + g_cell_h - 1;

    switch (cp) {
    case 0x2588: rect(L, x0, y0, x1, y1, fg); return 1;          /* █ */
    case 0x2580: rect(L, x0, y0, x1, y + g_cell_h / 2 - 1, fg); return 1;  /* ▀ */
    case 0x2584: rect(L, x0, y + g_cell_h / 2, x1, y1, fg); return 1;      /* ▄ */
    case 0x258C: rect(L, x0, y0, x + g_cell_w / 2 - 1, y1, fg); return 1;  /* ▌ */
    case 0x2590: rect(L, x + g_cell_w / 2, y0, x1, y1, fg); return 1;      /* ▐ */
    case 0x2591: rect(L, x0, y0, x1, y1, scale(fg, 1, 4)); return 1;     /* ░ */
    case 0x2592: rect(L, x0, y0, x1, y1, scale(fg, 1, 2)); return 1;     /* ▒ */
    case 0x2593: rect(L, x0, y0, x1, y1, scale(fg, 3, 4)); return 1;     /* ▓ */
    default:
        /* Left eighths (U+258F..U+2589) — htop draws its meters with these. */
        if (cp >= 0x2589 && cp <= 0x258F) {
            int eighths = 0x258F - cp + 1;   /* 1..7 */
            int w = g_cell_w * eighths / 8;
            (void)bg;
            if (w > 0) rect(L, x0, y0, x0 + w - 1, y1, fg);
            return 1;
        }
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

/*
 * lv_draw_letter anchors a glyph on a pivot at
 * (adv_w / 2, line_height - base_line) and the rasteriser then shifts the glyph
 * by -pivot. Passing a cell's top-left therefore lands every glyph half a cell
 * to the left and a whole line up — which is exactly how a terminal covering
 * the screen ends up one row out of step. Pre-compensate by the same pivot.
 */
static void draw_char(lv_layer_t *L, int x, int y, uint32_t cp, uint32_t fg)
{
    lv_draw_letter_dsc_t d;
    lv_point_t p;

    lv_draw_letter_dsc_init(&d);
    d.unicode = cp;
    d.color = lv_color_hex(rgb_of(fg));
    d.font = &lv_font_unscii_8;

    p.x = x + (int32_t)(lv_font_get_glyph_width(&lv_font_unscii_8, 'M', 'M') / 2);
    p.y = y + lv_font_get_line_height(&lv_font_unscii_8) - lv_font_unscii_8.base_line;
    lv_draw_letter(L, &d, &p);
}

/* ------------------------------------------------------------------ */
/* Cells                                                               */
/* ------------------------------------------------------------------ */

/* Draw one cell. `invert` paints it reversed — which is exactly what a block
 * cursor is: the cell itself, swapped, so the character under it stays legible
 * and a blank cursor cell is still visible. */
static void draw_cell(lv_layer_t *L, const tgs_term_cell *c, int px, int py, int invert)
{
    uint32_t fg = c->fg ? c->fg : DEF_FG;
    uint32_t bg = c->bg ? c->bg : DEF_BG;
    int swapped = invert || (c->attr & TGS_ATTR_REVERSE) != 0;

    if (swapped) { uint32_t s = fg; fg = bg; bg = s; }
    if (c->attr & TGS_ATTR_BOLD) fg = scale(fg, 5, 4);

    if (bg != DEF_BG || swapped)
        rect(L, px, py, px + g_cell_w - 1, py + g_cell_h - 1, bg);

    if (c->cp == 0 || c->cp == ' ') return;

    if (draw_block(L, px, py, c->cp, fg, bg)) return;
    if (c->cp >= 0x2500 && c->cp <= 0x256C) { draw_box(L, px, py, c->cp, fg); return; }

    draw_char(L, px, py, c->cp, fg);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

lv_obj_t *tgs_term_view_create(int cols, int rows)
{
    term_view *tv;
    lv_obj_t *canvas;
    int w, h;

    if (cols < 1 || rows < 1) return NULL;
    ensure_cell_size();
    w = cols * g_cell_w;
    h = rows * g_cell_h;

    tv = (term_view *)lv_malloc(sizeof(*tv));
    if (!tv) return NULL;
    tv->w = w;
    tv->h = h;
    tv->buf = (uint8_t *)lv_malloc((size_t)w * (size_t)h * 4u);
    if (!tv->buf) { lv_free(tv); return NULL; }
    memset(tv->buf, 0, (size_t)w * (size_t)h * 4u);

    /* A whole number of cells rarely fills the window, and the margin left over
     * belongs to the terminal, not to the theme (which would show as a bright
     * band under the last row). */
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(DEF_BG & 0x00FFFFFFu), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    }

    canvas = lv_canvas_create(lv_screen_active());
    if (!canvas) { lv_free(tv->buf); lv_free(tv); return NULL; }

    lv_canvas_set_buffer(canvas, tv->buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_clickable(canvas, false);
    lv_obj_set_user_data(canvas, tv);
    return canvas;
}

void tgs_term_view_draw(lv_obj_t *view, const tgs_term *t)
{
    term_view *tv;
    lv_layer_t layer;
    int cols, rows, cx, cy;

    if (!view || !t) return;
    ensure_cell_size();
    tv = (term_view *)lv_obj_get_user_data(view);
    if (!tv) return;

    cols = tgs_term_cols(t);
    rows = tgs_term_rows(t);

    lv_canvas_init_layer(view, &layer);

    /* The base: fill the whole canvas with the terminal background first, so a
     * cell that uses the default background never lets the display's own theme
     * show through. */
    rect(&layer, 0, 0, tv->w - 1, tv->h - 1, DEF_BG);

    for (cy = 0; cy < rows; cy++) {
        /* History first, then the live screen: one accessor covers both, so a
         * scrolled viewport needs no separate path. */
        const tgs_term_cell *line = tgs_term_view_line(t, cy);
        if (!line) continue;
        for (cx = 0; cx < cols; cx++)
            draw_cell(&layer, &line[cx], cx * g_cell_w, cy * g_cell_h, 0);
    }

    /* Cursor, drawn last so it is never overpainted. A block cursor is the cell
     * painted in reverse — that is what a terminal's cursor actually is, and it
     * keeps the character under it readable. */
    if (tgs_term_scroll_offset(t) == 0 && tgs_term_cursor_visible(t)) {
        int ccx = tgs_term_cx(t);
        int ccy = tgs_term_cy(t);
        if (ccx >= 0 && ccy >= 0 && ccx < cols && ccy < rows) {
            int px = ccx * g_cell_w;
            int py = ccy * g_cell_h;
            switch (tgs_term_cursor_shape(t)) {
            case TGS_CURSOR_UNDERLINE:
                rect(&layer, px, py + g_cell_h - 2, px + g_cell_w - 1, py + g_cell_h - 1, DEF_FG);
                break;
            case TGS_CURSOR_BAR:
                rect(&layer, px, py, px + 1, py + g_cell_h - 1, DEF_FG);
                break;
            case TGS_CURSOR_BLOCK:
            default:
                draw_cell(&layer, tgs_term_view_line(t, ccy), px, py, 1);
                break;
            }
        }
    }

    /* Cursor: a block over the cell, drawn last so it is never overpainted. */
    if (tgs_term_cursor_visible(t)) {
        int px = tgs_term_cx(t) * g_cell_w;
        int py = tgs_term_cy(t) * g_cell_h;
        if (px >= 0 && py >= 0 && px < tv->w && py < tv->h)
            rect(&layer, px, py + g_cell_h - 2, px + g_cell_w - 1, py + g_cell_h - 1, DEF_FG);
    }

    lv_canvas_finish_layer(view, &layer);
    lv_obj_invalidate(view);
}

void tgs_term_view_destroy(lv_obj_t *view)
{
    term_view *tv;

    if (!view) return;
    tv = (term_view *)lv_obj_get_user_data(view);
    if (tv) {
        lv_free(tv->buf);
        lv_free(tv);
    }
    lv_obj_delete(view);
}
