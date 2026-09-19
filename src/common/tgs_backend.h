/*
 * TGS Abstract Backend Interface
 * Backends implement this struct; one registered at a time.
 */
#ifndef TGS_BACKEND_H
#define TGS_BACKEND_H

#include "tgs_protocol.h"
#include <stdint.h>

typedef struct tgs_backend tgs_backend;

/* Backend calls this when a widget event fires. */
typedef void (*tgs_event_cb)(void *widget_handle,
                              tgs_event_type type,
                              const char *event_data,
                              void *user_data);

/* Navigation direction — argument of focus_dir (docs/navigation.md §J.6). */
typedef enum {
    TGS_NAV_LEFT = 0,
    TGS_NAV_RIGHT,
    TGS_NAV_UP,
    TGS_NAV_DOWN,
} tgs_nav_dir;

/* What the backend does with one key edge, decided by the compositor's
 * navigation precedence hook (docs/navigation.md §D.3):
 *   TGS_NAV_PASS     — not navigation: the renderer's usual indev path delivers it;
 *   TGS_NAV_WIDGET   — deliver straight to the focused widget, bypassing the
 *                      indev's own Tab/Enter/ESC group handling;
 *   TGS_NAV_CONSUMED — the compositor acted on it; the renderer must never see it. */
typedef enum {
    TGS_NAV_PASS = 0,
    TGS_NAV_WIDGET,
    TGS_NAV_CONSUMED,
} tgs_nav_key_action;

typedef tgs_nav_key_action (*tgs_nav_key_cb)(int key, int mods, int pressed,
                                             void *user_data);

struct tgs_backend {
    void *user_data;

    /* Lifecycle */
    int  (*init)(int width, int height);
    void (*tick)(uint32_t ms);
    void (*deinit)(void);

    /* Rendering */
    void (*render)(void);
    void (*set_size)(int w, int h);

    /* Windows (root containers) */
    void *(*create_window)(tgs_window_type type, const char *title);
    void  (*destroy_window)(void *handle);

    /* Widgets */
    void *(*create_widget)(void *parent, tgs_widget_type type);
    void  (*set_widget_rect)(void *handle, int x, int y, int w, int h);
    void  (*set_widget_content)(void *handle, const char *text);
    void  (*insert_widget_text)(void *handle, const char *text);  /* Insert text at cursor position (for IME commit) */
    void  (*set_widget_preedit)(void *handle, const char *text, int cursor); /* Render IME preedit as an overlay; empty text clears it (§H.2) */
    void  (*set_widget_style)(void *handle, tgs_style_prop prop, int32_t value);
    void  (*set_widget_layout)(void *handle, tgs_layout_type layout,
                               int cols, int rows);  /* cols/rows used for GRID; rows 0 = auto */
    void  (*destroy_widget)(void *handle);

    /* Events */
    void (*set_event_callback)(tgs_event_cb cb, void *user_data);

    /* Input injection */
    void (*inject_mouse)(int x, int y, int button, int pressed);
    void (*inject_key)(int key, int mods, int pressed);

    /* Focus & navigation (docs/navigation.md §I, §J.6), appended so the
     * existing members keep their order. The compositor owns focus and ring
     * order; these calls execute what it decided. */
    void  (*set_window_ring)(void *window, void **widgets, int count);
    void  (*set_active_window)(void *window);
    void  (*set_focus)(void *handle);           /* NULL clears the focus visuals */
    void *(*focus_dir)(void *from, void **candidates, int count, tgs_nav_dir dir);
    void  (*set_widget_focusable)(void *handle, int focusable);
    void  (*set_nav_key_cb)(tgs_nav_key_cb cb, void *user_data);

    /* The clipboard's text, or NULL when there is none. The backend owns the
     * storage and keeps it valid until the next call, so callers never free it. */
    const char *(*clipboard_text)(void);

    /* Query a widget's actual geometry in screen/absolute coordinates
     * (parent offsets accumulated; a window root sits at 0,0). Returns 0
     * on success, nonzero if the handle is unknown/not yet laid out. */
    int (*widget_geometry)(void *handle, int *x, int *y, int *w, int *h);
};

/* Register a backend (replaces previous). */
void tgs_backend_register(tgs_backend *backend);

/* Get currently registered backend, or NULL. */
tgs_backend *tgs_backend_get(void);

#endif /* TGS_BACKEND_H */
