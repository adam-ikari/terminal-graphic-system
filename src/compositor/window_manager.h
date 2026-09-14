/*
 * TGS Window Manager
 * Protocol frame dispatch, focus authority (docs/navigation.md §A–§H) and
 * event routing. ID→handle mapping and ring computation live in nav.c.
 */
#ifndef TGS_WINDOW_MANAGER_H
#define TGS_WINDOW_MANAGER_H

#include "tgs_protocol.h"
#include "tgs_frame.h"
#include "tgs_backend.h"
#include "nav.h"

typedef struct {
    tgs_backend *backend;
    int pty_fd;          /* Main app PTY fd */
    int ime_pty_fd;      /* IME app PTY fd (-1 if not running) */
    int frame_counter;
    int disp_w, disp_h;
    int hello_received;
    int ime_connected;   /* 1 if IME app has sent HELLO */

    /* Focus authority (§A.1): widget tree, attributes, rings, per-window
     * registry. The backend only executes what nav.c decides. */
    nav_model nav;
    int pending_focus;   /* widget the compositor just focused; 0 = none */
    int pending_reason;  /* its reason, echoed by the backend's focus report */
    int consumed_key;    /* key whose press navigation consumed; release to drop */
    int consumed_mods;
} window_manager;

void wm_init(window_manager *wm, tgs_backend *backend, int pty_fd, int ime_pty_fd);
void wm_handle_frame(const tgs_frame *frame, void *user_data);
void wm_backend_event(void *widget_handle, tgs_event_type type,
                      const char *event_data, void *user_data);

#endif /* TGS_WINDOW_MANAGER_H */
