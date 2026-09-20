/*
 * TGS widget registry (SVG semantics).
 *
 * The compositor keeps only what event routing needs: the widget tree
 * (id → handle, parent links, container validity) and the EVT_BIND event
 * subscription masks. There is NO window management, NO focus model: the
 * compositor neither manages windows nor routes focus — focus is a
 * graphics-API / WM concern owned by the program. The WM simply forwards
 * input events (tagged with their window id) to the program, which decides
 * what responds and draws its own focus visuals.
 *
 * Pure policy layer: no rendering engine, no PTY, no heap.
 */
#include "nav.h"

#include <string.h>

int nav_type_is_container(tgs_widget_type t)
{
    return t == TGS_WIDGET_BOX;
}

void nav_init(nav_model *m)
{
    memset(m, 0, sizeof(*m));
}

nav_widget *nav_widget_find(nav_model *m, int id)
{
    int i;

    if (id == 0) return NULL;
    for (i = 0; i < NAV_MAX_WIDGETS; i++) {
        if (m->widgets[i].used && m->widgets[i].id == id)
            return &m->widgets[i];
    }
    return NULL;
}

nav_widget *nav_widget_by_handle(nav_model *m, void *handle)
{
    int i;

    if (!handle) return NULL;
    for (i = 0; i < NAV_MAX_WIDGETS; i++) {
        if (m->widgets[i].used && m->widgets[i].handle == handle)
            return &m->widgets[i];
    }
    return NULL;
}

int nav_add_widget(nav_model *m, int id, int win_id, int parent_id,
                   int parent_is_window, tgs_widget_type type, void *handle)
{
    nav_widget *w;
    int i;

    if (id <= 0) return -1;
    if (nav_widget_find(m, id)) return -1;      /* duplicate id */
    for (i = 0; i < NAV_MAX_WIDGETS; i++) {
        if (!m->widgets[i].used) break;
    }
    if (i >= NAV_MAX_WIDGETS) return -1;

    w = &m->widgets[i];
    w->used = 1;
    w->id = id;
    w->win_id = win_id;
    w->parent = parent_id;
    w->parent_is_window = parent_is_window;
    w->handle = handle;
    w->type = type;
    w->event_mask = 0;
    return 0;
}

void nav_remove_widget(nav_model *m, int id)
{
    nav_widget *w = nav_widget_find(m, id);

    if (w) memset(w, 0, sizeof(*w));
}

int nav_subscribe(nav_model *m, int widget_id, tgs_event_type type)
{
    nav_widget *w = nav_widget_find(m, widget_id);

    if (!w) return -1;
    w->event_mask |= (1u << (unsigned)type);
    return 0;
}
