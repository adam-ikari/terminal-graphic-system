/*
 * TGS Client Library Implementation (SVG semantics)
 * Communicates with the renderer via stdin/stdout using TGS frames, which
 * ride the kitty graphics APC channel (ESC _ Gtgs;... ESC \).
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

/* Trace to stderr, opt-in via TGS_DEBUG. */
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

/* Scan input buffer for a complete APC frame (ESC _ ... ESC \). Returns the
 * payload length (between ESC_ and ST), or 0 if incomplete; copies the
 * payload out and advances the buffer. -1 on protocol error. */
static int find_apc_frame(char *payload, int payload_size)
{
    int i = 0;

    while (i < input_len - 1) {
        if ((unsigned char)input_buf[i] == 0x1B && input_buf[i + 1] == '_') break;
        i++;
    }

    if (i >= input_len - 1) {
        if (i > 0) {
            memmove(input_buf, input_buf + i, (size_t)(input_len - i));
            input_len -= i;
        }
        return 0;
    }
    if (i > 0) {
        memmove(input_buf, input_buf + i, (size_t)(input_len - i));
        input_len -= i;
    }

    for (i = 2; i < input_len - 1; i++) {
        if ((unsigned char)input_buf[i] == 0x1B && input_buf[i + 1] == '\\') {
            int payload_len = i - 2;
            if (payload_len <= 0 || payload_len >= payload_size) {
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
    return 0;
}

/* Read more of the frame stream from stdin: >0 bytes read, 0 timeout, -1
 * failure/EOF. */
static int read_stdin_timeout(int timeout_ms)
{
    struct pollfd pfd;
    int n;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    n = poll(&pfd, 1, timeout_ms);
    if (n < 0) return -1;
    if (n == 0) return 0;
    if (!(pfd.revents & POLLIN)) return -1;

    n = (int)read(STDIN_FILENO, input_buf + input_len,
                  (size_t)(INPUT_BUF_SIZE - input_len));
    if (n <= 0) return -1;
    input_len += n;
    return n;
}

/* Read the next frame arriving on `stream_id` into *out (blocking). */
static int read_stream_frame(int stream_id, tgs_frame *out)
{
    char payload[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    int payload_len;

    while (1) {
        payload_len = find_apc_frame(payload, (int)sizeof(payload));
        if (payload_len < 0) return -1;
        if (payload_len == 0) {
            int ret = read_stdin_timeout(-1);
            if (ret < 0) return -1;
            continue;
        }
        if (tgs_frame_decode(payload, payload_len, out) != 0) return -1;
        if (out->stream_id == stream_id) return 0;
    }
}

/* Wait for READY (0) or REJECT (1, capability in out->args[0]) on the
 * handshake stream. -1 on error. */
static int wait_for_ready_or_reject(tgs_frame *out)
{
    while (read_stream_frame(TGS_STREAM_HANDSHAKE, out) == 0) {
        if (out->command == TGS_CMD_READY) return 0;
        if (out->command == TGS_CMD_REJECT) return 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ */

int tgs_client_init(void)
{
    tgs_frame hello;
    const char *hello_args[2];
    int ret;

    hello_args[0] = TGS_PROTOCOL_VERSION;
    hello_args[1] = TGS_CAPS_LAYER0;
    if (tgs_frame_write(STDOUT_FILENO, TGS_STREAM_HANDSHAKE, 0,
                        TGS_CMD_HELLO, hello_args, 2) < 0) {
        debug_log("[TGS] failed to send HELLO\n");
        return -1;
    }

    ret = wait_for_ready_or_reject(&hello);
    if (ret < 0) {
        debug_log("[TGS] failed to receive READY\n");
        return -1;
    }
    if (ret == 1) {
        debug_log("[TGS] handshake rejected\n");
        return -1;
    }
    debug_log("[TGS] handshake complete\n");
    return 0;
}

static int send_command(int command, const char *args[], int num_args)
{
    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           command, args, num_args);
}

int tgs_client_create_element(tgs_widget_type type, int id, int parent,
                              int x, int y, int w, int h,
                              const char *content)
{
    char id_str[16], type_str[16], p_str[16], x_str[16], y_str[16];
    char w_str[16], h_str[16];
    const char *args[8];

    int_to_str(id, id_str, sizeof(id_str));
    int_to_str((int)type, type_str, sizeof(type_str));
    int_to_str(parent, p_str, sizeof(p_str));
    int_to_str(x, x_str, sizeof(x_str));
    int_to_str(y, y_str, sizeof(y_str));
    int_to_str(w, w_str, sizeof(w_str));
    int_to_str(h, h_str, sizeof(h_str));

    args[0] = id_str;
    args[1] = type_str;
    args[2] = p_str;
    args[3] = x_str;
    args[4] = y_str;
    args[5] = w_str;
    args[6] = h_str;
    args[7] = content ? content : "";
    return send_command(TGS_CMD_WGT_CREATE, args, 8);
}

int tgs_client_update_element(int id, const char *value)
{
    char id_str[16];
    const char *args[2];

    int_to_str(id, id_str, sizeof(id_str));
    args[0] = id_str;
    args[1] = value ? value : "";
    return send_command(TGS_CMD_WGT_UPDATE, args, 2);
}

int tgs_client_set_element_style(int id, tgs_style_prop prop, int32_t value)
{
    char id_str[16], prop_str[16], val_str[16];
    const char *args[3];

    int_to_str(id, id_str, sizeof(id_str));
    int_to_str((int)prop, prop_str, sizeof(prop_str));
    int_to_str((int)value, val_str, sizeof(val_str));
    args[0] = id_str;
    args[1] = prop_str;
    args[2] = val_str;
    return send_command(TGS_CMD_WGT_STYLE, args, 3);
}

int tgs_client_destroy_element(int id)
{
    char id_str[16];
    const char *args[1];

    int_to_str(id, id_str, sizeof(id_str));
    args[0] = id_str;
    return send_command(TGS_CMD_WGT_DESTROY, args, 1);
}

void tgs_client_shutdown(void)
{
    /* Nothing to flush: frames are written synchronously. */
}

/* ------------------------------------------------------------------ */

int tgs_client_poll_event(tgs_event *ev, int timeout_ms)
{
    tgs_frame frame;
    char payload[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    int payload_len;

    if (!ev) return -1;
    memset(ev, 0, sizeof(*ev));

    while (1) {
        payload_len = find_apc_frame(payload, (int)sizeof(payload));
        if (payload_len < 0) return -1;
        if (payload_len > 0) {
            if (tgs_frame_decode(payload, payload_len, &frame) != 0) continue;
            if (frame.stream_id != TGS_STREAM_EVENT) continue;

            switch (frame.command) {
            case TGS_CMD_EVT_CLICK:
                ev->type = TGS_EVENT_CLICK;
                if (frame.num_args >= 1) ev->id = atoi(frame.args[0]);
                return 0;

            case TGS_CMD_EVT_KEY:
                ev->type = TGS_EVENT_KEY;
                if (frame.num_args >= 2) {
                    ev->key = atoi(frame.args[0]);
                    ev->modifiers = atoi(frame.args[1]);
                }
                ev->id = -1;
                return 0;

            case TGS_CMD_EVT_HOVER_ENTER:
                ev->type = TGS_EVENT_HOVER_ENTER;
                if (frame.num_args >= 1) ev->id = atoi(frame.args[0]);
                return 0;

            case TGS_CMD_EVT_HOVER_LEAVE:
                ev->type = TGS_EVENT_HOVER_LEAVE;
                if (frame.num_args >= 1) ev->id = atoi(frame.args[0]);
                return 0;

            case TGS_CMD_EVT_POINTER:
                ev->type = TGS_EVENT_POINTER;
                if (frame.num_args >= 3) {
                    ev->x = atoi(frame.args[0]);
                    ev->y = atoi(frame.args[1]);
                    ev->phase = atoi(frame.args[2]);
                }
                ev->id = -1;
                return 0;

            case TGS_CMD_IME_COMMIT:
                ev->type = TGS_EVENT_IME_COMMIT;
                if (frame.num_args >= 1)
                    strncpy(ev->text, frame.args[0], sizeof(ev->text) - 1);
                ev->id = -1;
                return 0;

            default:
                continue;   /* notifications and unknown frames are absorbed */
            }
        }

        /* No complete frame: read more. Finite timeout: return 1 on expiry. */
        {
            int ret = read_stdin_timeout(timeout_ms);
            if (ret < 0) return -1;
            if (ret == 0) return (timeout_ms >= 0) ? 1 : -1;
        }
    }
}

/* ------------------------------------------------------------------ */

int tgs_client_send_ime_commit(const char *text)
{
    const char *args[1];

    args[0] = text ? text : "";
    return tgs_frame_write(STDOUT_FILENO, TGS_STREAM_COMMAND, 0,
                           TGS_CMD_IME_COMMIT, args, 1);
}