/*
 * TGS focus model and ring computation — see nav.h and docs/navigation.md.
 *
 * Ring order is tree order (§B.1): pre-order depth-first, children in creation
 * order, which for well-formed trees is creation order. FOCUS_INDEX (§B.2)
 * sorts explicit entries first, ascending, then auto entries in tree order.
 * Containers are transparent unless declared a scope (§C.1); a scope exposes
 * one entry and owns an inner ring.
 */
#include "nav.h"

#include <string.h>

/* ---- Widget-type policy (§A.3) ---- */

int nav_type_is_container(tgs_widget_type t)
{
    return t == TGS_WIDGET_VLAYOUT || t == TGS_WIDGET_HLAYOUT ||
           t == TGS_WIDGET_GLAYOUT || t == TGS_WIDGET_SCROLL;
}

int nav_type_focusable(tgs_widget_type t)
{
    switch (t) {
    case TGS_WIDGET_LABEL:
    case TGS_WIDGET_PROGRESS:
    case TGS_WIDGET_IMAGE:
    /* Layout containers are transparent for focus (§B.3): their children take
     * part in the enclosing ring, the container itself is never a stop. */
    case TGS_WIDGET_VLAYOUT:
    case TGS_WIDGET_HLAYOUT:
    case TGS_WIDGET_GLAYOUT:
    case TGS_WIDGET_SCROLL:
        return 0;
    default:
        return 1;
    }
}

int nav_type_consumes_arrows(tgs_widget_type t)
{
    switch (t) {
    case TGS_WIDGET_INPUT:
    case TGS_WIDGET_SLIDER:
    case TGS_WIDGET_LIST:
    case TGS_WIDGET_TABLE:
    case TGS_WIDGET_MENU:
    case TGS_WIDGET_DROPDOWN:
    case TGS_WIDGET_TIMEPICK:
    case TGS_WIDGET_DATEPICK:
        return 1;
    default:
        return 0;
    }
}

/* ---- Effective attributes (§J.4) ---- */

int nav_widget_focusable(const nav_widget *w)
{
    if (!w || !w->used || w->disabled) return 0;
    if (w->attr_focusable >= 0) return w->attr_focusable ? 1 : 0;
    return nav_type_focusable(w->type);
}

int nav_widget_consumes_arrows(const nav_widget *w)
{
    if (!w) return 0;
    if (w->attr_arrows >= 0) return w->attr_arrows ? 1 : 0;
    return nav_type_consumes_arrows(w->type);
}

int nav_widget_consumes_tab(const nav_widget *w)
{
    return (w && w->attr_tab) ? 1 : 0;
}

int nav_widget_scope(const nav_widget *w)
{
    if (!w || !w->used || !nav_type_is_container(w->type)) return 0;
    return w->scope;
}

/* ---- Model bookkeeping ---- */

void nav_init(nav_model *m)
{
    memset(m, 0, sizeof(*m));
    m->dirty = 1;
}

nav_window *nav_window_find(nav_model *m, int win_id)
{
    int i;

    for (i = 0; i < m->window_count; i++) {
        if (m->windows[i].used && m->windows[i].win_id == win_id)
            return &m->windows[i];
    }
    return NULL;
}

nav_widget *nav_widget_find(nav_model *m, int id)
{
    int i;

    if (id == 0) return NULL;
    for (i = 0; i < m->widget_count; i++) {
        if (m->widgets[i].used && m->widgets[i].id == id) return &m->widgets[i];
    }
    return NULL;
}

nav_widget *nav_widget_by_handle(nav_model *m, void *handle)
{
    int i;

    if (!handle) return NULL;
    for (i = 0; i < m->widget_count; i++) {
        if (m->widgets[i].used && m->widgets[i].handle == handle)
            return &m->widgets[i];
    }
    return NULL;
}

int nav_add_window(nav_model *m, int win_id, tgs_window_type type, void *handle)
{
    nav_window *win;

    if (nav_window_find(m, win_id)) return -1;
    if (m->window_count >= NAV_MAX_WINDOWS) return -1;

    win = &m->windows[m->window_count++];
    memset(win, 0, sizeof(*win));
    win->used = 1;
    win->win_id = win_id;
    win->handle = handle;
    win->type = type;
    win->root_scope = -1;
    m->dirty = 1;
    return 0;
}

void nav_remove_window(nav_model *m, int win_id)
{
    int i;

    for (i = 0; i < m->widget_count; i++) {
        if (m->widgets[i].used && m->widgets[i].win_id == win_id)
            m->widgets[i].used = 0;
    }
    for (i = 0; i < m->window_count; i++) {
        if (m->windows[i].used && m->windows[i].win_id == win_id) {
            m->windows[i] = m->windows[--m->window_count];
            break;
        }
    }
    if (m->active_win == win_id) m->active_win = 0;
    m->dirty = 1;
}

int nav_add_widget(nav_model *m, int id, int win_id, int parent_id,
                   int parent_is_window, tgs_widget_type type, void *handle)
{
    static int seq;
    nav_widget *w = NULL;
    int i;

    if (nav_widget_find(m, id)) return -1;
    for (i = 0; i < m->widget_count; i++) {
        if (!m->widgets[i].used) { w = &m->widgets[i]; break; }
    }
    if (!w) {
        if (m->widget_count >= NAV_MAX_WIDGETS) return -1;
        w = &m->widgets[m->widget_count++];
    }

    memset(w, 0, sizeof(*w));
    w->used = 1;
    w->id = id;
    w->win_id = win_id;
    w->parent = parent_id;
    w->parent_is_window = parent_is_window ? 1 : 0;
    w->handle = handle;
    w->type = type;
    w->attr_focusable = -1;
    w->attr_arrows = -1;
    w->attr_tab = 0;
    w->scope = 0;
    w->focus_index = -1;
    w->seq = ++seq;
    w->scope_idx = -1;
    m->dirty = 1;
    return 0;
}

void nav_remove_widget(nav_model *m, int id)
{
    nav_widget *w = nav_widget_find(m, id);
    int i;

    if (!w) return;
    w->used = 0;
    /* No scope may remember the corpse (§C.2). */
    for (i = 0; i < m->scope_count; i++) {
        if (m->scopes[i].remembered == id) m->scopes[i].remembered = 0;
    }
    m->dirty = 1;
}

int nav_attr_set(nav_model *m, int widget_id, tgs_widget_attr attr, int32_t value)
{
    nav_widget *w = nav_widget_find(m, widget_id);

    if (!w) return 0;

    switch (attr) {
    case TGS_ATTR_FOCUSABLE:
        if (value < -1 || value > 1) return 0;
        w->attr_focusable = (int8_t)value;
        break;
    case TGS_ATTR_NAV_ARROWS:
        if (value < -1 || value > 1) return 0;
        w->attr_arrows = (int8_t)value;
        break;
    case TGS_ATTR_NAV_TAB:
        if (value < 0 || value > 1) return 0;
        w->attr_tab = (int8_t)value;
        break;
    case TGS_ATTR_FOCUS_INDEX:
        if (value < -1) return 0;
        w->focus_index = value;
        break;
    case TGS_ATTR_FOCUS_SCOPE:
        /* Container-only: a scope write on a leaf widget is ignored (§J.4). */
        if (!nav_type_is_container(w->type)) return 0;
        if (value < 0 || value > 2) return 0;
        w->scope = (int8_t)value;
        break;
    default:
        return 0;
    }

    m->dirty = 1;
    return 1;
}

/* ---- Ring construction ---- */

typedef struct {
    struct {
        int is_scope;
        int ref;    /* widget id, or scope index */
        int k0, k1; /* sort keys: (explicit ? 0 : 1, index or tree position) */
    } e[NAV_RING_MAX];
    int n;
} ring_builder;

static int scope_new(nav_model *m, int kind, int container, int parent)
{
    nav_scope *s;

    if (m->scope_count >= NAV_MAX_SCOPES) return -1;
    s = &m->scopes[m->scope_count];
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->container = container;
    s->parent = parent;
    s->cursor = -1;
    return m->scope_count++;
}

static void rb_add(ring_builder *rb, int is_scope, int ref, int k0, int k1)
{
    if (rb->n >= NAV_RING_MAX) return;
    rb->e[rb->n].is_scope = is_scope;
    rb->e[rb->n].ref = ref;
    rb->e[rb->n].k0 = k0;
    rb->e[rb->n].k1 = k1;
    rb->n++;
}

static void build_ring(nav_model *m, int win_id, int scope_idx, int parent_id,
                       int parent_is_window);

static void collect(nav_model *m, ring_builder *rb, int win_id, int parent_id,
                    int parent_is_window, int scope_idx)
{
    int i, pos = 0;

    /* Creation order within one parent is tree order (§B.1). */
    for (i = 0; i < m->widget_count; i++) {
        nav_widget *w = &m->widgets[i];
        int kind;

        if (!w->used || w->win_id != win_id || w->parent != parent_id ||
            w->parent_is_window != parent_is_window) {
            continue;
        }

        kind = nav_widget_scope(w);
        if (kind != 0) {
            int sub = scope_new(m, kind, w->id, scope_idx);

            w->scope_idx = scope_idx;
            rb_add(rb, 1, sub, 1, pos++);
            if (sub >= 0) build_ring(m, win_id, sub, w->id, 0);
        } else if (nav_widget_focusable(w)) {
            w->scope_idx = scope_idx;
            rb_add(rb, 0, w->id,
                   w->focus_index >= 0 ? 0 : 1,
                   w->focus_index >= 0 ? w->focus_index : pos);
            pos++;
        } else {
            /* Transparent container: its children join this very ring (§B.3). */
            w->scope_idx = scope_idx;
            collect(m, rb, win_id, w->id, 0, scope_idx);
        }
    }
}

static void build_ring(nav_model *m, int win_id, int scope_idx, int parent_id,
                       int parent_is_window)
{
    ring_builder rb;
    nav_scope *s;
    int i, j;

    if (scope_idx < 0) return;
    rb.n = 0;
    collect(m, &rb, win_id, parent_id, parent_is_window, scope_idx);

    /* Stable insertion sort: explicit FOCUS_INDEX first ascending, then auto
     * entries in tree order (§B.2). */
    for (i = 1; i < rb.n; i++) {
        for (j = i; j > 0; j--) {
            int ordered = rb.e[j - 1].k0 < rb.e[j].k0 ||
                          (rb.e[j - 1].k0 == rb.e[j].k0 &&
                           rb.e[j - 1].k1 <= rb.e[j].k1);
            int ts, tr;

            if (ordered) break;
            ts = rb.e[j - 1].is_scope;
            tr = rb.e[j - 1].ref;
            rb.e[j - 1] = rb.e[j];
            rb.e[j].is_scope = ts;
            rb.e[j].ref = tr;
        }
    }

    s = &m->scopes[scope_idx];
    s->n = rb.n;
    for (i = 0; i < rb.n; i++) {
        s->e[i].is_scope = rb.e[i].is_scope;
        s->e[i].ref = rb.e[i].ref;
    }
    s->cursor = rb.n ? 0 : -1;
}

static int scope_contains(nav_model *m, int scope_idx, int widget_id)
{
    nav_widget *w = nav_widget_find(m, widget_id);
    int cur;

    if (!w) return 0;
    for (cur = w->scope_idx; cur >= 0; cur = m->scopes[cur].parent) {
        if (cur == scope_idx) return 1;
    }
    return 0;
}

/* First (from_end = 0) or last (from_end = 1) focusable member of a scope,
 * descending into nested scopes. 0 when the scope holds nothing focusable. */
static int scope_member(nav_model *m, int scope_idx, int from_end)
{
    nav_scope *s;
    int i;

    if (scope_idx < 0 || scope_idx >= m->scope_count) return 0;
    s = &m->scopes[scope_idx];
    for (i = from_end ? s->n - 1 : 0; i >= 0 && i < s->n; i += from_end ? -1 : 1) {
        if (s->e[i].is_scope) {
            int id = scope_member(m, s->e[i].ref, from_end);

            if (id) return id;
        } else {
            nav_widget *w = nav_widget_find(m, s->e[i].ref);

            if (nav_widget_focusable(w)) return w->id;
        }
    }
    return 0;
}

/* The widget an entry resolves to when focus enters it (§C.2): the scope's
 * remembered member when it is still alive and focusable, else its first. */
static int entry_member(nav_model *m, int scope_idx, int idx, int *remembered)
{
    nav_scope *s = &m->scopes[scope_idx];
    nav_widget *rem;

    if (idx < 0 || idx >= s->n) return 0;
    if (remembered) *remembered = 0;
    if (!s->e[idx].is_scope) {
        nav_widget *w = nav_widget_find(m, s->e[idx].ref);

        return nav_widget_focusable(w) ? w->id : 0;
    }

    rem = nav_widget_find(m, m->scopes[s->e[idx].ref].remembered);
    if (rem && nav_widget_focusable(rem) &&
        scope_contains(m, s->e[idx].ref, rem->id)) {
        if (remembered) *remembered = 1;
        return rem->id;
    }
    return scope_member(m, s->e[idx].ref, 0);
}

/* Entry of `scope_idx` that leads to widget_id, -1 when it is not inside. */
static int entry_of_widget(nav_model *m, int scope_idx, int widget_id)
{
    nav_scope *s = &m->scopes[scope_idx];
    int i;

    for (i = 0; i < s->n; i++) {
        if (s->e[i].is_scope) {
            if (scope_contains(m, s->e[i].ref, widget_id)) return i;
        } else if (s->e[i].ref == widget_id) {
            return i;
        }
    }
    return -1;
}

static void sync_cursors(nav_model *m, nav_window *win)
{
    int scope_idx = win->root_scope;

    if (win->focus == 0) return;
    while (scope_idx >= 0) {
        nav_scope *s = &m->scopes[scope_idx];
        int idx = entry_of_widget(m, scope_idx, win->focus);

        if (idx < 0) return;
        s->cursor = idx;
        if (!s->e[idx].is_scope) return;
        scope_idx = s->e[idx].ref;
    }
}

void nav_rebuild(nav_model *m)
{
    int i;

    if (!m->dirty) return;
    m->scope_count = 0;

    for (i = 0; i < m->window_count; i++) {
        nav_window *win = &m->windows[i];

        if (!win->used) continue;
        /* A DIALOG window's root scope is a TRAP (§C.1). */
        win->root_scope = scope_new(m, win->type == TGS_WINDOW_DIALOG ? 2 : 0,
                                    0, -1);
        build_ring(m, win->win_id, win->root_scope, win->win_id, 1);
        sync_cursors(m, win);
    }
    m->dirty = 0;
}

/* ---- Traversal ---- */

static int enter_entry(nav_model *m, int scope_idx, int idx, int nav_reason,
                       int *focus_out, int *reason_out)
{
    int remembered = 0;
    int id = entry_member(m, scope_idx, idx, &remembered);

    if (!id) return 0;
    *focus_out = id;
    *reason_out = remembered ? TGS_REASON_SCOPE_RESTORE : nav_reason;
    return 1;
}

static int move_cursor(nav_scope *s, int backwards)
{
    if (s->n <= 0) return -1;
    if (s->cursor < 0 || s->cursor >= s->n)
        return backwards ? s->n - 1 : 0;
    return (s->cursor + (backwards ? -1 : 1) + s->n) % s->n;
}

int nav_step(nav_model *m, int win_id, int backwards, int *focus_out, int *reason_out)
{
    nav_window *win;
    nav_widget *w;
    nav_scope *s;
    int reason = backwards ? TGS_REASON_SHIFT_TAB : TGS_REASON_TAB;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    if (!win || win->root_scope < 0) return 0;

    w = nav_widget_find(m, win->focus);
    if (!win->focus || !w || !nav_widget_focusable(w) || w->scope_idx < 0) {
        /* Nothing valid is focused: the ring's first member takes over. */
        int id = scope_member(m, win->root_scope, backwards);

        if (!id) return 0;
        *focus_out = id;
        *reason_out = win->focus ? reason : TGS_REASON_INIT;
        return 1;
    }
    s = &m->scopes[w->scope_idx];

    if (s->kind == 2) {
        /* TRAP: Tab wraps inside, it can never leave (§C.1). */
        s->cursor = move_cursor(s, backwards);
        return enter_entry(m, w->scope_idx, s->cursor, reason, focus_out, reason_out);
    }

    if (s->kind == 1) {
        /* GROUP: this Tab leaves the container entirely (§C.1, criterion L10). */
        int idx = entry_of_widget(m, s->parent, win->focus);
        nav_scope *p = &m->scopes[s->parent];

        s->remembered = win->focus;
        if (idx >= 0) p->cursor = idx;
        p->cursor = move_cursor(p, backwards);
        return enter_entry(m, s->parent, p->cursor, reason, focus_out, reason_out);
    }

    s->cursor = move_cursor(s, backwards);
    return enter_entry(m, w->scope_idx, s->cursor, reason, focus_out, reason_out);
}

int nav_edge(nav_model *m, int win_id, int to_last, int *focus_out, int *reason_out)
{
    nav_window *win;
    nav_widget *w;
    nav_scope *s;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    if (!win || win->root_scope < 0) return 0;

    w = nav_widget_find(m, win->focus);
    if (!win->focus || !w || !nav_widget_focusable(w) || w->scope_idx < 0) {
        int id = scope_member(m, win->root_scope, to_last);

        if (!id) return 0;
        *focus_out = id;
        *reason_out = win->focus ? TGS_REASON_ARROW : TGS_REASON_INIT;
        return 1;
    }
    s = &m->scopes[w->scope_idx];
    s->cursor = to_last ? s->n - 1 : 0;
    return enter_entry(m, w->scope_idx, s->cursor, TGS_REASON_ARROW,
                       focus_out, reason_out);
}

int nav_ring_handles(nav_model *m, int win_id, void **handles, int max)
{
    nav_window *win;
    nav_widget *w;
    nav_scope *s;
    int scope_idx, i, n = 0;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    if (!win || win->root_scope < 0) return 0;

    w = nav_widget_find(m, win->focus);
    scope_idx = (w && w->scope_idx >= 0) ? w->scope_idx : win->root_scope;
    s = &m->scopes[scope_idx];

    for (i = 0; i < s->n && n < max; i++) {
        nav_widget *mw = nav_widget_find(m, entry_member(m, scope_idx, i, NULL));

        if (mw && mw->handle) handles[n++] = mw->handle;
    }
    return n;
}

int nav_all_handles(nav_model *m, int win_id, void **handles, int max)
{
    int i, n = 0;

    nav_rebuild(m);
    for (i = 0; i < m->widget_count && n < max; i++) {
        nav_widget *w = &m->widgets[i];

        if (!w->used || w->win_id != win_id) continue;
        if (!nav_widget_focusable(w) || !w->handle) continue;
        handles[n++] = w->handle;
    }
    return n;
}

int nav_set_focus(nav_model *m, int win_id, int widget_id)
{
    nav_window *win = nav_window_find(m, win_id);
    int prev;

    if (!win) return 0;
    prev = win->focus;
    win->focus = widget_id;
    if (widget_id) win->last_focus = widget_id;
    nav_rebuild(m);
    sync_cursors(m, win);
    return prev;
}

int nav_restore(nav_model *m, int win_id, int *focus_out)
{
    nav_window *win;
    nav_widget *w;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    if (!win) return 0;

    w = nav_widget_find(m, win->last_focus);
    if (w && w->win_id == win_id && nav_widget_focusable(w)) {
        *focus_out = w->id;
        return 1;
    }
    *focus_out = scope_member(m, win->root_scope, 0);
    return 0;
}

int nav_successor(nav_model *m, int win_id, int widget_id)
{
    nav_window *win;
    nav_widget *w;
    nav_scope *s;
    int idx, id;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    w = nav_widget_find(m, widget_id);
    if (!win || !w || w->scope_idx < 0) return 0;

    s = &m->scopes[w->scope_idx];
    idx = entry_of_widget(m, w->scope_idx, widget_id);
    if (idx < 0) return 0;

    if (idx + 1 < s->n) {
        id = entry_member(m, w->scope_idx, idx + 1, NULL);
        if (id && id != widget_id) return id;
    }
    if (idx - 1 >= 0) {
        id = entry_member(m, w->scope_idx, idx - 1, NULL);
        if (id && id != widget_id) return id;
    }
    return 0;
}

int nav_first_focusable(nav_model *m, int win_id)
{
    nav_window *win;

    nav_rebuild(m);
    win = nav_window_find(m, win_id);
    if (!win || win->root_scope < 0) return 0;
    return scope_member(m, win->root_scope, 0);
}

int nav_other_window(nav_model *m, int win_id)
{
    int i;

    /* Topmost first: windows are created bottom-up (§G.2). */
    for (i = m->window_count - 1; i >= 0; i--) {
        if (!m->windows[i].used || m->windows[i].win_id == win_id) continue;
        if (nav_first_focusable(m, m->windows[i].win_id))
            return m->windows[i].win_id;
    }
    for (i = m->window_count - 1; i >= 0; i--) {
        if (m->windows[i].used && m->windows[i].win_id != win_id)
            return m->windows[i].win_id;
    }
    return 0;
}
