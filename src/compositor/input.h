/*
 * TGS Input Abstraction
 * Polls platform input (SDL events) and injects into backend.
 */
#ifndef TGS_INPUT_H
#define TGS_INPUT_H

#include "tgs_backend.h"

/* A program that has not spoken TGS is a character program: its keys must
 * reach it exactly as a terminal would send them. The sink is the compositor's
 * hook for that — the platform backends stay unaware of who consumes keys. */
typedef void (*tgs_input_key_sink)(int key, int mods, int pressed, void *ud);
void input_set_key_sink(tgs_input_key_sink sink, void *ud);

/* Mouse, reported the same way. `button` is 0 left / 1 middle / 2 right, and
 * `pressed` is 1 down, 0 up, -1 motion. The compositor owns the mapping from
 * pixels to whatever the consumer wants. */
typedef void (*tgs_input_mouse_sink)(int x, int y, int button, int pressed, void *ud);
void input_set_mouse_sink(tgs_input_mouse_sink sink, void *ud);

/* The window changed size. What that means for the grid, the display and the
 * program is the compositor's call, so the backend only reports it. */
typedef void (*tgs_input_resize_sink)(int w, int h, void *ud);
void input_set_resize_sink(tgs_input_resize_sink sink, void *ud);

/* The window gained (1) or lost (0) focus. */
typedef void (*tgs_input_focus_sink)(int focused, void *ud);
void input_set_focus_sink(tgs_input_focus_sink sink, void *ud);

int  input_init(tgs_backend *backend);
void input_poll(void);
void input_cleanup(void);

#endif /* TGS_INPUT_H */
