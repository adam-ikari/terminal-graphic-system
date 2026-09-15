/*
 * TGS terminal emulator — the character base, driven by libvterm.
 *
 * The emulation is not reimplemented here. It is libvterm, the C99 emulator
 * core that existing terminals and editors already use, so what a program sees
 * is what it would see on an existing terminal: the same sequence handling, the
 * same screen model, the same key encoding for the current mode. This file only
 * adapts libvterm's screen into the flat cell grid the renderer wants, and
 * routes libvterm's output (device replies, encoded keys) back to the program.
 */
#include "term.h"

#include <stdlib.h>
#include <string.h>

#include "vterm.h"

struct tgs_term {
    VTerm       *vt;
    VTermScreen *screen;

    int cols, rows;
    tgs_term_cell *cells;        /* snapshot of the screen */

    int cx, cy;
    int cursor_visible;

    int dirty;                   /* repaint wanted */
    int snapshot_dirty;          /* the cell snapshot is stale */

    tgs_term_reply_cb reply_cb;
    void *reply_ud;
};

/* ------------------------------------------------------------------ */
/* libvterm callbacks                                                  */
/* ------------------------------------------------------------------ */

static void output_cb(const char *s, size_t len, void *user)
{
    tgs_term *t = (tgs_term *)user;
    if (t->reply_cb && len > 0) t->reply_cb(s, (int)len, t->reply_ud);
}

static int on_damage(VTermRect rect, void *user)
{
    tgs_term *t = (tgs_term *)user;
    (void)rect;
    t->dirty = 1;
    t->snapshot_dirty = 1;
    return 1;
}

static int on_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    tgs_term *t = (tgs_term *)user;
    (void)oldpos;
    t->cx = pos.col;
    t->cy = pos.row;
    t->cursor_visible = visible;
    t->dirty = 1;   /* the cursor is drawn, so a move is a repaint */
    return 1;
}

static int on_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    tgs_term *t = (tgs_term *)user;
    if (prop == VTERM_PROP_CURSORVISIBLE) {
        t->cursor_visible = val->boolean;
        t->dirty = 1;
    }
    return 1;
}

static int on_moverect(VTermRect dest, VTermRect src, void *user)
{
    (void)dest; (void)src; (void)user;
    return 1;
}

static int on_bell(void *user)
{
    (void)user;
    return 1;
}

static int on_resize(int rows, int cols, void *user)
{
    (void)rows; (void)cols; (void)user;
    return 1;
}

static int on_sb_pushline(int cols, const VTermScreenCell *cells, void *user)
{
    (void)cols; (void)cells; (void)user;
    return 0;   /* no scrollback yet */
}

static int on_sb_popline(int cols, VTermScreenCell *cells, void *user)
{
    (void)cols; (void)cells; (void)user;
    return 0;
}

static int on_sb_clear(void *user)
{
    (void)user;
    return 0;
}

static const VTermScreenCallbacks g_screen_cbs = {
    .damage      = on_damage,
    .moverect    = on_moverect,
    .movecursor  = on_movecursor,
    .settermprop = on_settermprop,
    .bell        = on_bell,
    .resize      = on_resize,
    .sb_pushline = on_sb_pushline,
    .sb_popline  = on_sb_popline,
    .sb_clear    = on_sb_clear,
};

/* ------------------------------------------------------------------ */
/* Screen → cell snapshot                                              */
/* ------------------------------------------------------------------ */

static uint32_t color_argb(tgs_term *t, const VTermColor *c, int is_bg)
{
    VTermColor col = *c;

    if (is_bg ? VTERM_COLOR_IS_DEFAULT_BG(&col) : VTERM_COLOR_IS_DEFAULT_FG(&col))
        return TGS_TERM_DEFAULT;
    if (col.type & VTERM_COLOR_DEFAULT_MASK) return TGS_TERM_DEFAULT;

    vterm_screen_convert_color_to_rgb(t->screen, &col);
    return 0xFF000000u
         | ((uint32_t)col.rgb.red   << 16)
         | ((uint32_t)col.rgb.green << 8)
         |  (uint32_t)col.rgb.blue;
}

static void rebuild_snapshot(tgs_term *t)
{
    VTermPos pos;
    VTermScreenCell sc;
    tgs_term_cell *out;
    uint8_t attr;
    int x, y;

    for (y = 0; y < t->rows; y++) {
        for (x = 0; x < t->cols; x++) {
            pos.row = y;
            pos.col = x;
            out = &t->cells[(size_t)y * (size_t)t->cols + (size_t)x];

            memset(&sc, 0, sizeof(sc));
            (void)vterm_screen_get_cell(t->screen, pos, &sc);

            if (sc.chars[0] == 0) {
                /* Untouched cell: the terminal's own default colours. */
                out->cp = ' ';
                out->fg = TGS_TERM_DEFAULT;
                out->bg = TGS_TERM_DEFAULT;
                out->attr = 0;
                continue;
            }

            attr = 0;
            if (sc.attrs.bold)      attr |= TGS_ATTR_BOLD;
            if (sc.attrs.underline) attr |= TGS_ATTR_UNDERLINE;
            if (sc.attrs.reverse)   attr |= TGS_ATTR_REVERSE;

            out->cp = sc.attrs.conceal ? ' ' : sc.chars[0];
            out->fg = color_argb(t, &sc.fg, 0);
            out->bg = color_argb(t, &sc.bg, 1);
            out->attr = attr;
        }
    }

    t->snapshot_dirty = 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

tgs_term *tgs_term_new(int cols, int rows)
{
    tgs_term *t;

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    t = (tgs_term *)calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->cols = cols;
    t->rows = rows;
    t->cells = (tgs_term_cell *)malloc(
        (size_t)cols * (size_t)rows * sizeof(tgs_term_cell));
    if (!t->cells) { free(t); return NULL; }
    memset(t->cells, 0, (size_t)cols * (size_t)rows * sizeof(tgs_term_cell));

    t->vt = vterm_new(rows, cols);
    if (!t->vt) { free(t->cells); free(t); return NULL; }

    vterm_set_utf8(t->vt, 1);
    vterm_output_set_callback(t->vt, output_cb, t);

    t->screen = vterm_obtain_screen(t->vt);
    vterm_screen_set_callbacks(t->screen, &g_screen_cbs, t);
    vterm_screen_enable_altscreen(t->screen, 1);
    vterm_screen_set_damage_merge(t->screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(t->screen, 1);

    t->cursor_visible = 1;
    t->dirty = 1;
    t->snapshot_dirty = 1;
    rebuild_snapshot(t);
    return t;
}

void tgs_term_free(tgs_term *t)
{
    if (!t) return;
    if (t->vt) vterm_free(t->vt);
    free(t->cells);
    free(t);
}

void tgs_term_resize(tgs_term *t, int cols, int rows)
{
    tgs_term_cell *nc;

    if (!t || cols < 1 || rows < 1) return;
    if (cols == t->cols && rows == t->rows) return;

    nc = (tgs_term_cell *)malloc(
        (size_t)cols * (size_t)rows * sizeof(tgs_term_cell));
    if (!nc) return;

    free(t->cells);
    t->cells = nc;
    t->cols = cols;
    t->rows = rows;

    vterm_set_size(t->vt, rows, cols);
    rebuild_snapshot(t);
    t->dirty = 1;
}

void tgs_term_feed(tgs_term *t, const uint8_t *data, int len)
{
    if (!t || !data || len <= 0) return;
    vterm_input_write(t->vt, (const char *)data, (size_t)len);
    /* libvterm accumulates damage and only delivers it here — without this the
     * screen is written but nothing ever tells us it changed. */
    vterm_screen_flush_damage(t->screen);
    if (t->snapshot_dirty) rebuild_snapshot(t);
}

void tgs_term_set_reply_cb(tgs_term *t, tgs_term_reply_cb cb, void *ud)
{
    if (!t) return;
    t->reply_cb = cb;
    t->reply_ud = ud;
}

void tgs_term_key(tgs_term *t, int key, int mods)
{
    VTermModifier m = VTERM_MOD_NONE;

    if (!t) return;
    if (mods & 0x01) m |= VTERM_MOD_SHIFT;
    if (mods & 0x02) m |= VTERM_MOD_CTRL;
    if (mods & 0x04) m |= VTERM_MOD_ALT;

    switch (key) {
    case 13:   vterm_keyboard_key(t->vt, VTERM_KEY_ENTER, m);     return;
    case 9:    vterm_keyboard_key(t->vt, VTERM_KEY_TAB, m);       return;
    case 8:    vterm_keyboard_key(t->vt, VTERM_KEY_BACKSPACE, m); return;
    case 27:   vterm_keyboard_key(t->vt, VTERM_KEY_ESCAPE, m);    return;
    case 127:  vterm_keyboard_key(t->vt, VTERM_KEY_DEL, m);       return;
    case 1000: vterm_keyboard_key(t->vt, VTERM_KEY_LEFT, m);      return;
    case 1001: vterm_keyboard_key(t->vt, VTERM_KEY_RIGHT, m);     return;
    case 1002: vterm_keyboard_key(t->vt, VTERM_KEY_UP, m);        return;
    case 1003: vterm_keyboard_key(t->vt, VTERM_KEY_DOWN, m);      return;
    case 1004: vterm_keyboard_key(t->vt, VTERM_KEY_HOME, m);      return;
    case 1005: vterm_keyboard_key(t->vt, VTERM_KEY_END, m);       return;
    default:   break;
    }

    if (key > 0 && key < 0x110000u)
        vterm_keyboard_unichar(t->vt, (uint32_t)key, m);
}

int tgs_term_cols(const tgs_term *t) { return t ? t->cols : 0; }
int tgs_term_rows(const tgs_term *t) { return t ? t->rows : 0; }
const tgs_term_cell *tgs_term_cells(const tgs_term *t) { return t ? t->cells : NULL; }
int tgs_term_cx(const tgs_term *t) { return t ? t->cx : 0; }
int tgs_term_cy(const tgs_term *t) { return t ? t->cy : 0; }
int tgs_term_cursor_visible(const tgs_term *t) { return t ? t->cursor_visible : 0; }

int tgs_term_take_dirty(tgs_term *t)
{
    int d;
    if (!t) return 0;
    d = t->dirty;
    t->dirty = 0;
    return d;
}
