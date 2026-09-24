/*
 * kitty_encode.h — kitty graphics protocol encoder (shared).
 */
#ifndef KITTY_ENCODE_H
#define KITTY_ENCODE_H

#include <stdint.h>
#include <unistd.h>

/* Where a transmitted image lands on the host terminal. kitty places an
 * image at the *cell* under the cursor, with pixel offsets that must be
 * smaller than the cell — so a pixel-exact placement is (cell, offset),
 * both derived from the pixel position and the host cell size. */
typedef struct {
    uint32_t image_id;  /* persistent id: retransmitting replaces, keeping
                         * memory bounded to one canvas (see output_kitty.c) */
    int col, row;       /* destination cell, 0-based */
    int xoff, yoff;     /* pixel offset inside the cell (< cell size) */
} kitty_place;

/* Encode the framebuffer as a kitty graphics transmission and write it to
 * fd. argb is an ARGB8888 top-down buffer (stride bytes per row). The
 * transmission is a chain of APC frames (m=1 continuation, m=0 final),
 * PNG payload (f=100). Returns 0 on success, -1 on write failure. */
int kitty_encode_frame(int fd, const uint8_t *argb, int w, int h, int stride);

/* Encode only the rectangle [rx,ry,rw,rh) of the framebuffer and transmit
 * it as image `place.image_id`, placed at the cursor position (which the
 * caller leaves at place.col/place.row, moved with CUP) plus the pixel
 * offsets. Only the rectangle is swizzled, deflated and written — the cost
 * of a frame is the cost of what changed. Rect is clipped to the buffer.
 * Returns 0 on success, -1 on failure. */
int kitty_encode_rect(int fd, const uint8_t *argb, int fb_w, int fb_h,
                      int stride, int rx, int ry, int rw, int rh,
                      const kitty_place *place);

#endif /* KITTY_ENCODE_H */
