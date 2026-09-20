/*
 * TGS Abstract Backend Interface
 * Backends implement this struct; one registered at a time.
 *
 * The backend is a dumb scene painter: it holds one canvas tree, paints the
 * three primitives (TEXT / BOX / IMAGE), does hit-testing, and reports input.
 * It has NO windows, NO focus, NO navigation, NO layout, NO text editing:
 * those are program-side concerns expressed through events and repaints.
 */
#ifndef TGS_BACKEND_H
#define TGS_BACKEND_H

#include "tgs_protocol.h"
#include <stdint.h>

typedef struct tgs_backend tgs_backend;

/* Backend calls this when an input event fires. */
typedef void (*tgs_event_cb)(void *element_handle,
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

    /* Elements — the canvas root is created by init; create_element(parent)
     * with a NULL parent attaches to the canvas root. */
    void *(*create_element)(void *parent, tgs_widget_type type);
    void  (*set_element_rect)(void *handle, int x, int y, int w, int h);
    void  (*set_element_content)(void *handle, const char *text);
    void  (*set_element_style)(void *handle, tgs_style_prop prop, int32_t value);
    void  (*destroy_element)(void *handle);

    /* Events */
    void (*set_event_callback)(tgs_event_cb cb, void *user_data);

    /* Input injection */
    void (*inject_pointer)(int x, int y, int phase);   /* phase 0=down 1=move 2=up */
    void (*inject_key)(int key, int mods, int pressed);

    /* Hit-testing the program may query to route a POINTER event to the
     * element under the cursor (returns the handle, or NULL). The renderer
     * already emits CLICK/HOVER from its own hit-test; this lets the program
     * implement its own drag targeting. */
    void *(*hit_test)(int x, int y);

    /* The clipboard's text, or NULL when there is none. The backend owns the
     * storage and keeps it valid until the next call. */
    const char *(*clipboard_text)(void);
};

/* Register a backend (replaces previous). */
void tgs_backend_register(tgs_backend *backend);

/* Get currently registered backend, or NULL. */
tgs_backend *tgs_backend_get(void);

#endif /* TGS_BACKEND_H */
