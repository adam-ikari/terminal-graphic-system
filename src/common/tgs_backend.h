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
    void  (*set_widget_style)(void *handle, tgs_style_prop prop, int32_t value);
    void  (*set_widget_layout)(void *handle, tgs_layout_type layout);
    void  (*destroy_widget)(void *handle);

    /* Events */
    void (*set_event_callback)(tgs_event_cb cb, void *user_data);

    /* Input injection */
    void (*inject_mouse)(int x, int y, int button, int pressed);
    void (*inject_key)(int key, int mods, int pressed);
};

/* Register a backend (replaces previous). */
void tgs_backend_register(tgs_backend *backend);

/* Get currently registered backend, or NULL. */
tgs_backend *tgs_backend_get(void);

#endif /* TGS_BACKEND_H */
