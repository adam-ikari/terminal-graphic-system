/*
 * TGS Window Manager (SVG semantics)
 *
 * Maps program frames to backend element calls and routes the backend's
 * input events back to the program (or the IME program). No window, focus,
 * navigation, layout, subscription, or geometry logic — the program owns
 * all of those.
 */
#ifndef TGS_WINDOW_MANAGER_H
#define TGS_WINDOW_MANAGER_H

#include "tgs_protocol.h"
#include "tgs_frame.h"
#include "tgs_backend.h"
#include "nav.h"

typedef struct {
    tgs_backend *backend;
    int pty_fd;          /* Main program PTY fd */
    int ime_pty_fd;      /* IME program PTY fd (-1 if not running) */
    int frame_counter;
    int disp_w, disp_h;
    int hello_received;
    int ime_connected;   /* 1 if the IME program has sent HELLO */

    /* Element registry (id → backend handle). */
    nav_model nav;
} window_manager;

void wm_init(window_manager *wm, tgs_backend *backend, int pty_fd, int ime_pty_fd);

/* Update the cached display size (disp_w/disp_h) — call at init and on every
 * resize; NTF_RESIZE reports these dimensions. */
void wm_set_display_size(window_manager *wm, int w, int h);
void wm_handle_frame(const tgs_frame *frame, void *user_data);

void wm_backend_event(void *element_handle, tgs_event_type type,
                      const char *event_data, void *user_data);

/* No-op (layout is program policy); kept for caller stability. */
void wm_flush_geometry(window_manager *wm);

#endif /* TGS_WINDOW_MANAGER_H */