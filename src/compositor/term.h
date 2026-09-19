/*
 * TGS Terminal Emulator — the character base.
 *
 * A program's ordinary output goes here; TGS APC frames are demultiplexed
 * separately by the parser (parser.h). This module is the character grid and
 * its ANSI/VT parser — no rendering, no platform dependency, unit-testable alone.
 */
#ifndef TGS_TERM_H
#define TGS_TERM_H

#include <stdint.h>

/* Cell attributes the emulator can actually express.
 *
 * libvterm models bold, underline, italic, blink, reverse, conceal and strike —
 * but it has no faint/dim bit at all, and a 1-bit bitmap font has no italic or
 * blink to render. So what a program paints arrives here as these three, plus
 * conceal (which clears the cell). Everything else renders at normal intensity:
 * that is a stated contract, not an oversight. */
#define TGS_ATTR_BOLD      0x01
#define TGS_ATTR_UNDERLINE 0x02
#define TGS_ATTR_REVERSE   0x04

/* Default colour sentinel. A resolved colour is 0xFFRRGGBB; 0 means
 * "the terminal's default foreground/background". */
#define TGS_TERM_DEFAULT 0u

/* Cursor shape, as the program last asked for it (DECSCUSR). */
enum {
    TGS_CURSOR_BLOCK = 1,
    TGS_CURSOR_UNDERLINE,
    TGS_CURSOR_BAR
};

typedef struct {
    uint32_t cp;    /* Unicode codepoint (0 for an empty cell) */
    uint32_t fg;    /* 0xFFRRGGBB, or TGS_TERM_DEFAULT */
    uint32_t bg;
    uint8_t  attr;  /* TGS_ATTR_* */
} tgs_term_cell;

typedef struct tgs_term tgs_term;

/* Feed one key edge (the key space of docs/navigation.md §D.4) to the program.
 * The bytes are encoded for the mode the program has put the terminal in
 * (cursor-key application mode, keypad mode, ...) and leave through the reply
 * callback, exactly as a real terminal would send them. */
void tgs_term_key(tgs_term *t, int key, int mods);

/* Report a mouse action to the program, in cells (0-based). `pressed` is 1 for
 * a press, 0 for a release, -1 for motion. libvterm drops reports the program
 * has not asked for, so this is safe to call unconditionally. */
void tgs_term_mouse(tgs_term *t, int col, int row, int button, int pressed);

/* Tell the program the window gained or lost focus. libvterm stays quiet unless
 * the program asked for focus reporting (CSI ?1004h). */
void tgs_term_focus(tgs_term *t, int focused);

/* Paste text into the program. It goes through as text rather than keystrokes,
 * and a program that asked for bracketed paste (CSI ?2004h) receives it wrapped
 * so it can tell pasted text from typing. */
void tgs_term_paste(tgs_term *t, const char *text, int len);

/* Scrollback — the lines that have scrolled off the top.
 *
 * The viewport is the live screen at offset 0 and history at any larger offset;
 * new output drops back to the live screen, as a terminal does. History is
 * dropped rather than reflowed on resize. */
#define TGS_TERM_SCROLLBACK 1000

/* Move the viewport; positive looks further back. Returns the new offset. */
int tgs_term_scroll(tgs_term *t, int lines);
int tgs_term_scroll_offset(const tgs_term *t);

/* The cells of viewport row `row`, with the scroll offset applied — NULL when
 * the row is past everything that has been written. */
const tgs_term_cell *tgs_term_view_line(const tgs_term *t, int row);

/* Bytes the terminal must send back to the program (DSR replies etc.). */
typedef void (*tgs_term_reply_cb)(const char *bytes, int len, void *ud);

tgs_term *tgs_term_new(int cols, int rows);
void      tgs_term_free(tgs_term *t);
void      tgs_term_resize(tgs_term *t, int cols, int rows);
void      tgs_term_feed(tgs_term *t, const uint8_t *data, int len);
void      tgs_term_set_reply_cb(tgs_term *t, tgs_term_reply_cb cb, void *ud);

int tgs_term_cols(const tgs_term *t);
int tgs_term_rows(const tgs_term *t);
const tgs_term_cell *tgs_term_cells(const tgs_term *t);
int tgs_term_cx(const tgs_term *t);
int tgs_term_cy(const tgs_term *t);
int tgs_term_cursor_visible(const tgs_term *t);
int tgs_term_cursor_shape(const tgs_term *t);

/* 1 if the grid changed since the previous call; clears the flag. */
int tgs_term_take_dirty(tgs_term *t);

#endif /* TGS_TERM_H */
