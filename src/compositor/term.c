/*
 * TGS Terminal Emulator — cell grid + ANSI/VT parser.
 *
 * Scope: what a full-screen character program (shell, htop, vim) actually
 * emits — C0 controls, CSI cursor/erase/scroll/SGR, DEC private modes
 * (cursor visibility, alternate screen, autowrap), the alternate screen
 * buffer, UTF-8 input, and the handful of ESC single-shift/charset escapes
 * that must be swallowed rather than printed. Unknown sequences are consumed
 * whole and ignored, which is the terminal tradition that keeps new
 * sequences from corrupting old displays.
 */
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* xterm-style 16-colour palette (ARGB8888). */
static const uint32_t g_palette[16] = {
    0xFF000000u, 0xFFCD0000u, 0xFF00CD00u, 0xFFCDCD00u,
    0xFF0000EEu, 0xFFCD00CDu, 0xFF00CDCDu, 0xFFE5E5E5u,
    0xFF7F7F7Fu, 0xFFFF0000u, 0xFF00FF00u, 0xFFFFFF00u,
    0xFF5C5CFFu, 0xFFFF00FFu, 0xFF00FFFFu, 0xFFFFFFFFu,
};

enum { ST_GROUND = 0, ST_ESC, ST_ESC_INTER, ST_CSI, ST_OSC, ST_OSC_ESC };

#define MAX_PARAMS 16

struct tgs_term {
    int cols, rows;

    tgs_term_cell *main_cells;
    tgs_term_cell *alt_cells;   /* allocated on first use */
    tgs_term_cell *cells;       /* main or alt, whichever is active */
    int use_alt;

    int cx, cy;
    uint32_t cur_fg, cur_bg;
    uint8_t cur_attr;

    /* Saved cursor (DECSC / CSI s) */
    int sv_x, sv_y;
    uint32_t sv_fg, sv_bg;
    uint8_t sv_attr;

    int top, bot;               /* scroll region, 0-based inclusive */
    int cursor_visible;
    int autowrap;
    int wrap_pending;           /* the previous glyph filled the last column */
    int dirty;

    int state;
    int esc_inter;              /* '(', ')' — one more byte is charset data */

    /* CSI accumulation */
    int params[MAX_PARAMS];
    int nparams;
    int cur_param;
    int have_param;
    int priv;                   /* 0, or '?', '<', '>', '=' */

    /* UTF-8 accumulation */
    uint32_t utf_cp;
    int utf_left;

    tgs_term_reply_cb reply_cb;
    void *reply_ud;
};

/* ------------------------------------------------------------------ */
/* Grid helpers                                                        */
/* ------------------------------------------------------------------ */

static tgs_term_cell *cell_at(tgs_term *t, int x, int y)
{
    return &t->cells[(size_t)y * (size_t)t->cols + (size_t)x];
}

static void cells_blank(tgs_term_cell *c, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        c[i].cp = ' ';
        c[i].fg = TGS_TERM_DEFAULT;
        c[i].bg = TGS_TERM_DEFAULT;
        c[i].attr = 0;
    }
}

static uint32_t resolve_index(int idx, int is_bg)
{
    if (idx < 0) return is_bg ? TGS_TERM_DEFAULT : TGS_TERM_DEFAULT;
    if (idx < 16) return g_palette[idx];
    /* 256-colour cube / greyscale approximated by the nearest palette entry:
     * L0 keeps one palette; a truecolour path is a later concern. */
    return g_palette[((idx - 16) % 16)];
}

static void scroll_up(tgs_term *t, int n)
{
    int y, rows = t->bot - t->top + 1;
    if (n > rows) n = rows;
    for (y = t->top; y <= t->bot - n; y++) {
        memcpy(cell_at(t, 0, y), cell_at(t, 0, y + n),
               (size_t)t->cols * sizeof(tgs_term_cell));
    }
    for (y = t->bot - n + 1; y <= t->bot; y++) {
        cells_blank(cell_at(t, 0, y), (size_t)t->cols);
    }
    t->dirty = 1;
}

static void scroll_down(tgs_term *t, int n)
{
    int y, rows = t->bot - t->top + 1;
    if (n > rows) n = rows;
    for (y = t->bot; y >= t->top + n; y--) {
        memcpy(cell_at(t, 0, y), cell_at(t, 0, y - n),
               (size_t)t->cols * sizeof(tgs_term_cell));
    }
    for (y = t->top; y < t->top + n; y++) {
        cells_blank(cell_at(t, 0, y), (size_t)t->cols);
    }
    t->dirty = 1;
}

static void linefeed(tgs_term *t)
{
    t->wrap_pending = 0;
    if (t->cy == t->bot) {
        scroll_up(t, 1);
    } else if (t->cy < t->rows - 1) {
        t->cy++;
    }
    t->dirty = 1;
}

static void put_char(tgs_term *t, uint32_t cp)
{
    tgs_term_cell *c;

    if (t->wrap_pending) {
        t->wrap_pending = 0;
        if (t->autowrap) {
            t->cx = 0;
            if (t->cy == t->bot) scroll_up(t, 1);
            else if (t->cy < t->rows - 1) t->cy++;
        }
    }

    c = cell_at(t, t->cx, t->cy);
    c->cp = cp;
    c->fg = t->cur_fg;
    c->bg = t->cur_bg;
    c->attr = t->cur_attr;
    t->dirty = 1;

    if (t->cx + 1 >= t->cols) {
        if (t->autowrap) t->wrap_pending = 1;
        /* Autowrap off: the cursor sticks at the last column. */
    } else {
        t->cx++;
    }
}

/* ------------------------------------------------------------------ */
/* Erase / insert / delete                                             */
/* ------------------------------------------------------------------ */

static void erase_display(tgs_term *t, int mode)
{
    int y;
    switch (mode) {
    case 0: /* cursor to end */
        cells_blank(cell_at(t, t->cx, t->cy), (size_t)(t->cols - t->cx));
        for (y = t->cy + 1; y < t->rows; y++)
            cells_blank(cell_at(t, 0, y), (size_t)t->cols);
        break;
    case 1: /* start to cursor */
        for (y = 0; y < t->cy; y++)
            cells_blank(cell_at(t, 0, y), (size_t)t->cols);
        cells_blank(cell_at(t, 0, t->cy), (size_t)(t->cx + 1));
        break;
    case 2: /* whole screen */
    case 3:
        for (y = 0; y < t->rows; y++)
            cells_blank(cell_at(t, 0, y), (size_t)t->cols);
        break;
    default:
        return;
    }
    t->dirty = 1;
}

static void erase_line(tgs_term *t, int mode)
{
    switch (mode) {
    case 0: cells_blank(cell_at(t, t->cx, t->cy), (size_t)(t->cols - t->cx)); break;
    case 1: cells_blank(cell_at(t, 0, t->cy), (size_t)(t->cx + 1)); break;
    case 2: cells_blank(cell_at(t, 0, t->cy), (size_t)t->cols); break;
    default: return;
    }
    t->dirty = 1;
}

static void insert_lines(tgs_term *t, int n)
{
    int y, rows;
    if (t->cy < t->top || t->cy > t->bot) return;
    rows = t->bot - t->cy + 1;
    if (n > rows) n = rows;
    for (y = t->bot; y >= t->cy + n; y--) {
        memcpy(cell_at(t, 0, y), cell_at(t, 0, y - n),
               (size_t)t->cols * sizeof(tgs_term_cell));
    }
    for (y = t->cy; y < t->cy + n; y++)
        cells_blank(cell_at(t, 0, y), (size_t)t->cols);
    t->dirty = 1;
}

static void delete_lines(tgs_term *t, int n)
{
    int y, rows;
    if (t->cy < t->top || t->cy > t->bot) return;
    rows = t->bot - t->cy + 1;
    if (n > rows) n = rows;
    for (y = t->cy; y <= t->bot - n; y++) {
        memcpy(cell_at(t, 0, y), cell_at(t, 0, y + n),
               (size_t)t->cols * sizeof(tgs_term_cell));
    }
    for (y = t->bot - n + 1; y <= t->bot; y++)
        cells_blank(cell_at(t, 0, y), (size_t)t->cols);
    t->dirty = 1;
}

static void delete_chars(tgs_term *t, int n)
{
    int x;
    if (n > t->cols - t->cx) n = t->cols - t->cx;
    for (x = t->cx; x < t->cols - n; x++)
        *cell_at(t, x, t->cy) = *cell_at(t, x + n, t->cy);
    cells_blank(cell_at(t, t->cols - n, t->cy), (size_t)n);
    t->dirty = 1;
}

static void insert_chars(tgs_term *t, int n)
{
    int x;
    if (n > t->cols - t->cx) n = t->cols - t->cx;
    for (x = t->cols - 1; x >= t->cx + n; x--)
        *cell_at(t, x, t->cy) = *cell_at(t, x - n, t->cy);
    cells_blank(cell_at(t, t->cx, t->cy), (size_t)n);
    t->dirty = 1;
}

static void erase_chars(tgs_term *t, int n)
{
    if (n > t->cols - t->cx) n = t->cols - t->cx;
    cells_blank(cell_at(t, t->cx, t->cy), (size_t)n);
    t->dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Modes / SGR                                                         */
/* ------------------------------------------------------------------ */

static void set_mode(tgs_term *t, int priv, int set)
{
    /* Only DEC private modes (priv == '?') are tracked. */
    if (priv != '?') return;

    switch (t->params[0]) {
    case 1:  break;                     /* DECCKM (application cursor) — ignored */
    case 7:  t->autowrap = set; break;  /* DECAWM */
    case 25: t->cursor_visible = set; t->dirty = 1; break;
    case 1047:
    case 1049:
    case 47:
        if (set) {
            if (!t->use_alt) {
                if (!t->alt_cells) {
                    t->alt_cells = (tgs_term_cell *)malloc(
                        (size_t)t->cols * (size_t)t->rows * sizeof(tgs_term_cell));
                    if (!t->alt_cells) return;
                }
                cells_blank(t->alt_cells, (size_t)t->cols * (size_t)t->rows);
                t->use_alt = 1;
                t->cells = t->alt_cells;
                /* Save the primary cursor so leaving restores it. */
                t->sv_x = t->cx; t->sv_y = t->cy;
                t->sv_fg = t->cur_fg; t->sv_bg = t->cur_bg;
                t->sv_attr = t->cur_attr;
                t->cx = 0; t->cy = 0;
                t->dirty = 1;
            }
        } else if (t->use_alt) {
            t->use_alt = 0;
            t->cells = t->main_cells;
            t->cx = t->sv_x; t->cy = t->sv_y;
            t->cur_fg = t->sv_fg; t->cur_bg = t->sv_bg;
            t->cur_attr = t->sv_attr;
            t->wrap_pending = 0;
            t->dirty = 1;
        }
        break;
    default:
        break;
    }
}

static void sgr(tgs_term *t)
{
    int i;
    if (t->nparams == 0) {
        t->cur_fg = TGS_TERM_DEFAULT;
        t->cur_bg = TGS_TERM_DEFAULT;
        t->cur_attr = 0;
        return;
    }

    for (i = 0; i < t->nparams; i++) {
        int p = t->params[i];
        switch (p) {
        case 0:  t->cur_fg = TGS_TERM_DEFAULT; t->cur_bg = TGS_TERM_DEFAULT;
                 t->cur_attr = 0; break;
        case 1:  t->cur_attr |= TGS_ATTR_BOLD; break;
        case 2:  t->cur_attr |= TGS_ATTR_DIM; break;
        case 4:  t->cur_attr |= TGS_ATTR_UNDERLINE; break;
        case 7:  t->cur_attr |= TGS_ATTR_REVERSE; break;
        case 22: t->cur_attr &= (uint8_t)~(TGS_ATTR_BOLD | TGS_ATTR_DIM); break;
        case 24: t->cur_attr &= (uint8_t)~TGS_ATTR_UNDERLINE; break;
        case 27: t->cur_attr &= (uint8_t)~TGS_ATTR_REVERSE; break;
        case 39: t->cur_fg = TGS_TERM_DEFAULT; break;
        case 49: t->cur_bg = TGS_TERM_DEFAULT; break;
        case 38:
        case 48: {
            int bg = (p == 48);
            if (i + 1 < t->nparams && t->params[i + 1] == 5 && i + 2 < t->nparams) {
                uint32_t c = resolve_index(t->params[i + 2], bg);
                if (bg) t->cur_bg = c; else t->cur_fg = c;
                i += 2;
            } else if (i + 1 < t->nparams && t->params[i + 1] == 2 && i + 4 < t->nparams) {
                uint32_t c = 0xFF000000u
                    | ((uint32_t)(t->params[i + 2] & 0xFF) << 16)
                    | ((uint32_t)(t->params[i + 3] & 0xFF) << 8)
                    |  (uint32_t)(t->params[i + 4] & 0xFF);
                if (bg) t->cur_bg = c; else t->cur_fg = c;
                i += 4;
            } else {
                i = t->nparams; /* malformed — stop */
            }
            break;
        }
        default:
            if (p >= 30 && p <= 37)       t->cur_fg = g_palette[p - 30];
            else if (p >= 40 && p <= 47)  t->cur_bg = g_palette[p - 40];
            else if (p >= 90 && p <= 97)  t->cur_fg = g_palette[p - 90 + 8];
            else if (p >= 100 && p <= 107) t->cur_bg = g_palette[p - 100 + 8];
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sequence dispatch                                                   */
/* ------------------------------------------------------------------ */

static int param_or(tgs_term *t, int i, int dflt)
{
    if (i >= t->nparams || t->params[i] == 0) return dflt;
    return t->params[i];
}

static void csi_dispatch(tgs_term *t, int final)
{
    int n;

    switch (final) {
    case 'A': n = param_or(t, 0, 1); t->cy -= n; if (t->cy < t->top) t->cy = t->top;
              t->wrap_pending = 0; t->dirty = 1; break;
    case 'B': n = param_or(t, 0, 1); t->cy += n; if (t->cy > t->bot) t->cy = t->bot;
              t->wrap_pending = 0; t->dirty = 1; break;
    case 'C': n = param_or(t, 0, 1); t->cx += n; if (t->cx > t->cols - 1) t->cx = t->cols - 1;
              t->wrap_pending = 0; break;
    case 'D': n = param_or(t, 0, 1); t->cx -= n; if (t->cx < 0) t->cx = 0;
              t->wrap_pending = 0; break;
    case 'E': n = param_or(t, 0, 1); t->cy += n; if (t->cy > t->bot) t->cy = t->bot;
              t->cx = 0; t->wrap_pending = 0; t->dirty = 1; break;
    case 'F': n = param_or(t, 0, 1); t->cy -= n; if (t->cy < t->top) t->cy = t->top;
              t->cx = 0; t->wrap_pending = 0; t->dirty = 1; break;
    case 'G': n = param_or(t, 0, 1); t->cx = n - 1;
              if (t->cx < 0) t->cx = 0;
              if (t->cx > t->cols - 1) t->cx = t->cols - 1;
              t->wrap_pending = 0; break;
    case 'd': n = param_or(t, 0, 1); t->cy = n - 1;
              if (t->cy < 0) t->cy = 0;
              if (t->cy > t->rows - 1) t->cy = t->rows - 1;
              t->wrap_pending = 0; break;
    case 'H':
    case 'f': {
        int row = param_or(t, 0, 1), col = param_or(t, 1, 1);
        t->cy = row - 1; t->cx = col - 1;
        if (t->cy < 0) t->cy = 0;
        if (t->cy > t->rows - 1) t->cy = t->rows - 1;
        if (t->cx < 0) t->cx = 0;
        if (t->cx > t->cols - 1) t->cx = t->cols - 1;
        t->wrap_pending = 0;
        break;
    }
    case 'J': erase_display(t, t->nparams ? t->params[0] : 0); break;
    case 'K': erase_line(t, t->nparams ? t->params[0] : 0); break;
    case 'L': insert_lines(t, param_or(t, 0, 1)); break;
    case 'M': delete_lines(t, param_or(t, 0, 1)); break;
    case 'P': delete_chars(t, param_or(t, 0, 1)); break;
    case '@': insert_chars(t, param_or(t, 0, 1)); break;
    case 'X': erase_chars(t, param_or(t, 0, 1)); break;
    case 'S': scroll_up(t, param_or(t, 0, 1)); break;
    case 'T': scroll_down(t, param_or(t, 0, 1)); break;
    case 'r': {
        int top = param_or(t, 0, 1), bot = param_or(t, 1, t->rows);
        if (top < 1) top = 1;
        if (bot > t->rows) bot = t->rows;
        if (top < bot) { t->top = top - 1; t->bot = bot - 1; }
        t->cx = 0; t->cy = t->top;
        t->wrap_pending = 0;
        break;
    }
    case 'm': sgr(t); break;
    case 'h': set_mode(t, t->priv, 1); break;
    case 'l': set_mode(t, t->priv, 0); break;
    case 's': t->sv_x = t->cx; t->sv_y = t->cy;
              t->sv_fg = t->cur_fg; t->sv_bg = t->cur_bg;
              t->sv_attr = t->cur_attr; break;
    case 'u': t->cx = t->sv_x; t->cy = t->sv_y;
              t->cur_fg = t->sv_fg; t->cur_bg = t->sv_bg;
              t->cur_attr = t->sv_attr; t->wrap_pending = 0; break;
    case 'n': { /* DSR — answer 6n (cursor position) so programs can query. */
        if (t->reply_cb && t->nparams > 0 && t->params[0] == 6) {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dR", t->cy + 1, t->cx + 1);
            if (len > 0) t->reply_cb(buf, len, t->reply_ud);
        }
        break;
    }
    default:
        break; /* unknown — swallowed with its parameters */
    }
}

static void esc_dispatch(tgs_term *t, int final)
{
    switch (final) {
    case '7': t->sv_x = t->cx; t->sv_y = t->cy;
              t->sv_fg = t->cur_fg; t->sv_bg = t->cur_bg;
              t->sv_attr = t->cur_attr; break;
    case '8': t->cx = t->sv_x; t->cy = t->sv_y;
              t->cur_fg = t->sv_fg; t->cur_bg = t->sv_bg;
              t->cur_attr = t->sv_attr; t->wrap_pending = 0; t->dirty = 1; break;
    case 'D': linefeed(t); break;                       /* IND */
    case 'E': t->cx = 0; linefeed(t); break;            /* NEL */
    case 'M':                                           /* RI — reverse index */
        t->wrap_pending = 0;
        if (t->cy == t->top) scroll_down(t, 1);
        else if (t->cy > 0) t->cy--;
        t->dirty = 1;
        break;
    case 'H': break;                                    /* HTS — tab stop */
    case 'c':                                           /* RIS — full reset */
        cells_blank(t->cells, (size_t)t->cols * (size_t)t->rows);
        t->cx = t->cy = 0;
        t->top = 0; t->bot = t->rows - 1;
        t->cur_fg = TGS_TERM_DEFAULT; t->cur_bg = TGS_TERM_DEFAULT;
        t->cur_attr = 0; t->autowrap = 1; t->cursor_visible = 1;
        t->wrap_pending = 0; t->dirty = 1;
        break;
    default:
        break;
    }
}

static void csi_reset(tgs_term *t)
{
    t->nparams = 0;
    t->cur_param = 0;
    t->have_param = 0;
    t->priv = 0;
}

static void csi_param_push(tgs_term *t)
{
    if (t->nparams < MAX_PARAMS) t->params[t->nparams] = t->cur_param;
    t->nparams++;
    t->cur_param = 0;
    t->have_param = 0;
}

/* ------------------------------------------------------------------ */
/* Byte feed                                                           */
/* ------------------------------------------------------------------ */

static void feed_byte(tgs_term *t, uint8_t b)
{
    switch (t->state) {
    case ST_GROUND:
        if (b == 0x1B) { t->state = ST_ESC; return; }
        if (b < 0x20) {
            switch (b) {
            case 0x08: if (t->cx > 0) t->cx--; t->wrap_pending = 0; break; /* BS */
            case 0x09: {                                                   /* HT */
                int next = ((t->cx / 8) + 1) * 8;
                if (next > t->cols - 1) next = t->cols - 1;
                t->cx = next; t->wrap_pending = 0;
                break;
            }
            case 0x0A: case 0x0B: case 0x0C: linefeed(t); break;           /* LF VT FF */
            case 0x0D: t->cx = 0; t->wrap_pending = 0; break;              /* CR */
            default: break; /* BEL, SO, SI, … — ignored */
            }
            return;
        }
        if (b < 0x80) { put_char(t, b); return; }
        /* UTF-8 lead or continuation */
        if (t->utf_left == 0) {
            if ((b & 0xE0) == 0xC0) { t->utf_cp = b & 0x1Fu; t->utf_left = 1; }
            else if ((b & 0xF0) == 0xE0) { t->utf_cp = b & 0x0Fu; t->utf_left = 2; }
            else if ((b & 0xF8) == 0xF0) { t->utf_cp = b & 0x07u; t->utf_left = 3; }
            /* else: stray continuation byte — drop */
        } else {
            t->utf_cp = (t->utf_cp << 6) | (uint32_t)(b & 0x3Fu);
            if (--t->utf_left == 0) put_char(t, t->utf_cp);
        }
        return;

    case ST_ESC:
        if (b == '[') { t->state = ST_CSI; csi_reset(t); return; }
        if (b == ']') { t->state = ST_OSC; return; }
        if (b == '(' || b == ')' || b == '*' || b == '+') {
            t->esc_inter = b; t->state = ST_ESC_INTER; return;
        }
        t->state = ST_GROUND;
        esc_dispatch(t, b);
        return;

    case ST_ESC_INTER:          /* one byte of charset designation — swallow */
        (void)t->esc_inter;
        t->state = ST_GROUND;
        return;

    case ST_CSI:
        if (b >= '0' && b <= '9') {
            t->cur_param = t->cur_param * 10 + (b - '0');
            t->have_param = 1;
            return;
        }
        if (b == ';') { csi_param_push(t); return; }
        if (b == '?' || b == '<' || b == '>' || b == '=') {
            if (!t->have_param && t->nparams == 0) { t->priv = b; return; }
        }
        if (b >= 0x20 && b <= 0x2F) return;   /* intermediate — swallow */
        if (b >= 0x40 && b <= 0x7E) {
            if (t->have_param || t->nparams > 0) csi_param_push(t);
            t->state = ST_GROUND;
            csi_dispatch(t, b);
            return;
        }
        /* Anything else aborts the sequence. */
        t->state = ST_GROUND;
        return;

    case ST_OSC:
        if (b == 0x07) { t->state = ST_GROUND; return; }   /* BEL terminates */
        if (b == 0x1B) { t->state = ST_OSC_ESC; return; }
        return;

    case ST_OSC_ESC:
        t->state = (b == '\\') ? ST_GROUND : ST_OSC;
        return;

    default:
        t->state = ST_GROUND;
        return;
    }
}

void tgs_term_feed(tgs_term *t, const uint8_t *data, int len)
{
    int i;
    if (!t || !data || len <= 0) return;
    for (i = 0; i < len; i++) feed_byte(t, data[i]);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static tgs_term_cell *alloc_cells(int cols, int rows)
{
    tgs_term_cell *c = (tgs_term_cell *)malloc(
        (size_t)cols * (size_t)rows * sizeof(tgs_term_cell));
    if (c) cells_blank(c, (size_t)cols * (size_t)rows);
    return c;
}

tgs_term *tgs_term_new(int cols, int rows)
{
    tgs_term *t;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    t = (tgs_term *)calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->cols = cols;
    t->rows = rows;
    t->main_cells = alloc_cells(cols, rows);
    if (!t->main_cells) { free(t); return NULL; }
    t->cells = t->main_cells;

    t->top = 0;
    t->bot = rows - 1;
    t->cur_fg = TGS_TERM_DEFAULT;
    t->cur_bg = TGS_TERM_DEFAULT;
    t->cursor_visible = 1;
    t->autowrap = 1;
    t->dirty = 1;
    return t;
}

void tgs_term_free(tgs_term *t)
{
    if (!t) return;
    free(t->main_cells);
    free(t->alt_cells);
    free(t);
}

void tgs_term_resize(tgs_term *t, int cols, int rows)
{
    tgs_term_cell *nc;
    int y, w;

    if (!t || cols < 1 || rows < 1) return;
    if (cols == t->cols && rows == t->rows) return;

    nc = alloc_cells(cols, rows);
    if (!nc) return;

    w = (cols < t->cols) ? cols : t->cols;
    for (y = 0; y < rows && y < t->rows; y++) {
        memcpy(&nc[(size_t)y * (size_t)cols], cell_at(t, 0, y),
               (size_t)w * sizeof(tgs_term_cell));
    }

    /* The primary buffer is reallocated; the alternate screen is dropped and
     * rebuilt on demand (resizing during an alt-screen program is rare). */
    if (t->use_alt) { t->use_alt = 0; t->cells = t->main_cells; }
    free(t->main_cells);
    t->main_cells = nc;
    t->cells = nc;
    free(t->alt_cells);
    t->alt_cells = NULL;

    t->cols = cols;
    t->rows = rows;
    t->top = 0;
    t->bot = rows - 1;
    if (t->cx >= cols) t->cx = cols - 1;
    if (t->cy >= rows) t->cy = rows - 1;
    t->wrap_pending = 0;
    t->dirty = 1;
}

void tgs_term_set_reply_cb(tgs_term *t, tgs_term_reply_cb cb, void *ud)
{
    if (!t) return;
    t->reply_cb = cb;
    t->reply_ud = ud;
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

/* ------------------------------------------------------------------ */
/* Key encoding                                                        */
/* ------------------------------------------------------------------ */

#define TGS_KEY_BASE 1000   /* 1000..1005 = arrows, Home, End */

int tgs_term_key_bytes(int key, int mods, char *out, int cap)
{
    static const char *nav[] = {
        "\x1b[D", /* 1000 LEFT  */
        "\x1b[C", /* 1001 RIGHT */
        "\x1b[A", /* 1002 UP    */
        "\x1b[B", /* 1003 DOWN  */
        "\x1b[H", /* 1004 HOME  */
        "\x1b[F", /* 1005 END   */
    };

    if (!out || cap <= 0) return 0;

    if (key >= TGS_KEY_BASE && key < TGS_KEY_BASE + (int)(sizeof(nav) / sizeof(nav[0]))) {
        const char *s = nav[key - TGS_KEY_BASE];
        int n = (int)strlen(s);
        if (n > cap) n = cap;
        memcpy(out, s, (size_t)n);
        return n;
    }

    if (key > 0 && key < 128) {
        char c = (char)key;
        /* Ctrl+letter is the classic control byte; input backends report the
         * unshifted letter with the CTRL modifier set. */
        if (mods & 0x02) {
            if (key >= 'a' && key <= 'z') c = (char)(key - 'a' + 1);
            else if (key >= 'A' && key <= 'Z') c = (char)(key - 'A' + 1);
        }
        out[0] = c;
        return 1;
    }
    return 0;
}
