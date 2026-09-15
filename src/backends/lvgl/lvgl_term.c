/*
 * Terminal view — draws a tgs_term cell grid into an LVGL canvas.
 *
 * The bitmap font in use (unscii-16) covers ASCII only, and every TUI draws
 * its frames from the box-drawing and block ranges. Rather than pull in a
 * larger font, those glyphs are synthesised as rectangles: exact at any cell
 * size, and it keeps the font tiny.
 */
#include "lvgl_term.h"

#include <stdlib.h>
#include <string.h>

extern const lv_font_t lv_font_unscii_16;

#define CELL_W 8
#define CELL_H 16

#define DEF_FG 0xFFE0E0E0u
#define DEF_BG 0xFF101014u

typedef struct {
    uint8_t *buf;   /* ARGB8888, w*h */
    int w, h;
} term_view;

int tgs_term_view_cell_w(void) { return CELL_W; }
int tgs_term_view_cell_h(void) { return CELL_H; }

static uint32_t rgb_of(uint32_t c) { return c & 0x00FFFFFFu; }

static uint32_t scale(uint32_t c, int num, int den)
{
    uint32_t r = (rgb_of(c) >> 16) & 0xFFu;
    uint32_t g = (rgb_of(c) >> 8) & 0xFFu;
    uint32_t b = rgb_of(c) & 0xFFu;
    r = r * (uint32_t)num / (uint32_t)den;
    g = g * (uint32_t)num / (uint32_t)den;
    b = b * (uint32_t)num / (uint32_t)den;
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
    int cx = x + CELL_W / 2, cy = y + CELL_H / 2;
    int x0 = x, x1 = x + CELL_W - 1, y0 = y, y1 = y + CELL_H - 1;

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
    int x0 = x, x1 = x + CELL_W - 1, y0 = y, y1 = y + CELL_H - 1;

    switch (cp) {
    case 0x2588: rect(L, x0, y0, x1, y1, fg); return 1;          /* █ */
    case 0x2580: rect(L, x0, y0, x1, y + CELL_H / 2 - 1, fg); return 1;  /* ▀ */
    case 0x2584: rect(L, x0, y + CELL_H / 2, x1, y1, fg); return 1;      /* ▄ */
    case 0x258C: rect(L, x0, y0, x + CELL_W / 2 - 1, y1, fg); return 1;  /* ▌ */
    case 0x2590: rect(L, x + CELL_W / 2, y0, x1, y1, fg); return 1;      /* ▐ */
    case 0x2591: rect(L, x0, y0, x1, y1, scale(fg, 1, 4)); return 1;     /* ░ */
    case 0x2592: rect(L, x0, y0, x1, y1, scale(fg, 1, 2)); return 1;     /* ▒ */
    case 0x2593: rect(L, x0, y0, x1, y1, scale(fg, 3, 4)); return 1;     /* ▓ */
    default:
        /* Left eighths (U+258F..U+2589) — htop draws its meters with these. */
        if (cp >= 0x2589 && cp <= 0x258F) {
            int eighths = 0x258F - cp + 1;   /* 1..7 */
            int w = CELL_W * eighths / 8;
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

static void draw_char(lv_layer_t *L, int x, int y, uint32_t cp, uint32_t fg)
{
    lv_draw_letter_dsc_t d;
    lv_point_t p;

    lv_draw_letter_dsc_init(&d);
    d.unicode = cp;
    d.color = lv_color_hex(rgb_of(fg));
    d.font = &lv_font_unscii_16;

    p.x = x;
    p.y = y;
    lv_draw_letter(L, &d, &p);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

lv_obj_t *tgs_term_view_create(int cols, int rows)
{
    term_view *tv;
    lv_obj_t *canvas;
    int w = cols * CELL_W;
    int h = rows * CELL_H;

    if (cols < 1 || rows < 1) return NULL;

    tv = (term_view *)lv_malloc(sizeof(*tv));
    if (!tv) return NULL;
    tv->w = w;
    tv->h = h;
    tv->buf = (uint8_t *)lv_malloc((size_t)w * (size_t)h * 4u);
    if (!tv->buf) { lv_free(tv); return NULL; }
    memset(tv->buf, 0, (size_t)w * (size_t)h * 4u);

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
    const tgs_term_cell *cells;
    lv_layer_t layer;
    int cols, rows, cx, cy;

    if (!view || !t) return;
    tv = (term_view *)lv_obj_get_user_data(view);
    cells = tgs_term_cells(t);
    if (!tv || !cells) return;

    cols = tgs_term_cols(t);
    rows = tgs_term_rows(t);

    lv_canvas_init_layer(view, &layer);

    /* The base: fill the whole canvas with the terminal background first, so a
     * cell that uses the default background never lets the display's own theme
     * show through. */
    rect(&layer, 0, 0, tv->w - 1, tv->h - 1, DEF_BG);

    for (cy = 0; cy < rows; cy++) {
        for (cx = 0; cx < cols; cx++) {
            const tgs_term_cell *c = &cells[(size_t)cy * (size_t)cols + (size_t)cx];
            int px = cx * CELL_W;
            int py = cy * CELL_H;
            uint32_t fg = c->fg ? c->fg : DEF_FG;
            uint32_t bg = c->bg ? c->bg : DEF_BG;
            int reversed = (c->attr & TGS_ATTR_REVERSE) != 0;

            if (reversed) { uint32_t s = fg; fg = bg; bg = s; }
            if (c->attr & TGS_ATTR_BOLD) fg = scale(fg, 5, 4);
            if (c->attr & TGS_ATTR_DIM)  fg = scale(fg, 2, 3);

            if (bg != DEF_BG || reversed)
                rect(&layer, px, py, px + CELL_W - 1, py + CELL_H - 1, bg);

            if (c->cp == 0 || c->cp == ' ') continue;

            if (draw_block(&layer, px, py, c->cp, fg, bg)) continue;
            if (c->cp >= 0x2500 && c->cp <= 0x256C) { draw_box(&layer, px, py, c->cp, fg); continue; }

            draw_char(&layer, px, py, c->cp, fg);
        }
    }

    /* Cursor: a block over the cell, drawn last so it is never overpainted. */
    if (tgs_term_cursor_visible(t)) {
        int px = tgs_term_cx(t) * CELL_W;
        int py = tgs_term_cy(t) * CELL_H;
        if (px >= 0 && py >= 0 && px < tv->w && py < tv->h)
            rect(&layer, px, py + CELL_H - 2, px + CELL_W - 1, py + CELL_H - 1, DEF_FG);
    }

    lv_canvas_finish_layer(view, &layer);
    lv_obj_invalidate(view);
}
