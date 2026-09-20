/*
 * TGS Frame encode/decode implementation.
 * Fixed-buffer, no dynamic allocation, C99.
 */
#define _POSIX_C_SOURCE 200809L
#include "tgs_frame.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int tgs_frame_encode(int stream_id, int frame_id, int command,
                     const char *args[], int num_args,
                     char *out, int out_size)
{
    int n;
    int written;

    if (out_size <= 0) return -1;
    if (num_args < 0 || num_args > TGS_MAX_ARGS) return -1;

    /* TGS rides the kitty graphics APC: payload = "Gtgs;" + the classic
     * semicolon payload. A "G" followed directly by key=value is a kitty-
     * native frame (pixel placement); "Gtgs;" marks TGS's superset frames. */
    n = snprintf(out, out_size, "Gtgs;%d;%d;%d",
                 stream_id, frame_id, command);
    if (n < 0 || n >= out_size) return -1;
    written = n;

    for (int i = 0; i < num_args; i++) {
        if (!args[i]) return -1;
        n = snprintf(out + written, out_size - written, ";%s", args[i]);
        if (n < 0 || written + n >= out_size) return -1;
        written += n;
    }

    return written;
}

/* Split off one ';'-separated field, advancing *cursor. Unlike strtok_r,
 * empty fields are preserved: the client legitimately sends empty arguments
 * (e.g. an empty widget content), and collapsing them silently shifts or
 * drops trailing arguments. Returns NULL once the payload is exhausted. */
static char *next_field(char **cursor)
{
    char *start = *cursor;
    char *sep;

    if (!start) return NULL;

    sep = strchr(start, ';');
    if (sep) {
        *sep = '\0';
        *cursor = sep + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

int tgs_frame_decode(const char *data, int len, tgs_frame *frame)
{
    char buf[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    char *cursor;
    char *token;
    int count;

    if (!data || len <= 0 || !frame) return -1;
    if (len >= (int)sizeof(buf)) return -1;

    memcpy(buf, data, (size_t)len);
    buf[len] = '\0';

    memset(frame, 0, sizeof(*frame));

    cursor = buf;

    /* First field: must be "Gtgs" — the kitty APC carrying a TGS frame.
     * A bare "G" (kitty-native payload) is not a TGS frame; the parser
     * routes those straight to the kitty pixel path. */
    token = next_field(&cursor);
    if (!token || strcmp(token, "Gtgs") != 0) return -1;

    /* stream_id */
    token = next_field(&cursor);
    if (!token) return -1;
    frame->stream_id = atoi(token);

    /* frame_id */
    token = next_field(&cursor);
    if (!token) return -1;
    frame->frame_id = atoi(token);

    /* command */
    token = next_field(&cursor);
    if (!token) return -1;
    frame->command = atoi(token);

    /* remaining fields are args */
    count = 0;
    while ((token = next_field(&cursor)) != NULL) {
        if (count >= TGS_MAX_ARGS) return -1;
        strncpy(frame->args[count], token, TGS_MAX_ARG_LEN - 1);
        frame->args[count][TGS_MAX_ARG_LEN - 1] = '\0';
        count++;
    }
    frame->num_args = count;

    return 0;
}

int tgs_frame_write(int fd, int stream_id, int frame_id, int command,
                    const char *args[], int num_args)
{
    char payload[TGS_MAX_ARGS * TGS_MAX_ARG_LEN + 64];
    int plen;
    const char *apc_start = "\x1b_";
    const char *apc_end = "\x1b\\";
    ssize_t total = 0;
    ssize_t w;

    plen = tgs_frame_encode(stream_id, frame_id, command, args, num_args,
                            payload, (int)sizeof(payload));
    if (plen <= 0) return -1;

    w = write(fd, apc_start, 2);
    if (w != 2) return -1;
    total += w;

    w = write(fd, payload, (size_t)plen);
    if (w != plen) return -1;
    total += w;

    w = write(fd, apc_end, 2);
    if (w != 2) return -1;
    total += w;

    return (int)total;
}
