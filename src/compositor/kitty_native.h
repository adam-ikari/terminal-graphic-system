/*
 * kitty-native graphics frames — the TGS ⊃ kitty superset face, receive side.
 *
 * The parser (parser.h) splits the shared APC channel: "G1;..." is a TGS
 * frame, every other "G..." payload lands here (spec §8.1 — accepted, never
 * fed to the TGS decoder). This module is the reference receiver that turns
 * those frames into pixels on the canvas (spec §8.3).
 *
 * Implemented subset:  a=t/T/p/d (transmit / transmit+display / display
 * stored / delete), f=100|32|24 (PNG via stb_image, raw RGBA, raw RGB),
 * chunked base64 assembly (m=1..0), i= image ids (64 slots, auto-id when
 * absent), placement = cursor-cell snapshot + X/Y in-cell pixel offset,
 * c/r display size in cells (nearest-neighbour), x/y/w/h display crop,
 * z ordering, and the i=-keynowledged reply contract (q=0/1/2).
 *
 * Compositing position is a load-bearing decision (recorded in the brain):
 * tgs_kitty_draw() writes into the term-view pixel buffer after the glyph
 * pass — images sit over the character base and under the scene elements,
 * the same z-order rule opaque windows already follow.
 *
 * Known gaps, documented rather than implemented: U=1 unicode-placeholder
 * virtual placements, animation (a=a/f=/c=), zlib-compressed payloads,
 * multiple placements per image (p>0), the cursor movement policy C=0
 * (the vterm cursor never moves — a stated divergence), placement
 * scroll-coupling (a placement stays at its snapshot cell while text
 * scrolls under it), BMP/GIF formats, partial raw transmits (x/y on a
 * data-carrying raw frame), file/shm targets (t=/o=).
 */
#ifndef TGS_KITTY_NATIVE_H
#define TGS_KITTY_NATIVE_H

#include <stdint.h>
#include "term.h"

/* Feed one kitty-native APC payload. `payload` starts at 'G' (the byte after
 * ESC _), `len` its length up to — not including — the String Terminator.
 * `term` is the character base (cursor snapshot for placement; may be NULL),
 * cell_w/cell_h its cell geometry (placement math and c/r display sizes),
 * reply_fd the pty the acknowledgements go to (<0 disables replies). */
void tgs_kitty_feed(const uint8_t *payload, int len, const tgs_term *term,
                    int cell_w, int cell_h, int reply_fd);

/* 1 if the visible image layer changed since the previous call; clears. */
int  tgs_kitty_take_dirty(void);

/* Force the next base repaint to re-blit the images (view rebuilt, etc.). */
void tgs_kitty_mark_dirty(void);

/* Blit every visible placement into the term-view pixel buffer — called
 * right after tgs_term_view_draw(), before the scene render picks the
 * underlay up. px_w/px_h bound the buffer (clipping). */
void tgs_kitty_draw(uint32_t *px, int px_w, int px_h);

/* Drop all images, placements and assembly state (tests, shutdown). */
void tgs_kitty_reset(void);

#endif /* TGS_KITTY_NATIVE_H */
