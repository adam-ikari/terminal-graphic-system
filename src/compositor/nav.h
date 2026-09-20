/*
 * TGS widget registry (SVG semantics).
 *
 * The compositor keeps only what event routing needs: the widget tree
 * (id → handle, parent links, container validity) and the EVT_BIND event
 * subscription masks. There is NO window management, NO focus model, NO
 * activation state: the compositor neither manages windows nor routes focus —
 * those are graphics-API / WM concerns owned by the program. The WM simply
 * forwards input events (tagged with their window id) to the program, which
 * decides what responds and draws its own focus visuals.
 *
 * Pure policy layer: no rendering engine, no PTY, no heap.
 */
#ifndef TGS_NAV_H
#define TGS_NAV_H

#include "tgs_backend.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_MAX_WIDGETS 256

/* ---- Widget-type policy ---- */
int nav_type_is_container(tgs_widget_type type);

typedef struct {
    int             used;
    int             id;
    int             win_id;
    int             parent;         /* parent window or widget id */
    int             parent_is_window; /* parent id is a window id, not a widget —
                                       * the two id spaces can collide */
    void           *handle;         /* backend handle, opaque here */
    tgs_widget_type type;
    uint32_t        event_mask;    /* EVT_BIND subscriptions: bit i =
                                    * tgs_event_type i (gates app delivery) */
} nav_widget;

typedef struct {
    nav_widget widgets[NAV_MAX_WIDGETS];
} nav_model;

void nav_init(nav_model *m);

int  nav_add_widget(nav_model *m, int id, int win_id, int parent_id,
                    int parent_is_window, tgs_widget_type type, void *handle);
void nav_remove_widget(nav_model *m, int id);
nav_widget *nav_widget_find(nav_model *m, int id);
nav_widget *nav_widget_by_handle(nav_model *m, void *handle);

/* Subscribe a widget to an event type (EVT_BIND). */
int nav_subscribe(nav_model *m, int widget_id, tgs_event_type type);

#ifdef __cplusplus
}
#endif

#endif /* TGS_NAV_H */
