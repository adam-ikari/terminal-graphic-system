/*
 * TGS Display Output Abstraction
 * Platform-agnostic display: provides framebuffer, presents to screen.
 * SDL backend for desktop, sixel for terminal.
 */
#ifndef TGS_OUTPUT_H
#define TGS_OUTPUT_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    int bpp;       /* bits per pixel */
    int stride;    /* bytes per row */
    uint8_t *buffer;
    void *backend_priv;
} tgs_display;

int  output_init(tgs_display *d, int width, int height);
void output_present(tgs_display *d);

/* Resize the surface the backend presents. A backend whose surface has one
 * fixed size (an embedded panel) returns -1, and the caller then leaves the
 * display as it is. */
int  output_resize(tgs_display *d, int width, int height);
void output_cleanup(tgs_display *d);

#endif /* TGS_OUTPUT_H */
