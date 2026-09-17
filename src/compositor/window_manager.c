/*
 * TGS Window Manager Implementation
 * Protocol frame dispatch, focus authority, event routing.
 *
 * The compositor owns focus (docs/navigation.md §A.1): nav.c holds the widget
 * tree, the effective attributes, the rings and the per-window focus registry;
 * this file turns protocol frames into those model changes plus backend calls,
 * and turns backend events into protocol frames. The backend executes the
 * focus moves and reports the resulting change back, so NTF_FOCUS is emitted
 * from exactly one place (§F.1). Id 83 (EVT_FOCUS) is retired and never sent.
 */
#include "window_manager.h"
#include "nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical TGS key codes and modifier mask (docs/navigation.md §D.4) — the
 * same space input_sdl.c/input_fb.c produce and lvgl_backend.c translates. */
#define TGS_KEY_TAB    9
#define TGS_KEY_LEFT   1000
#define TGS_KEY_RIGHT  1001
#define TGS_KEY_UP     1002
#define TGS_KEY_DOWN   1003
#define TGS_KEY_HOME   1004
#define TGS_KEY_END    1005

#define TGS_MOD_SHIFT  0x01
#define TGS_MOD_CTRL   0x02
#define TGS_MOD_ALT    0x04

/* ---- Focus (§E, §F, §G) ---- */

static void emit_focus(window_manager *wm, int win_id, int widget_id,
                       int focused, tgs_focus_reason reason)
{
    char win_str[16], wid_str[16], foc_str[4], reason_str[8];
    const char *args[4];

    snprintf(win_str, sizeof(win_str), "%d", win_id);
    snprintf(wid_str, sizeof(wid_str), "%d", widget_id);
    snprintf(foc_str, sizeof(foc_str), "%d", focused ? 1 : 0);
    snprintf(reason_str, sizeof(reason_str), "%d", (int)reason);

    args[0] = win_str;
    args[1] = wid_str;
    args[2] = foc_str;
    args[3] = reason_str;

    wm->frame_counter++;
    tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                    TGS_CMD_NTF_FOCUS, args, 4);
}

/* Focus the widget in LVGL and remember the reason for the report the backend
 * will send back (§F.1 keeps the optimistic move from echoing). */
static void backend_set_focus(window_manager *wm, int widget_id,
                             tgs_focus_reason reason)
{
    nav_widget *w = nav_widget_find(&wm->nav, widget_id);

    wm->pending_focus = widget_id;
    wm->pending_reason = (int)reason;
    if (wm->backend->set_focus)
        wm->backend->set_focus(w ? w->handle : NULL);
}

/* §H.3 rule 2: composition is cancelled, never auto-committed, before focus
 * leaves an IME-armed widget. */
static void ime_cancel(window_manager *wm, int win_id, int widget_id)
{
    nav_widget *w = nav_widget_find(&wm->nav, widget_id);
    char win_str[16], wid_str[16];
    const char *args[2];

    if (!w || w->type != TGS_WIDGET_INPUT) return;
    if (!wm->ime_connected || wm->ime_pty_fd < 0) return;

    snprintf(win_str, sizeof(win_str), "%d", win_id);
    snprintf(wid_str, sizeof(wid_str), "%d", widget_id);
    args[0] = win_str;
    args[1] = wid_str;

    wm->frame_counter++;
    tgs_frame_write(wm->ime_pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                    TGS_CMD_IME_CANCEL, args, 2);
}

/* The single focus-change path: emit the pair (§F.1), update the registry and
 * move LVGL's focus. Identical (win, widget) in a row emits nothing. */
static void focus_commit(window_manager *wm, int win_id, int widget_id,
                         tgs_focus_reason reason)
{
    nav_window *win = nav_window_find(&wm->nav, win_id);
    int old;

    if (!win) return;
    old = win->focus;
    if (old == widget_id) {
        wm->pending_focus = 0;
        return;
    }

    if (old) ime_cancel(wm, win_id, old);
    nav_set_focus(&wm->nav, win_id, widget_id);

    if (old) emit_focus(wm, win_id, old, 0, reason);
    if (widget_id) emit_focus(wm, win_id, widget_id, 1, reason);

    if (widget_id && win_id == wm->nav.active_win) {
        backend_set_focus(wm, widget_id, reason);
    } else {
        wm->pending_focus = 0;
    }
}

/* Clear the registry without emitting — the caller owns the lost frame. */
static void focus_forget(window_manager *wm, int win_id)
{
    nav_set_focus(&wm->nav, win_id, 0);
    wm->pending_focus = 0;
}

/* §G.2: switch the keyboard to `win_id`, hand focus to `target` (0 = the
 * remembered widget, else the first ring member). The window that loses the
 * keyboard gets its lost frame with the same reason. */
static void activate_window(window_manager *wm, int win_id,
                            tgs_focus_reason reason, int target)
{
    nav_window *win = nav_window_find(&wm->nav, win_id);
    int prev = wm->nav.active_win;

    if (!win) return;

    if (prev != win_id) {
        nav_window *pw = nav_window_find(&wm->nav, prev);

        if (pw && pw->focus) {
            emit_focus(wm, prev, pw->focus, 0, reason);
            focus_forget(wm, prev);
        }
        wm->nav.active_win = win_id;
        if (wm->backend->set_active_window)
            wm->backend->set_active_window(win->handle);
    }

    if (!target) nav_restore(&wm->nav, win_id, &target);
    if (!target) return;
    if (win->focus == target)
        backend_set_focus(wm, target, reason); /* registry knew, LVGL did not */
    else
        focus_commit(wm, win_id, target, reason);
}

/* Enroll a window's focusable widgets in its LVGL group (§I.1). */
static void push_ring(window_manager *wm, int win_id)
{
    nav_window *win = nav_window_find(&wm->nav, win_id);
    void *handles[NAV_MAX_WIDGETS];
    int n;

    if (!win || !wm->backend->set_window_ring) return;
    n = nav_all_handles(&wm->nav, win_id, handles, NAV_MAX_WIDGETS);
    wm->backend->set_window_ring(win->handle, handles, n);
}

static void enroll_widget(window_manager *wm, nav_widget *w)
{
    if (!w || !wm->backend->set_widget_focusable) return;
    wm->backend->set_widget_focusable(w->handle, nav_widget_focusable(w));
}

/* ---- Navigation bindings (§D.2, §D.3) ---- */

static int nav_move(window_manager *wm, nav_window *win, int backwards)
{
    int target = 0;
    int reason = TGS_REASON_TAB;

    if (!nav_step(&wm->nav, win->win_id, backwards, &target, &reason)) return 0;
    focus_commit(wm, win->win_id, target, (tgs_focus_reason)reason);
    return 1;
}

static int nav_edge_move(window_manager *wm, nav_window *win, int to_last)
{
    int target = 0;
    int reason = TGS_REASON_ARROW;

    if (!nav_edge(&wm->nav, win->win_id, to_last, &target, &reason)) return 0;
    focus_commit(wm, win->win_id, target, (tgs_focus_reason)reason);
    return 1;
}

/* Spatial arrow navigation inside the current ring (§D.3 rule 3) — the
 * geometry lives in the backend, the candidate set and the outcome here. */
static int nav_dir_move(window_manager *wm, nav_window *win, tgs_nav_dir dir)
{
    void *cands[NAV_RING_MAX];
    nav_widget *from = nav_widget_find(&wm->nav, win->focus);
    nav_widget *to;
    void *hit;
    int n;

    if (!from || !wm->backend->focus_dir) return 0;
    n = nav_ring_handles(&wm->nav, win->win_id, cands, NAV_RING_MAX);
    if (n <= 0) return 0;

    hit = wm->backend->focus_dir(from->handle, cands, n, dir);
    if (!hit) return 0;
    to = nav_widget_by_handle(&wm->nav, hit);
    if (!to || to->id == from->id) return 0;

    focus_commit(wm, win->win_id, to->id, TGS_REASON_ARROW);
    return 1;
}

/* Residual forward (§D.3 rule 4): a key that was neither consumed by the
 * widget nor usable as navigation still belongs to the app. The compositor
 * sends it itself instead of letting LVGL hand it to the widget — an object's
 * own key handling can swallow it before any app-visible event exists. */
static void forward_key(window_manager *wm, nav_widget *w, int key, int mods)
{
    /* KEY is input transport, not a subscription notification: residual keys
     * always reach the app, never gated by EVT_BIND (D6). */
    if (!w) return;
    int target_fd = wm->ime_connected && wm->ime_pty_fd >= 0 &&
                    w->type == TGS_WIDGET_INPUT
                        ? wm->ime_pty_fd
                        : wm->pty_fd;
    char win_str[16], wid_str[16], key_str[16], mod_str[16];
    const char *args[4];

    snprintf(win_str, sizeof(win_str), "%d", w->win_id);
    snprintf(wid_str, sizeof(wid_str), "%d", w->id);
    snprintf(key_str, sizeof(key_str), "%d", key);
    snprintf(mod_str, sizeof(mod_str), "%d", mods);
    args[0] = win_str;
    args[1] = wid_str;
    args[2] = key_str;
    args[3] = mod_str;

    wm->frame_counter++;
    tgs_frame_write(target_fd, TGS_STREAM_EVENT, wm->frame_counter,
                    TGS_CMD_EVT_KEY, args, 4);
}

/* Precedence hook (design §D.3), called by the backend for every key edge
 * before LVGL sees it: the compositor decides whether the key is navigation
 * for the focused widget, a hand-off to that widget, or neither. */
static tgs_nav_key_action wm_nav_key(int key, int mods, int pressed, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    nav_window *win;
    nav_widget *w;
    int consume = 0;

    if (!wm) return TGS_NAV_PASS;

    if (!pressed) {
        /* Drop the release of a press navigation consumed. */
        if (key == wm->consumed_key && mods == wm->consumed_mods) {
            wm->consumed_key = -1;
            return TGS_NAV_CONSUMED;
        }
        return TGS_NAV_PASS;
    }

    win = nav_window_find(&wm->nav, wm->nav.active_win);
    w = win ? nav_widget_find(&wm->nav, win->focus) : NULL;
    if (!win || !w || !nav_widget_focusable(w)) return TGS_NAV_PASS;

    switch (key) {
    case TGS_KEY_TAB:
        /* Ctrl+Tab is the escape hatch out of a NAV_TAB=1 widget (§D.2). */
        if (mods & TGS_MOD_CTRL) {
            nav_move(wm, win, 0);
            consume = 1;
        } else if (nav_widget_consumes_tab(w)) {
            /* Rule 1: the widget opted out — it takes Tab as text, and the
             * backend delivers it straight to the widget (rule 2). */
            return TGS_NAV_WIDGET;
        } else {
            consume = nav_move(wm, win, (mods & TGS_MOD_SHIFT) ? 1 : 0);
        }
        break;

    case TGS_KEY_LEFT:
    case TGS_KEY_RIGHT:
    case TGS_KEY_UP:
    case TGS_KEY_DOWN:
        if (nav_widget_consumes_arrows(w)) return TGS_NAV_PASS;
        consume = nav_dir_move(wm, win, key == TGS_KEY_LEFT ? TGS_NAV_LEFT :
                                         key == TGS_KEY_RIGHT ? TGS_NAV_RIGHT :
                                         key == TGS_KEY_UP ? TGS_NAV_UP :
                                         TGS_NAV_DOWN);
        if (!consume) {
            forward_key(wm, w, key, mods);
            consume = 1;
        }
        break;

    case TGS_KEY_HOME:
    case TGS_KEY_END:
        if (nav_widget_consumes_arrows(w)) return TGS_NAV_PASS;
        consume = nav_edge_move(wm, win, key == TGS_KEY_END);
        if (!consume) {
            forward_key(wm, w, key, mods);
            consume = 1;
        }
        break;

    default:
        return TGS_NAV_PASS;
    }

    if (!consume) return TGS_NAV_PASS; /* residual forward (§D.3 rule 4) */
    wm->consumed_key = key;
    wm->consumed_mods = mods;
    return TGS_NAV_CONSUMED;
}

/* ---- Protocol dispatch ---- */

void wm_init(window_manager *wm, tgs_backend *backend, int pty_fd, int ime_pty_fd)
{
    wm->backend = backend;
    wm->pty_fd = pty_fd;
    wm->ime_pty_fd = ime_pty_fd;
    wm->frame_counter = 0;
    wm->disp_w = 0;
    wm->disp_h = 0;
    wm->hello_received = 0;
    wm->ime_connected = 0;
    wm->pending_focus = 0;
    wm->pending_reason = TGS_REASON_NONE;
    wm->consumed_key = -1;
    wm->consumed_mods = 0;
    wm->geom_pending = 0;

    nav_init(&wm->nav);
    if (backend->set_nav_key_cb) backend->set_nav_key_cb(wm_nav_key, wm);
}

/* Keep the compositor's cached display size in sync with the real surface.
 * Called at init (so NTF_RESIZE at the first WIN_CREATE is correct) and on
 * every resize (main.c term_resize_to). NTF_RESIZE reports these dimensions
 * to the app — a stale cache makes every later window report wrong w/h. */
void wm_set_display_size(window_manager *wm, int w, int h)
{
    if (!wm) return;
    wm->disp_w = w;
    wm->disp_h = h;
}

static void send_resize(window_manager *wm, int win_id)
{
    char win_id_str[16];
    char w_str[16];
    char h_str[16];
    const char *resize_args[5];

    snprintf(win_id_str, sizeof(win_id_str), "%d", win_id);
    snprintf(w_str, sizeof(w_str), "%d", wm->disp_w);
    snprintf(h_str, sizeof(h_str), "%d", wm->disp_h);
    resize_args[0] = win_id_str;
    resize_args[1] = "0";
    resize_args[2] = "0";
    resize_args[3] = w_str;
    resize_args[4] = h_str;
    wm->frame_counter++;
    tgs_frame_write(wm->pty_fd, TGS_STREAM_COMMAND,
                    wm->frame_counter, TGS_CMD_NTF_RESIZE,
                    resize_args, 5);
}

/* Emit one NTF_GEOMETRY for `w` using the backend's actual geometry. The
 * frame format mirrors NTF_RESIZE (5 int args) but carries a widget id. */
static void emit_widget_geometry(window_manager *wm, nav_widget *w)
{
    char id_str[16], x_str[16], y_str[16], w_str[16], h_str[16];
    const char *args[5];
    int x, y, gw, gh;

    if (!wm->backend->widget_geometry ||
        wm->backend->widget_geometry(w->handle, &x, &y, &gw, &gh) != 0)
        return;

    snprintf(id_str, sizeof(id_str), "%d", w->id);
    snprintf(x_str, sizeof(x_str), "%d", x);
    snprintf(y_str, sizeof(y_str), "%d", y);
    snprintf(w_str, sizeof(w_str), "%d", gw);
    snprintf(h_str, sizeof(h_str), "%d", gh);
    args[0] = id_str;
    args[1] = x_str;
    args[2] = y_str;
    args[3] = w_str;
    args[4] = h_str;
    wm->frame_counter++;
    tgs_frame_write(wm->pty_fd, TGS_STREAM_COMMAND,
                    wm->frame_counter, TGS_CMD_NTF_GEOMETRY, args, 5);
}

/* Emit NTF_GEOMETRY for every layout container and its directly-laid-out
 * children. Called by the main loop after the backend tick, so flex/grid
 * positions are the settled ones. Only runs when geom_pending is set, so a
 * program that never touches layout containers sees no new traffic. */
void wm_flush_geometry(window_manager *wm)
{
    nav_model *m = &wm->nav;
    int wi, i;

    if (!wm->geom_pending) return;
    if (!wm->backend->widget_geometry) {
        wm->geom_pending = 0;
        return;
    }

    for (wi = 0; wi < m->window_count; wi++) {
        int win_id = m->windows[wi].win_id;

        /* 1st pass: each container reports its own real geometry. */
        for (i = 0; i < m->widget_count; i++) {
            nav_widget *w = &m->widgets[i];

            if (w->used && w->win_id == win_id &&
                nav_type_is_container(w->type))
                emit_widget_geometry(wm, w);
        }
        /* 2nd pass: children placed by a container report theirs. */
        for (i = 0; i < m->widget_count; i++) {
            nav_widget *w = &m->widgets[i];
            nav_widget *parent;

            if (!w->used || w->win_id != win_id || w->parent_is_window)
                continue;
            parent = nav_widget_find(m, w->parent);
            if (parent && nav_type_is_container(parent->type))
                emit_widget_geometry(wm, w);
        }
    }
    wm->geom_pending = 0;
}

static tgs_widget_type str_to_widget_type(const char *s)
{
    if (strcmp(s, "button") == 0)   return TGS_WIDGET_BUTTON;
    if (strcmp(s, "label") == 0)    return TGS_WIDGET_LABEL;
    if (strcmp(s, "input") == 0)    return TGS_WIDGET_INPUT;
    if (strcmp(s, "checkbox") == 0) return TGS_WIDGET_CHECKBOX;
    if (strcmp(s, "radio") == 0)    return TGS_WIDGET_RADIO;
    if (strcmp(s, "slider") == 0)   return TGS_WIDGET_SLIDER;
    if (strcmp(s, "progress") == 0) return TGS_WIDGET_PROGRESS;
    if (strcmp(s, "switch") == 0)   return TGS_WIDGET_SWITCH;
    if (strcmp(s, "vlayout") == 0)  return TGS_WIDGET_VLAYOUT;
    if (strcmp(s, "hlayout") == 0)  return TGS_WIDGET_HLAYOUT;
    if (strcmp(s, "glayout") == 0)  return TGS_WIDGET_GLAYOUT;
    if (strcmp(s, "scroll") == 0)   return TGS_WIDGET_SCROLL;
    if (strcmp(s, "list") == 0)     return TGS_WIDGET_LIST;
    if (strcmp(s, "table") == 0)    return TGS_WIDGET_TABLE;
    if (strcmp(s, "menu") == 0)     return TGS_WIDGET_MENU;
    if (strcmp(s, "tab") == 0)      return TGS_WIDGET_TAB;
    if (strcmp(s, "dropdown") == 0) return TGS_WIDGET_DROPDOWN;
    if (strcmp(s, "image") == 0)    return TGS_WIDGET_IMAGE;
    if (strcmp(s, "timepick") == 0) return TGS_WIDGET_TIMEPICK;
    if (strcmp(s, "datepick") == 0) return TGS_WIDGET_DATEPICK;
    /* Same numeric encoding as str_to_window_type: the client sends
     * int_to_str((int)type), so digits must decode too. */
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);

        if (v >= 0 && v < (int)TGS_WIDGET_COUNT) return (tgs_widget_type)v;
    }
    return TGS_WIDGET_BUTTON;
}

static tgs_window_type str_to_window_type(const char *s)
{
    if (strcmp(s, "dialog") == 0)      return TGS_WINDOW_DIALOG;
    if (strcmp(s, "fullscreen") == 0)  return TGS_WINDOW_FULLSCREEN;
    if (strcmp(s, "tool") == 0)        return TGS_WINDOW_TOOL;
    /* Same numeric encoding as str_to_widget_type: the client sends
     * int_to_str((int)type), so digits must decode too. */
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);

        if (v >= 0 && v <= (int)TGS_WINDOW_TOOL)
            return (tgs_window_type)v;
    }
    return TGS_WINDOW_NORMAL;
}

void wm_handle_frame(const tgs_frame *frame, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    tgs_backend *be = wm->backend;

    switch (frame->command) {
    case TGS_CMD_HELLO: {
        /* The handshake is program-initiated: the compositor must never write
         * to a stream whose program has not asked for TGS, or a plain
         * character program would find protocol bytes on its stdin. The
         * program speaks first; the compositor answers READY. */
        const char *ready_args[1] = { TGS_CAPS_LAYER0 };
        int is_ime = (frame->num_args >= 2 &&
                      strstr(frame->args[1], "ime=true") != NULL);

        if (is_ime) {
            wm->ime_connected = 1;
            if (wm->ime_pty_fd >= 0)
                tgs_frame_write(wm->ime_pty_fd, TGS_STREAM_HANDSHAKE, 2,
                                TGS_CMD_READY, ready_args, 1);
        } else {
            wm->hello_received = 1;
            tgs_frame_write(wm->pty_fd, TGS_STREAM_HANDSHAKE, 2,
                            TGS_CMD_READY, ready_args, 1);
        }
        break;
    }

    case TGS_CMD_WIN_CREATE: {
        /* args: [win_id, type, title] */
        int win_id;
        tgs_window_type wtype;
        void *handle;

        if (frame->num_args < 3) break;
        win_id = atoi(frame->args[0]);
        wtype = str_to_window_type(frame->args[1]);
        handle = be->create_window(wtype, frame->args[2]);
        if (!handle) break;

        nav_add_window(&wm->nav, win_id, wtype, handle);
        send_resize(wm, win_id);
        /* Newest window is topmost and takes the keyboard (§G.2). */
        activate_window(wm, win_id, TGS_REASON_WINDOW_ACTIVATE, 0);
        break;
    }

    case TGS_CMD_WIN_DESTROY: {
        /* args: [win_id] */
        nav_window *win;
        void *handle;
        int win_id, next;

        if (frame->num_args < 1) break;
        win_id = atoi(frame->args[0]);
        win = nav_window_find(&wm->nav, win_id);
        if (!win) break;
        handle = win->handle;

        /* Destroy is hide plus registry removal (§G.2): the app that owned the
         * window sees its widget lose focus. */
        if (win->focus) emit_focus(wm, win_id, win->focus, 0, TGS_REASON_HIDDEN);
        focus_forget(wm, win_id);
        nav_remove_window(&wm->nav, win_id);
        be->destroy_window(handle);

        next = nav_other_window(&wm->nav, 0);
        if (next) activate_window(wm, next, TGS_REASON_WINDOW_RESTORE, 0);
        break;
    }

    case TGS_CMD_WGT_CREATE: {
        /* args: [widget_id, parent_id, type, x, y, w, h, content] */
        int wid, parent_id, x, y, w, h, win_id = -1;
        tgs_widget_type wtype;
        nav_window *pwin;
        nav_widget *pw = NULL, *nw;
        void *parent = NULL;
        void *handle;

        if (frame->num_args < 8) break;
        wid = atoi(frame->args[0]);
        parent_id = atoi(frame->args[1]);
        wtype = str_to_widget_type(frame->args[2]);
        x = atoi(frame->args[3]);
        y = atoi(frame->args[4]);
        w = atoi(frame->args[5]);
        h = atoi(frame->args[6]);

        /* A window is always a valid parent; a widget parent must be an
         * explicit container. */
        pwin = nav_window_find(&wm->nav, parent_id);
        if (pwin) {
            parent = pwin->handle;
            win_id = parent_id;
        } else {
            pw = nav_widget_find(&wm->nav, parent_id);
            if (!pw || !nav_type_is_container(pw->type)) {
                fprintf(stderr,
                        "wm: WGT_CREATE rejected — parent %d is neither a window "
                        "nor a container, cannot parent widget %d\n",
                        parent_id, wid);
                break;
            }
            parent = pw->handle;
            win_id = pw->win_id;
        }

        handle = be->create_widget(parent, wtype);
        if (!handle) break;
        be->set_widget_rect(handle, x, y, w, h);
        /* Content is always present in the frame; an empty string means an
         * empty widget, so it must still be applied — otherwise LVGL's
         * placeholder text (LV_LABEL_DEFAULT_TEXT) leaks through. */
        be->set_widget_content(handle, frame->args[7]);

        nav_add_widget(&wm->nav, wid, win_id, parent_id, pwin ? 1 : 0, wtype,
                       handle);
        nw = nav_widget_find(&wm->nav, wid);
        enroll_widget(wm, nw);
        push_ring(wm, win_id);

        /* §E: the first focusable widget of a window whose focus is "none"
         * takes focus with reason INIT. */
        if (nw && win_id == wm->nav.active_win) {
            nav_window *win = nav_window_find(&wm->nav, win_id);

            if (win && win->focus == 0 && nav_widget_focusable(nw))
                focus_commit(wm, win_id, wid, TGS_REASON_INIT);
        }
        /* A new container — or anything parented to one — changes the layout;
         * emit real geometry once LVGL has settled it (next flush). */
        if (nav_type_is_container(wtype) || pw)
            wm->geom_pending = 1;
        break;
    }

    case TGS_CMD_WGT_UPDATE: {
        /* args: [widget_id, content] */
        nav_widget *w;

        if (frame->num_args < 2) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) be->set_widget_content(w->handle, frame->args[1]);
        break;
    }

    case TGS_CMD_WGT_STYLE: {
        /* args: [widget_id, prop, value] — appearance only (§J.2). */
        nav_widget *w;

        if (frame->num_args < 3) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) be->set_widget_style(w->handle, (tgs_style_prop)atoi(frame->args[1]),
                                    (int32_t)atoi(frame->args[2]));
        break;
    }

    case TGS_CMD_WGT_LAYOUT: {
        /* args: [widget_id, layout_type] — runtime relayout of a container. */
        nav_widget *w;

        if (frame->num_args < 2) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) {
            if (be->set_widget_layout)
                be->set_widget_layout(w->handle,
                                      (tgs_layout_type)atoi(frame->args[1]));
            if (nav_type_is_container(w->type))
                wm->geom_pending = 1;
        }
        break;
    }

    case TGS_CMD_WGT_DESTROY: {
        /* args: [widget_id] */
        nav_widget *w;
        nav_window *win;
        int wid, win_id, succ = 0, focused;

        if (frame->num_args < 1) break;
        wid = atoi(frame->args[0]);
        w = nav_widget_find(&wm->nav, wid);
        if (!w) break;

        win_id = w->win_id;
        win = nav_window_find(&wm->nav, win_id);
        focused = (win && win->focus == wid);
        /* Successor is picked while the corpse is still in the ring (§G.2). */
        if (focused) succ = nav_successor(&wm->nav, win_id, wid);
        {
            void *handle = w->handle;

            nav_remove_widget(&wm->nav, wid);
            be->destroy_widget(handle);
        }
        push_ring(wm, win_id);

        if (focused) {
            nav_widget *sw;

            emit_focus(wm, win_id, wid, 0, TGS_REASON_DESTROYED);
            focus_forget(wm, win_id);
            sw = nav_widget_find(&wm->nav, succ);
            if (sw && nav_widget_focusable(sw))
                focus_commit(wm, win_id, succ, TGS_REASON_DESTROYED);
        }
        break;
    }

    case TGS_CMD_EVT_BIND: {
        /* args: [widget_id, event_type] — subscribe one event type on one
         * widget. The mask gates app delivery of CLICK/VALUE_CHANGED (the
         * subscription notifications); KEY is input transport and always
         * reaches the focused app; binding one widget does not affect others.
         * Focus is NOT gated: NTF_FOCUS (65) is the single focus channel and
         * never requires a binding (§F.2 — the retired EVT_FOCUS (83) was
         * the one that "requires EVT_BIND"). */
        nav_widget *w;
        int wid, ev;

        if (frame->num_args < 2) break;
        wid = atoi(frame->args[0]);
        ev = atoi(frame->args[1]);
        w = nav_widget_find(&wm->nav, wid);
        if (!w) {
            fprintf(stderr, "wm: EVT_BIND ignored — unknown widget %d\n", wid);
            break;
        }
        /* tgs_event_type is a contiguous 0..N enum; anything outside it is
         * a protocol error, not a bit to set. */
        if (ev < 0 || ev > (int)TGS_EVENT_IME_COMMIT) break;
        w->event_mask |= (uint32_t)(1u << ev);
        break;
    }

    case TGS_CMD_SET_FOCUS: {
        /* args: [window_id, widget_id]; 0 clears (§E) */
        nav_window *win;
        nav_widget *w;
        int win_id, wid;

        if (frame->num_args < 2) break;
        win_id = atoi(frame->args[0]);
        wid = atoi(frame->args[1]);
        win = nav_window_find(&wm->nav, win_id);
        if (!win) {
            fprintf(stderr, "wm: SET_FOCUS ignored — unknown window %d\n", win_id);
            break;
        }
        if (wid == 0) {
            focus_commit(wm, win_id, 0, TGS_REASON_PROGRAMMATIC);
            break;
        }
        w = nav_widget_find(&wm->nav, wid);
        if (!w || w->win_id != win_id || !nav_widget_focusable(w)) {
            fprintf(stderr, "wm: SET_FOCUS ignored — widget %d is not focusable "
                            "in window %d\n", wid, win_id);
            break;
        }
        focus_commit(wm, win_id, wid, TGS_REASON_PROGRAMMATIC);
        break;
    }

    case TGS_CMD_WGT_ATTR: {
        /* args: [widget_id, attr, value] (§J.4) */
        nav_widget *w;
        int wid;

        if (frame->num_args < 3) break;
        wid = atoi(frame->args[0]);
        w = nav_widget_find(&wm->nav, wid);
        if (!w) {
            fprintf(stderr, "wm: WGT_ATTR ignored — unknown widget %d\n", wid);
            break;
        }
        if (!nav_attr_set(&wm->nav, wid, (tgs_widget_attr)atoi(frame->args[1]),
                          (int32_t)atoi(frame->args[2]))) {
            fprintf(stderr, "wm: WGT_ATTR ignored — widget %d, attr %s, value %s\n",
                    wid, frame->args[1], frame->args[2]);
            break;
        }
        if (atoi(frame->args[1]) == TGS_ATTR_FOCUSABLE)
            enroll_widget(wm, w);
        push_ring(wm, w->win_id);
        break;
    }

    case TGS_CMD_IME_COMMIT: {
        /* args: [win_id, widget_id, text] — a commit for a widget that no
         * longer has focus is discarded (§H.3 rule 2). */
        nav_widget *w;
        nav_window *win;

        if (frame->num_args < 3) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[1]));
        win = w ? nav_window_find(&wm->nav, w->win_id) : NULL;
        if (!w || !win || win->focus != w->id) break;
        if (be->insert_widget_text) be->insert_widget_text(w->handle, frame->args[2]);
        break;
    }

    case TGS_CMD_IME_PREEDIT:
        /* args: [win_id, widget_id, text, cursor] — the preedit overlay is
         * rendered by the backend (deferred, docs/navigation.md §H.2). */
        break;

    default:
        break;
    }
}

void wm_backend_event(void *widget_handle, tgs_event_type type,
                      const char *event_data, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    nav_widget *w = nav_widget_by_handle(&wm->nav, widget_handle);
    char wid_str[16];

    if (!w) return;
    /* Subscription gate (D6): CLICK and VALUE_CHANGED are subscription
     * notifications — they reach the app only when the widget has bound that
     * event type via EVT_BIND. KEY is input transport and always reaches the
     * focused app (an input widget must receive keystrokes regardless of
     * bindings). FOCUS/BLUR are exempt too: they drive NTF_FOCUS, the single
     * focus channel, which never requires a binding (§F.2). */
    switch (type) {
    case TGS_EVENT_CLICK:
    case TGS_EVENT_VALUE_CHANGED:
        if (!(w->event_mask & (1u << (unsigned)type))) return;
        break;
    default:
        break;
    }
    char win_str[16];

    snprintf(wid_str, sizeof(wid_str), "%d", w->id);
    snprintf(win_str, sizeof(win_str), "%d", w->win_id);
    wm->frame_counter++;

    switch (type) {
    case TGS_EVENT_CLICK: {
        const char *args[2] = {win_str, wid_str};

        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_CLICK, args, 2);
        break;
    }
    case TGS_EVENT_VALUE_CHANGED: {
        const char *args[3] = {win_str, wid_str, event_data ? event_data : ""};

        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_VALUE, args, 3);
        break;
    }
    case TGS_EVENT_FOCUS: {
        /* Reasons, in order (§F.1, §G.2): the move the compositor asked for,
         * the initial focus of a window that had none (LVGL focuses the first
         * member of a group by itself), or a pointer click. */
        nav_window *win = nav_window_find(&wm->nav, w->win_id);
        tgs_focus_reason reason;

        if (!win) break;
        if (win->focus == w->id) {
            /* Already known: a ring rebuild re-reporting the same focus is
             * not a focus change and must not become a POINTER frame. */
            wm->pending_focus = 0;
            break;
        }
        reason = (wm->pending_focus == w->id)
                     ? (tgs_focus_reason)wm->pending_reason
                     : (win->focus == 0 ? TGS_REASON_INIT : TGS_REASON_POINTER);

        wm->pending_focus = 0;
        if (wm->nav.active_win != w->win_id)
            activate_window(wm, w->win_id, reason, w->id);
        else
            focus_commit(wm, w->win_id, w->id, reason);
        break;
    }
    case TGS_EVENT_BLUR:
        /* A bare defocus is not a focus change: the lost frame goes out with
         * its gained counterpart (§F.1), or from the path that caused it
         * (destroy, hide, activation). */
        break;
    case TGS_EVENT_KEY: {
        int target_fd = wm->ime_connected && wm->ime_pty_fd >= 0 &&
                        w->type == TGS_WIDGET_INPUT
                            ? wm->ime_pty_fd
                            : wm->pty_fd;
        char key_buf[16] = "0";
        char mod_buf[16] = "0";
        const char *kargs[4];

        if (event_data) {
            const char *semi = strchr(event_data, ';');

            if (semi) {
                int klen = (int)(semi - event_data);

                if (klen > 0 && klen < (int)sizeof(key_buf) - 1) {
                    memcpy(key_buf, event_data, (size_t)klen);
                    key_buf[klen] = '\0';
                }
                strncpy(mod_buf, semi + 1, sizeof(mod_buf) - 1);
                mod_buf[sizeof(mod_buf) - 1] = '\0';
            } else {
                strncpy(key_buf, event_data, sizeof(key_buf) - 1);
                key_buf[sizeof(key_buf) - 1] = '\0';
            }
        }
        kargs[0] = win_str;
        kargs[1] = wid_str;
        kargs[2] = key_buf;
        kargs[3] = mod_buf;
        tgs_frame_write(target_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_KEY, kargs, 4);
        break;
    }
    default:
        break;
    }
}
