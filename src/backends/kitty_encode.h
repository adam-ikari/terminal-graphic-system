/*
 * kitty_encode.h — kitty graphics protocol encoder (shared).
 */
#ifndef KITTY_ENCODE_H
#define KITTY_ENCODE_H

#include <stdint.h>
#include <unistd.h>

/* Encode the framebuffer as a kitty graphics transmission and write it to
 * fd. argb is an ARGB8888 top-down buffer (stride bytes per row). The
 * transmission is a chain of APC frames (m=1 continuation, m=0 final),
 * formatted as raw RGBA (f=32) — no PNG dependency on the wire path.
 * Returns 0 on success, -1 on write failure. */
int kitty_encode_frame(int fd, const uint8_t *argb, int w, int h, int stride);

#endif /* KITTY_ENCODE_H */