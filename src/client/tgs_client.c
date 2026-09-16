/*
 * TGS Client Library Implementation
 * Communicates with compositor via stdin/stdout using TGS protocol frames.
 * C99, no external dependencies.
 */
#include "tgs_client.h"
#include "tgs_frame.h"
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Input buffer for partial frame reads */
#define INPUT_BUF_SIZE 4096
static char input_buf[INPUT_BUF_SIZE];
static int  input_len = 0;

/* Widget text cache */
#define MAX_WIDGETS 256
static char widget_text_cache[MAX_WIDGETS][256];

/* Focused widget id per window, kept up to date from NTF_FOCUS (65); 0 = none */
#define MAX_FOCUS_WINDOWS 64
static int focus_cache[MAX_FOCUS_WINDOWS];

/* Window ID counter */
static int next_window_id = 1;

/* Trace to stderr, opt-in via TGS_DEBUG.
 *
 * An app's stderr is the same PTY as its stdout, so anything written here
 * arrives on the terminal as characters and lands in the middle of the UI.
 * Silent unless asked for. */
static void debug_log(const char *msg)
{
    static int enabled = -1;
    if (enabled < 0) enabled = (getenv("TGS_DEBUG") != NULL) ? 1 : 0;
    if (enabled) (void)write(STDERR_FILENO, msg, strlen(msg));
}

/* Helper: convert int to string buffer */
static void int_to_str(int val, char *buf, int bufsize)
{
    snprintf(buf, (size_t)bufsize, "%d", val);
}

/* Helper: scan input buffer for a complete APC frame.
 * Returns frame payload length (between ESC_ and ST), or 0 if incomplete.
 * If found, copies payload to `payload` and advances input_buf/input_len.
 * Returns -1 on protocol error. */
static int find_apc_frame(char *payload, int payload_size)
{
    int i = 0;

    /* Find ESC _ */
    while (i < input_len - 1) {
        if ((unsigned char)input_buf[i] == 0x1B && input_buf[i + 1] == '_') {
            break;
        }
        i++;
    }

    if (i >= input_len - 1) {
        /* No APC start found; discard processed bytes */
        if (i > 0) {
            memmove(input_buf, input_buf + i, (size_t)(input_len - i));
            input_len -= i;
        }
        return 0;
    }

    /* Discard any bytes before ESC _ */
    if (i > 0) {
        memmove(input_buf, input_buf + i, (size_t)(input_len - i));
        input_len -= i;
    }

    /* Now input_buf starts with ESC _. Payload starts at offset 2. */
    /* Find ESC \ (ST) */
    for (i = 2; i < input_len - 1; i++) {
        if ((unsigned char)input_buf[i] == 0x1B && input_buf[i + 1] == '\\') {
            /* Found ST at position i */
            int payload_len = i - 2;
            if (payload_len <= 0 || payload_len >= payload_size) {
                /* Invalid or too large — skip this frame */
                memmove(input_buf, input_buf + i + 2,
                        (size_t)(input_len - i - 2));
                input_len -= (i + 2);
                return (payload_len <= 0) ? -1 : 0;
            }
            memcpy(payload, input_buf + 2, (size_t)payload_len);
            payload[payload_len] = '\0';
            memmove(input_buf, input_buf + i + 2,
                    (size_t)(input_len - i - 2));
            input_len -= (i + 2);
            return payload_len;
        }
    }

    /* ST not yet received — need more data */
    return 0;
}


/* Read more of the frame stream from stdin.
 *
 * Returns the number of bytes read (>0) when data arrived; 0 when the poll
 * expired with nothing to read; -1 on a real failure or EOF (the compositor
 * closed the stream). The previous version folded HUP into "timeout" (return
 * 0), which made a finite-timeout poll_event unable to tell a quiet period
 * from a broken connection and report both as -1. */
static int read_stdin_timeout(int timeout_ms)
{
    struct pollfd pfd;
    int n;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    n = poll(&pfd, 1, timeout_ms);
    if (n < 0) return -1;                 /* poll error */
    if (n == 0) return 0;                 /* timed out: nothing to read */
    if (!(pfd.revents & POLLIN)) return -1;  /* HUP/ERR/NVAL: the stream is over */

    n = (int)read(STDIN_FILENO, input_buf + input_len,
                  (size_t)(INPUT_BUF_SIZE - input_len));
    if (n <= 0) return -1;
    input_len += n;
    return n;
}

/* Read the next frame arriving on `stream_id` into *out.
 * Blocks until one arrives. Returns 0 on success, -1 on error. */
static int read_stream_frame(int stream_id, tgs_frame *out)
{
    char payload[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    int payload_len;
    int ret;

    while (1) {
        payload_len = find_apc_frame(payload, (int)sizeof(payload));
        if (payload_len < 0) return -1;
        if (payload_len == 0) {
            /* Need more data — blocking read */
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, -1) <= 0) return -1;
            if (!(pfd.revents & POLLIN)) return -1;
            ret = (int)read(STDIN_FILENO, input_buf + input_len,
                            (size_t)(INPUT_BUF_SIZE - input_len));
            if (ret <= 0) return -1;
            input_len += ret;
            continue;
        }
        if (tgs_frame_decode(payload, payload_len, out) != 0) {
            return -1;
        }
        if (out->stream_id == stream_id) return 0;
        /* Not our stream — keep looking */
    }
}

/* Wait for a specific command on a given stream. Blocks until received or error.
 * Returns 0 on success (frame parsed into *out), -1 on error. */
static int wait_for_frame(int stream_id, int command, tgs_frame *out)
{
    while (read_stream_frame(stream_id, out) == 0) {
        if (out->command == command) return 0;
        /* Not the frame we want — keep looking */
    }
    return -1;
}

/* Wait for READY (return 0) or REJECT (return 1, rejected capability in
 * out->args[0]) on the handshake stream. Returns -1 on error. */
static int wait_for_ready_or_reject(tgs_frame *out)
{
    while (read_stream_frame(TGS_STREAM_HANDSHAKE, out) == 0) {
        if (out->command == TGS_CMD_READY) return 0;
        if (out->command == TGS_CMD_REJECT) return 1;
        /* Other handshake traffic — keep looking */
    }
    return -1;
}

/* ------------------------------------------------------------------ */

int tgs_client_init(void)
{
    tgs_frame hello;
    char ver_str[32];
    const char *hello_args[2];
    int ret;

    debug_log("[TGS] client init: sending HELLO\n");

    /* The handshake is program-initiated: the compositor does not write to a
     * program that has not asked for TGS, so a character program never finds
     * protocol bytes on its stdin. The program speaks first. */
    hello_args[0] = TGS_PROTOCOL_VERSION;
    hello_args[1] = TGS_CAPS_LAYER0;
    if (tgs_frame_write(STDOUT_FILENO, TGS_STREAM_HANDSHAKE, 0,
                        TGS_CMD_HELLO, hello_args, 2) < 0) {
        debug_log("[TGS] failed to send HELLO\n");
        return -1;
    }

    (void)ver_str; /* unused after removing debug snprintf */

    debug_log("[TGS] sent HELLO, waiting for READY\n");

    /* Wait for READY, or REJECT if our capabilities cannot be honoured */
    ret = wait_for_ready_or_reject(&hello);
    if (ret < 0) {
        debug_log("[TGS] failed to receive READY\n");
        return -1;
    }
    if (ret == 1) {
        debug_log("[TGS] handshake rejected; unsupported capability: ");
        debug_log(hello.num_args > 0 ? hello.args[0] : "(unspecified)");
        debug_log("\n");
        return -1;
    }

    debug_log("[TGS] handshake complete\n");
    return 0;
}

int tgs_client_create_window(tgs_window_type type, const char *title,
                             tgs_window_info *info)
{
    tgs_frame resp;
    char id_str[32];
    char type_str[32];
    const char *args[3];
    int win_id;

    win_id = next_window_id++;
    int_to_str(win_id, id_str, (int)sizeof(id_str));
    int_to_str((int)type, type_str, (int)sizeof(type_str));

    args[0] = id_str;
    args[1] = type_str;
    args[2] = title;

    if (tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                        TGS_CMD_WIN_CREATE, args, 3) < 0) {
        return -1;
    }

    /* Wait for NTF_RESIZE */
    if (wait_for_frame(TGS_STREAM_COMMAND, TGS_CMD_NTF_RESIZE, &resp) != 0) {
        return -1;
    }

    if (resp.num_args < 5) return -1;

    if (info) {
        info->window_id = atoi(resp.args[0]);
        info->x = atoi(resp.args[1]);
        info->y = atoi(resp.args[2]);
        info->w = atoi(resp.args[3]);
        info->h = atoi(resp.args[4]);
    }

    return 0;
}

int tgs_client_create_widget(tgs_widget_type type, int id, int parent_id,
                              int x, int y, int w, int h, const char *content)
{
    char id_str[32], pid_str[32], type_str[32];
    char x_str[32], y_str[32], w_str[32], h_str[32];
    const char *args[8];

    int_to_str(id, id_str, (int)sizeof(id_str));
    int_to_str(parent_id, pid_str, (int)sizeof(pid_str));
    int_to_str((int)type, type_str, (int)sizeof(type_str));

    /* [widget_id, parent_id, type, x, y, w, h, content] */
    args[0] = id_str;
    args[1] = pid_str;
    args[2] = type_str;
    int_to_str(x, x_str, (int)sizeof(x_str));
    int_to_str(y, y_str, (int)sizeof(y_str));
    int_to_str(w, w_str, (int)sizeof(w_str));
    int_to_str(h, h_str, (int)sizeof(h_str));
    args[3] = x_str;
    args[4] = y_str;
    args[5] = w_str;
    args[6] = h_str;
    args[7] = (content ? content : "");

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_CREATE, args, 8) < 0 ? -1 : 0;
}

int tgs_client_update_widget(int id, const char *text)
{
    char id_str[32];
    const char *args[2];

    int_to_str(id, id_str, (int)sizeof(id_str));
    args[0] = id_str;
    args[1] = (text ? text : "");

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_UPDATE, args, 2) < 0 ? -1 : 0;
}

int tgs_client_set_widget_style(int id, tgs_style_prop prop, int32_t value)
{
    char id_str[32], prop_str[32], val_str[32];
    const char *args[3];

    int_to_str(id, id_str, (int)sizeof(id_str));
    int_to_str((int)prop, prop_str, (int)sizeof(prop_str));
    int_to_str((int)value, val_str, (int)sizeof(val_str));

    args[0] = id_str;
    args[1] = prop_str;
    args[2] = val_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_STYLE, args, 3) < 0 ? -1 : 0;
}

int tgs_client_set_focus(int win_id, int widget_id)
{
    char win_str[32], wid_str[32];
    const char *args[2];

    int_to_str(win_id, win_str, (int)sizeof(win_str));
    int_to_str(widget_id, wid_str, (int)sizeof(wid_str));

    args[0] = win_str;
    args[1] = wid_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_SET_FOCUS, args, 2) < 0 ? -1 : 0;
}

int tgs_client_get_focus(int win_id)
{
    if (win_id < 0 || win_id >= MAX_FOCUS_WINDOWS) return 0;
    return focus_cache[win_id];
}

int tgs_client_set_widget_attr(int id, tgs_widget_attr attr, int32_t value)
{
    char id_str[32], attr_str[32], val_str[32];
    const char *args[3];

    int_to_str(id, id_str, (int)sizeof(id_str));
    int_to_str((int)attr, attr_str, (int)sizeof(attr_str));
    int_to_str((int)value, val_str, (int)sizeof(val_str));

    args[0] = id_str;
    args[1] = attr_str;
    args[2] = val_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_ATTR, args, 3) < 0 ? -1 : 0;
}

int tgs_client_destroy_widget(int id)
{
    char id_str[32];
    const char *args[1];

    int_to_str(id, id_str, (int)sizeof(id_str));
    args[0] = id_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_DESTROY, args, 1) < 0 ? -1 : 0;
}

int tgs_client_bind_event(int widget_id, tgs_event_type event)
{
    char wid_str[32], ev_str[32];
    const char *args[2];

    int_to_str(widget_id, wid_str, (int)sizeof(wid_str));
    int_to_str((int)event, ev_str, (int)sizeof(ev_str));

    args[0] = wid_str;
    args[1] = ev_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_EVT_BIND, args, 2) < 0 ? -1 : 0;
}

int tgs_client_destroy_window(int win_id)
{
    char id_str[32];
    const char *args[1];

    int_to_str(win_id, id_str, (int)sizeof(id_str));
    args[0] = id_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WIN_DESTROY, args, 1) < 0 ? -1 : 0;
}

void tgs_client_shutdown(void)
{
    input_len = 0;
    memset(input_buf, 0, sizeof(input_buf));
    next_window_id = 1;
}

int tgs_client_poll_event(tgs_event *ev, int timeout_ms)
{
    tgs_frame frame;
    char payload[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    int payload_len;
    int wid;

    if (!ev) return -1;
    memset(ev, 0, sizeof(*ev));

    while (1) {
        /* Try to extract a complete frame from buffer */
        payload_len = find_apc_frame(payload, (int)sizeof(payload));
        if (payload_len < 0) return -1;
        if (payload_len > 0) {
            if (tgs_frame_decode(payload, payload_len, &frame) != 0)
                continue;
            if (frame.stream_id != TGS_STREAM_EVENT) continue;

            /* Map command to event */
            switch (frame.command) {
            case TGS_CMD_EVT_CLICK:
                ev->type = TGS_EVENT_CLICK;
                if (frame.num_args >= 2) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                }
                return 0;

            case TGS_CMD_EVT_KEY:
                ev->type = TGS_EVENT_KEY;
                if (frame.num_args >= 4) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                    ev->key = atoi(frame.args[2]);
                    ev->modifiers = atoi(frame.args[3]);
                }
                return 0;

            case TGS_CMD_EVT_VALUE:
                ev->type = TGS_EVENT_VALUE_CHANGED;
                if (frame.num_args >= 3) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                    strncpy(ev->text, frame.args[2], sizeof(ev->text) - 1);
                    /* Update widget text cache */
                    wid = ev->widget_id;
                    if (wid >= 0 && wid < MAX_WIDGETS) {
                        strncpy(widget_text_cache[wid], frame.args[2],
                                sizeof(widget_text_cache[wid]) - 1);
                        widget_text_cache[wid][255] = '\0';
                    }
                }
                return 0;

            case TGS_CMD_NTF_FOCUS:
                /* args: [win_id, widget_id, focused, reason] */
                if (frame.num_args >= 3) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                    ev->focused = atoi(frame.args[2]);
                    ev->reason = frame.num_args >= 4
                                     ? atoi(frame.args[3])
                                     : TGS_REASON_NONE;
                    if (ev->window_id >= 0 && ev->window_id < MAX_FOCUS_WINDOWS) {
                        focus_cache[ev->window_id] =
                            ev->focused ? ev->widget_id : 0;
                    }
                    ev->type = ev->focused ? TGS_EVENT_FOCUS : TGS_EVENT_BLUR;
                }
                return 0;

            case TGS_CMD_IME_PREEDIT:
                ev->type = TGS_EVENT_IME_PREEDIT;
                if (frame.num_args >= 3) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                    strncpy(ev->text, frame.args[2], sizeof(ev->text) - 1);
                }
                return 0;

            case TGS_CMD_IME_COMMIT:
                ev->type = TGS_EVENT_IME_COMMIT;
                if (frame.num_args >= 3) {
                    ev->window_id = atoi(frame.args[0]);
                    ev->widget_id = atoi(frame.args[1]);
                    strncpy(ev->text, frame.args[2], sizeof(ev->text) - 1);
                }
                return 0;

            default:
                /* Unknown event command — skip */
                continue;
            }
        }

        /* No complete frame yet; read more. */
        int r = read_stdin_timeout(timeout_ms);
        if (r < 0) return -1;          /* the stream ended or failed */
        if (r == 0) return 1;          /* nothing arrived within the timeout: no event */
        /* data arrived; loop to scan it for a frame */
    }
}

const char *tgs_client_get_widget_text(int widget_id)
{
    if (widget_id < 0 || widget_id >= MAX_WIDGETS) return "";
    return widget_text_cache[widget_id];
}

int tgs_client_send_ime_commit(int win_id, int widget_id, const char *text)
{
    char win_str[16], wid_str[16];
    snprintf(win_str, sizeof(win_str), "%d", win_id);
    snprintf(wid_str, sizeof(wid_str), "%d", widget_id);
    const char *args[] = { win_str, wid_str, text };
    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_IME_COMMIT, args, 3) < 0 ? -1 : 0;
}

int tgs_client_send_ime_preedit(int win_id, int widget_id, const char *text, int cursor)
{
    char win_str[16], wid_str[16], cur_str[16];
    snprintf(win_str, sizeof(win_str), "%d", win_id);
    snprintf(wid_str, sizeof(wid_str), "%d", widget_id);
    snprintf(cur_str, sizeof(cur_str), "%d", cursor);
    const char *args[] = { win_str, wid_str, text, cur_str };
    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_IME_PREEDIT, args, 4) < 0 ? -1 : 0;
}

/* Frame 100 is compositor → IME app (stream 4, same channel as the key
 * hand-off); this writer exists for tests and simulated compositors. */
int tgs_client_send_ime_cancel(int win_id, int widget_id)
{
    char win_str[32], wid_str[32];
    const char *args[2];

    int_to_str(win_id, win_str, (int)sizeof(win_str));
    int_to_str(widget_id, wid_str, (int)sizeof(wid_str));

    args[0] = win_str;
    args[1] = wid_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_EVENT, 0,
                           TGS_CMD_IME_CANCEL, args, 2) < 0 ? -1 : 0;
}

/* Change a container's layout at runtime. Optional: containers receive their
 * layout from their widget type at WGT_CREATE time. */
int tgs_client_set_widget_layout(int id, tgs_layout_type layout)
{
    char id_str[32], layout_str[32];
    const char *args[2];

    int_to_str(id, id_str, (int)sizeof(id_str));
    int_to_str((int)layout, layout_str, (int)sizeof(layout_str));

    args[0] = id_str;
    args[1] = layout_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_WGT_LAYOUT, args, 2) < 0 ? -1 : 0;
}

int tgs_client_send_ime_candidates(int win_id, int widget_id,
                                   const char *candidates[], int count)
{
    char win_str[16], wid_str[16], cnt_str[16];
    const char *args[TGS_MAX_ARGS];
    int i;

    if (count < 0 || (count > 0 && !candidates)) return -1;

    /* win_id + widget_id + count occupy three arg slots */
    if (count > TGS_MAX_ARGS - 3) count = TGS_MAX_ARGS - 3;

    int_to_str(win_id, win_str, (int)sizeof(win_str));
    int_to_str(widget_id, wid_str, (int)sizeof(wid_str));
    int_to_str(count, cnt_str, (int)sizeof(cnt_str));

    args[0] = win_str;
    args[1] = wid_str;
    args[2] = cnt_str;
    for (i = 0; i < count; i++) {
        args[3 + i] = candidates[i] ? candidates[i] : "";
    }

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_IME_CANDIDATES, args, count + 3) < 0 ? -1 : 0;
}

int tgs_client_send_ime_select(int win_id, int widget_id, int index)
{
    char win_str[16], wid_str[16], idx_str[16];
    const char *args[3];

    int_to_str(win_id, win_str, (int)sizeof(win_str));
    int_to_str(widget_id, wid_str, (int)sizeof(wid_str));
    int_to_str(index, idx_str, (int)sizeof(idx_str));

    args[0] = win_str;
    args[1] = wid_str;
    args[2] = idx_str;

    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_IME_SELECT, args, 3) < 0 ? -1 : 0;
}
