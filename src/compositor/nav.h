/*
 * TGS focus model and ring computation (docs/navigation.md §A–§C).
 *
 * Pure policy layer: no rendering engine, no PTY, no heap. The compositor owns the widget
 * tree, the effective attributes and the per-window focus registry; the
 * backend only executes what this module decides (§A.1 division of labour).
 */
#ifndef TGS_NAV_H
#define TGS_NAV_H

/* tgs_nav_dir — the direction argument of the backend's focus_dir (§J.6) —
 * lives in the backend interface, which the compositor already includes. */
#include "tgs_backend.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_MAX_WIDGETS 256
#define NAV_MAX_WINDOWS 16
#define NAV_MAX_SCOPES  64
#define NAV_RING_MAX    64

/* ---- Widget-type policy (§A.3, §D.2) ---- */
int nav_type_focusable(tgs_widget_type type);
int nav_type_consumes_arrows(tgs_widget_type type);
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
    uint32_t        event_mask;    /* EVT_BIND subscriptions: bit i = tgs_event_type i
                                    * (CLICK/KEY/VALUE gate app delivery; D6) */
    int8_t          attr_focusable; /* -1 = type default (§J.4) */
    int8_t          attr_arrows;    /* -1 = type default */
    int8_t          attr_tab;       /* 0/1 */
    int8_t          scope;          /* 0 none, 1 GROUP, 2 TRAP (§C.1) */
    int             disabled;
    int             seq;            /* creation order = tree order (§B.1) */
    int32_t         focus_index;    /* -1 = auto (§B.2) */
    int             scope_idx;      /* innermost scope, filled by nav_rebuild */
} nav_widget;

typedef struct {
    int kind;       /* 0 = window root, 1 = GROUP, 2 = TRAP */
    int container;  /* widget id of the scope container; 0 for a window root */
    int parent;     /* enclosing scope index; -1 for a window root */
    int remembered; /* widget id remembered inside this scope (§C.2) */
    int cursor;     /* index into e[]; -1 = no cursor */
    int n;
    struct {
        int is_scope;
        int ref;    /* widget id, or scope index when is_scope */
    } e[NAV_RING_MAX];
} nav_scope;

typedef struct {
    int             used;
    int             win_id;
    void           *handle;  /* backend root object, opaque here */
    tgs_window_type type;
    int             root_scope;
    int             focus;      /* focused widget id, 0 = none (§A.2) */
    int             last_focus; /* last focused widget id, §G.2 */
} nav_window;

typedef struct {
    nav_widget widgets[NAV_MAX_WIDGETS];
    int        widget_count;
    nav_scope  scopes[NAV_MAX_SCOPES];
    int        scope_count;
    nav_window windows[NAV_MAX_WINDOWS];
    int        window_count;
    int        active_win;  /* win_id holding the keyboard; 0 = none */
    int        dirty;       /* rings need recomputation (§B.2) */
} nav_model;

void nav_init(nav_model *m);

int  nav_add_window(nav_model *m, int win_id, tgs_window_type type, void *handle);
void nav_remove_window(nav_model *m, int win_id);
nav_window *nav_window_find(nav_model *m, int win_id);

int  nav_add_widget(nav_model *m, int id, int win_id, int parent_id,
                    int parent_is_window, tgs_widget_type type, void *handle);
void nav_remove_widget(nav_model *m, int id);
nav_widget *nav_widget_find(nav_model *m, int id);
nav_widget *nav_widget_by_handle(nav_model *m, void *handle);

/* Effective attributes: type default overridden by WGT_ATTR (§A.3, §J.4). */
int  nav_widget_focusable(const nav_widget *w);
int  nav_widget_consumes_arrows(const nav_widget *w);
int  nav_widget_consumes_tab(const nav_widget *w);

/* Scope kind of a container: 0 none, 1 GROUP, 2 TRAP. */
int  nav_widget_scope(const nav_widget *w);

/* Applies one WGT_ATTR. Returns 1 when applied, 0 when ignored — an
 * out-of-range value, an unknown widget, or a scope write on a leaf widget is
 * ignored and the previous value kept (§J.4). */
int  nav_attr_set(nav_model *m, int widget_id, tgs_widget_attr attr, int32_t value);

/* Recomputes every ring when the model is dirty (§B.1, §B.2, §C.2). */
void nav_rebuild(nav_model *m);

/* Traversal (§D.2). Returns 1 and writes the target widget id (0 = "clear
 * focus") plus its tgs_focus_reason, or 0 when nothing can move. */
int  nav_step(nav_model *m, int win_id, int backwards, int *focus_out, int *reason_out);
/* Home (to_last = 0) / End (to_last = 1) inside the current ring (§D.2). */
int  nav_edge(nav_model *m, int win_id, int to_last, int *focus_out, int *reason_out);

/* Handles of the ring the focused widget lives in — the candidate set the
 * backend's focus_dir searches (§D.3 rule 3). Returns the count. */
int  nav_ring_handles(nav_model *m, int win_id, void **handles, int max);

/* Focus bookkeeping; returns the previous focus (§G.2 last_focus). */
int  nav_set_focus(nav_model *m, int win_id, int widget_id);
/* Focus target on activation: remembered widget if still focusable, else the
 * first ring member; 1 when the remembered one was used (§G.2). */
int  nav_restore(nav_model *m, int win_id, int *focus_out);
/* Successor of a destroyed widget: next ring member, else previous, else 0. */
int  nav_successor(nav_model *m, int win_id, int widget_id);
/* Every focusable widget of a window in tree order — the backend's enrollment
 * set (§I.1). Rings can be smaller: a scope hides its inner members behind a
 * single entry. Returns the count. */
int  nav_all_handles(nav_model *m, int win_id, void **handles, int max);
/* First focusable widget of a window, 0 when it has none (§E init focus). */
int  nav_first_focusable(nav_model *m, int win_id);
/* Topmost other window with focusable content, for focus restoration on
 * destroy/hide (§G.2). 0 when there is none. */
int  nav_other_window(nav_model *m, int win_id);

#ifdef __cplusplus
}
#endif
#endif /* TGS_NAV_H */
