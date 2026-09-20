/*
 * TGS Window Manager — frame dispatch and event routing (SVG semantics).
 *
 * The WM is thin: it maps program frames to backend element calls, routes
 * the backend's input events back to the program (or to the IME program),
 * and reports canvas resize. There is no window, focus, navigation, layout,
 * subscription, or geometry logic — the program owns all of those.
 */
#include "window_manager.h"
#include "nav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Canonical TGS key codes and modifier mask (same space input_sdl.c /
 * input_fb.c produce). */
#define TGS_KEY_TAB    9
#define TGS_KEY_LEFT   1000
#define TGS_KEY_RIGHT  1001
#define TGS_KEY_UP     1002
#define TGS_KEY_DOWN   1003

#define TGS_MOD_SHIFT  0x01
#define TGS_MOD_CTRL   0x02
#define TGS_MOD_ALT    0x04

/* Forward a key frame to a target stream (the program, or the IME program
 * when connected — the IME is the input front end; it commits text back). */
static void forward_key_to(window_manager *wm, int fd, int key, int mods)
{
    char key_str[16], mod_str[16];
    const char *kargs[2];

    snprintf(key_str, sizeof(key_str), "%d", key);
    snprintf(mod_str, sizeof(mod_str), "%d", mods);
    kargs[0] = key_str;
    kargs[1] = mod_str;
    wm->frame_counter++;
    tgs_frame_write(fd, TGS_STREAM_EVENT, wm->frame_counter,
                    TGS_CMD_EVT_KEY, kargs, 2);
}

static tgs_widget_type str_to_widget_type(const char *s)
{
    if (strcmp(s, "text") == 0)     return TGS_WIDGET_TEXT;
    if (strcmp(s, "box") == 0)      return TGS_WIDGET_BOX;
    if (strcmp(s, "graphic") == 0)  return TGS_WIDGET_GRAPHIC;
    /* Retired names: button was a box+text composition; input is text the
     * program edits; checkbox/slider/scroll are compositions. */
    if (strcmp(s, "button") == 0 || strcmp(s, "input") == 0 ||
        strcmp(s, "checkbox") == 0 || strcmp(s, "radio") == 0 ||
        strcmp(s, "switch") == 0 || strcmp(s, "slider") == 0 ||
        strcmp(s, "scroll") == 0 || strcmp(s, "container") == 0 ||
        strcmp(s, "list") == 0 || strcmp(s, "table") == 0 ||
        strcmp(s, "menu") == 0 || strcmp(s, "tab") == 0 ||
        strcmp(s, "dropdown") == 0 || strcmp(s, "image") == 0)
        return TGS_WIDGET_BOX;
    if (strcmp(s, "label") == 0)    return TGS_WIDGET_TEXT;
    /* Same numeric encoding as the client sends */
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        switch (v) {
        case 0: case 2: case 3: case 5: case 8:
            return TGS_WIDGET_BOX;
        case 1: return TGS_WIDGET_TEXT;
        case 16: return TGS_WIDGET_GRAPHIC;
        default: break;
        }
    }
    return TGS_WIDGET_BOX;
}

void wm_init(window_manager *wm, tgs_backend *backend, int pty_fd, int ime_pty_fd)
{
    memset(wm, 0, sizeof(*wm));
    wm->backend = backend;
    wm->pty_fd = pty_fd;
    wm->ime_pty_fd = ime_pty_fd;
    nav_init(&wm->nav);
}

void wm_set_display_size(window_manager *wm, int w, int h)
{
    wm->disp_w = w;
    wm->disp_h = h;
}

static void emit_resize(window_manager *wm)
{
    char w_str[16], h_str[16];
    const char *args[2];

    snprintf(w_str, sizeof(w_str), "%d", wm->disp_w);
    snprintf(h_str, sizeof(h_str), "%d", wm->disp_h);
    args[0] = w_str;
    args[1] = h_str;
    wm->frame_counter++;
    tgs_frame_write(wm->pty_fd, TGS_STREAM_COMMAND, wm->frame_counter,
                    TGS_CMD_NTF_RESIZE, args, 2);
}

void wm_handle_frame(const tgs_frame *frame, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    tgs_backend *be = wm->backend;

    if (!be) return;
    frame = frame;
    switch (frame->command) {
    case TGS_CMD_HELLO: {
        /* The handshake is program-initiated: the compositor must never write
         * to a stream whose program has not asked for TGS. */
        const char *ready_args[1] = { TGS_CAPS_LAYER0 };
        int is_ime = (frame->num_args >= 2 &&
                      strstr(frame->args[1], "ime=true") != NULL);
        int fd = is_ime ? wm->ime_pty_fd : wm->pty_fd;

        if (is_ime) wm->ime_connected = 1;
        else        wm->hello_received = 1;
        if (fd >= 0)
            tgs_frame_write(fd, TGS_STREAM_HANDSHAKE, 2,
                            TGS_CMD_READY, ready_args, 1);
        break;
    }

    case TGS_CMD_WGT_CREATE: {
        /* args: [id, type, parent, x, y, w, h] — parent 0 = canvas root. */
        int id, parent_id, x, y, w, h;
        tgs_widget_type wtype;
        nav_widget *pw = NULL;
        void *parent = NULL;
        void *handle;

        if (frame->num_args < 7) break;
        id = atoi(frame->args[0]);
        wtype = str_to_widget_type(frame->args[1]);
        parent_id = atoi(frame->args[2]);
        x = atoi(frame->args[3]);
        y = atoi(frame->args[4]);
        w = atoi(frame->args[5]);
        h = atoi(frame->args[6]);

        if (parent_id != 0) {
            pw = nav_widget_find(&wm->nav, parent_id);
            if (!pw || !nav_type_is_container(pw->type)) {
                fprintf(stderr, "wm: WGT_CREATE rejected — parent %d is not a "
                        "container\n", parent_id);
                break;
            }
            parent = pw->handle;
        }

        handle = be->create_element(parent, wtype);
        if (!handle) break;
        be->set_element_rect(handle, x, y, w, h);

        nav_add_widget(&wm->nav, id, parent_id ? pw->win_id : 0, parent_id,
                       parent_id == 0, wtype, handle);
        break;
    }

    case TGS_CMD_WGT_UPDATE: {
        /* args: [id, value] */
        nav_widget *w;

        if (frame->num_args < 2) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) be->set_element_content(w->handle, frame->args[1]);
        break;
    }

    case TGS_CMD_WGT_STYLE: {
        /* args: [id, prop, value] */
        nav_widget *w;

        if (frame->num_args < 3) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) be->set_element_style(w->handle, (tgs_style_prop)atoi(frame->args[1]),
                                     (int32_t)atoi(frame->args[2]));
        break;
    }

    case TGS_CMD_WGT_DESTROY: {
        /* args: [id] */
        nav_widget *w;

        if (frame->num_args < 1) break;
        w = nav_widget_find(&wm->nav, atoi(frame->args[0]));
        if (w) {
            be->destroy_element(w->handle);
            nav_remove_widget(&wm->nav, w->id);
        }
        break;
    }

    case TGS_CMD_IME_COMMIT: {
        /* args: [text] — the IME program committed text; deliver it to the
         * program as an IME_COMMIT event. */
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_IME_COMMIT, (const char *[]){ frame->args[0] ? frame->args[0] : "" }, 1);
        break;
    }

    default:
        break;
    }
}

void wm_backend_event(void *element_handle, tgs_event_type type,
                      const char *event_data, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    nav_widget *w = nav_widget_by_handle(&wm->nav, element_handle);
    char id_str[16];

    if (!w) return;
    snprintf(id_str, sizeof(id_str), "%d", w->id);
    wm->frame_counter++;

    switch (type) {
    case TGS_EVENT_CLICK: {
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_CLICK, &id_str, 1);
        break;
    }
    case TGS_EVENT_HOVER_ENTER: {
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_HOVER_ENTER, &id_str, 1);
        break;
    }
    case TGS_EVENT_HOVER_LEAVE: {
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_HOVER_LEAVE, &id_str, 1);
        break;
    }
    case TGS_EVENT_POINTER: {
        /* event_data = "x;y;phase" */
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT, wm->frame_counter,
                        TGS_CMD_EVT_POINTER,
                        &(const char *){ event_data ? event_data : "0;0;0" }, 1);
        break;
    }
    case TGS_EVENT_KEY: {
        /* Keys go to the IME program when connected (it is the input front
         * end); otherwise to the program. */
        int fd = (wm->ime_connected && wm->ime_pty_fd >= 0)
                     ? wm->ime_pty_fd : wm->pty_fd;
        int key = 0, mods = 0;

        if (event_data) {
            const char *semi = strchr(event_data, ';');
            if (semi) {
                key = atoi(event_data);
                mods = atoi(semi + 1);
            } else {
                key = atoi(event_data);
            }
        }
        forward_key_to(wm, fd, key, mods);
        break;
    }
    default:
        break;
    }
}

void wm_flush_geometry(window_manager *wm)
{
    /* Layout is program policy — no geometry is ever reported back. */
    (void)wm;
}