/*
 * TGS Window Manager Implementation
 * ID→handle mapping, protocol frame dispatch, event routing.
 */
#include "window_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIDGETS 256
#define MAX_WINDOWS 16
#define MAX_EVENTS  32

typedef struct {
    int widget_id;
    void *handle;
    tgs_widget_type type;
    int window_id;
} widget_entry;

typedef struct {
    int win_id;
    void *handle;
} window_entry;

static widget_entry widget_map[MAX_WIDGETS];
static int widget_count;

static window_entry window_map[MAX_WINDOWS];
static int window_count;

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
    widget_count = 0;
    window_count = 0;
}

static int widget_add(int id, void *handle, tgs_widget_type type, int window_id)
{
    if (widget_count >= MAX_WIDGETS) return -1;
    widget_map[widget_count].widget_id = id;
    widget_map[widget_count].handle = handle;
    widget_map[widget_count].type = type;
    widget_map[widget_count].window_id = window_id;
    widget_count++;
    return 0;
}

static void widget_remove(int id)
{
    int i;
    for (i = 0; i < widget_count; i++) {
        if (widget_map[i].widget_id == id) {
            widget_map[i] = widget_map[--widget_count];
            return;
        }
    }
}

static void *widget_find(int id)
{
    int i;
    for (i = 0; i < widget_count; i++) {
        if (widget_map[i].widget_id == id) return widget_map[i].handle;
    }
    return NULL;
}

static tgs_widget_type find_widget_type(void *handle)
{
    int i;
    for (i = 0; i < widget_count; i++) {
        if (widget_map[i].handle == handle) return widget_map[i].type;
    }
    return TGS_WIDGET_LABEL;
}

static int find_widget_window_id(void *handle)
{
    int i;
    for (i = 0; i < widget_count; i++) {
        if (widget_map[i].handle == handle) return widget_map[i].window_id;
    }
    return -1;
}

static int window_add(int id, void *handle)
{
    if (window_count >= MAX_WINDOWS) return -1;
    window_map[window_count].win_id = id;
    window_map[window_count].handle = handle;
    window_count++;
    return 0;
}

static void window_remove(int id)
{
    int i;
    for (i = 0; i < window_count; i++) {
        if (window_map[i].win_id == id) {
            window_map[i] = window_map[--window_count];
            return;
        }
    }
}

static void *window_find(int id)
{
    int i;
    for (i = 0; i < window_count; i++) {
        if (window_map[i].win_id == id) return window_map[i].handle;
    }
    return NULL;
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
    /* The client (tgs_client.c) encodes the type numerically via
     * int_to_str((int)type); accept that form too so non-label widgets
     * are not silently collapsed into plain labels. */
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        if (v >= 0 && v < (int)TGS_WIDGET_COUNT)
            return (tgs_widget_type)v;
    }
    return TGS_WIDGET_LABEL;
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
    case TGS_CMD_HELLO:
        wm->hello_received = 1;
        /* Check if this is the IME app (hello args contain "ime=true") */
        if (frame->num_args >= 2) {
            if (strstr(frame->args[1], "ime=true") != NULL) {
                wm->ime_connected = 1;
            }
        }
        break;

    case TGS_CMD_WIN_CREATE: {
        /* args: [win_id, type, title] */
        if (frame->num_args < 3) break;
        int win_id = atoi(frame->args[0]);
        tgs_window_type wtype = str_to_window_type(frame->args[1]);
        void *handle = be->create_window(wtype, frame->args[2]);
        window_add(win_id, handle);
        send_resize(wm, win_id);
        break;
    }

    case TGS_CMD_WIN_DESTROY: {
        /* args: [win_id] */
        if (frame->num_args < 1) break;
        int win_id = atoi(frame->args[0]);
        void *handle = window_find(win_id);
        if (handle) {
            be->destroy_window(handle);
            window_remove(win_id);
        }
        break;
    }

    case TGS_CMD_WGT_CREATE: {
        /* args: [widget_id, parent_id, type, x, y, w, h, content] */
        if (frame->num_args < 8) break;
        int wid = atoi(frame->args[0]);
        int parent_id = atoi(frame->args[1]);
        tgs_widget_type wtype = str_to_widget_type(frame->args[2]);
        int x = atoi(frame->args[3]);
        int y = atoi(frame->args[4]);
        int w = atoi(frame->args[5]);
        int h = atoi(frame->args[6]);

        /* Find parent: try window first, then widget */
        void *parent = window_find(parent_id);
        int win_id_for_widget = -1;
        if (parent) {
            win_id_for_widget = parent_id;
        } else {
            parent = widget_find(parent_id);
            if (parent) win_id_for_widget = find_widget_window_id(parent);
        }

        void *handle = be->create_widget(parent, wtype);
        be->set_widget_rect(handle, x, y, w, h);
        /* Content is always present in the frame; an empty string means an
         * empty widget, so it must still be applied — otherwise LVGL's
         * placeholder text (LV_LABEL_DEFAULT_TEXT) leaks through. */
        if (frame->num_args >= 8) {
            be->set_widget_content(handle, frame->args[7]);
        }
        widget_add(wid, handle, wtype, win_id_for_widget);
        break;
    }

    case TGS_CMD_WGT_UPDATE: {
        /* args: [widget_id, content] */
        if (frame->num_args < 2) break;
        int wid = atoi(frame->args[0]);
        void *handle = widget_find(wid);
        if (handle) be->set_widget_content(handle, frame->args[1]);
        break;
    }

    case TGS_CMD_WGT_STYLE: {
        /* args: [widget_id, prop, value] */
        if (frame->num_args < 3) break;
        int wid = atoi(frame->args[0]);
        tgs_style_prop prop = (tgs_style_prop)atoi(frame->args[1]);
        int32_t value = (int32_t)atoi(frame->args[2]);
        void *handle = widget_find(wid);
        if (handle) be->set_widget_style(handle, prop, value);
        break;
    }

    case TGS_CMD_WGT_DESTROY: {
        /* args: [widget_id] */
        if (frame->num_args < 1) break;
        int wid = atoi(frame->args[0]);
        void *handle = widget_find(wid);
        if (handle) {
            be->destroy_widget(handle);
            widget_remove(wid);
        }
        break;
    }

    case TGS_CMD_EVT_BIND:
        /* Layer 0: all events forwarded, no-op */
        break;

    case TGS_CMD_IME_COMMIT: {
        /* args: [win_id, widget_id, text] */
        if (frame->num_args < 3) break;
        int wid = atoi(frame->args[1]);
        void *handle = widget_find(wid);
        if (handle && be->insert_widget_text) {
            be->insert_widget_text(handle, frame->args[2]);
        }
        break;
    }

    case TGS_CMD_IME_PREEDIT: {
        /* args: [win_id, widget_id, text, cursor] */
        /* Store preedit text for future display in textarea */
        break;
    }

    default:
        break;
    }
}

void wm_backend_event(void *widget_handle, tgs_event_type type,
                      const char *event_data, void *user_data)
{
    window_manager *wm = (window_manager *)user_data;
    char wid_str[16];
    char win_str[16];
    int widget_id = -1;
    int win_id = -1;
    int i;

    /* Look up widget_id from handle */
    for (i = 0; i < widget_count; i++) {
        if (widget_map[i].handle == widget_handle) {
            widget_id = widget_map[i].widget_id;
            break;
        }
    }
    if (widget_id < 0) return;

    /* Look up which window owns this widget — scan all windows for match.
     * For Layer 0: use first window as default. */
    if (window_count > 0) {
        win_id = window_map[0].win_id;
    }
    if (win_id < 0) return;

    snprintf(wid_str, sizeof(wid_str), "%d", widget_id);
    snprintf(win_str, sizeof(win_str), "%d", win_id);

    wm->frame_counter++;

    switch (type) {
    case TGS_EVENT_CLICK: {
        const char *args[2] = {win_str, wid_str};
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT,
                        wm->frame_counter, TGS_CMD_EVT_CLICK,
                        args, 2);
        break;
    }
    case TGS_EVENT_VALUE_CHANGED: {
        const char *args[3] = {win_str, wid_str,
                               event_data ? event_data : ""};
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT,
                        wm->frame_counter, TGS_CMD_EVT_VALUE,
                        args, 3);
        break;
    }
    case TGS_EVENT_FOCUS: {
        const char *args[3] = {win_str, wid_str, "1"};
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT,
                        wm->frame_counter, TGS_CMD_EVT_FOCUS,
                        args, 3);
        break;
    }
    case TGS_EVENT_BLUR: {
        const char *args[3] = {win_str, wid_str, "0"};
        tgs_frame_write(wm->pty_fd, TGS_STREAM_EVENT,
                        wm->frame_counter, TGS_CMD_EVT_FOCUS,
                        args, 3);
        /* Deactivate IME on blur */
        break;
    }
    case TGS_EVENT_KEY: {
        /* Route key to IME app if connected and focused widget is input,
         * otherwise route to main app */
        int target_fd = wm->pty_fd;
        char key_buf[16] = "0";
        char mod_buf[16] = "0";
        const char *kargs[4];
        if (wm->ime_connected && i < widget_count &&
            widget_map[i].type == TGS_WIDGET_INPUT && wm->ime_pty_fd >= 0) {
            target_fd = wm->ime_pty_fd;
        }
        /* Parse key and mods from event_data */
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
        tgs_frame_write(target_fd, TGS_STREAM_EVENT,
                        wm->frame_counter, TGS_CMD_EVT_KEY,
                        kargs, 4);
        break;
    }
    default:
        break;
}
}
