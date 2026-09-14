/*
 * TGS APC Frame Scanner/Dispatcher
 * APC = ESC _ <payload> ESC \
 * Scans byte stream, extracts payloads, decodes, dispatches.
 */
#include "parser.h"
#include <string.h>

void tgs_parser_init(tgs_parser *p, tgs_frame_callback cb, void *ud)
{
    p->buf_len = 0;
    p->callback = cb;
    p->user_data = ud;
}

void tgs_parser_feed(tgs_parser *p, const uint8_t *data, int len)
{
    int avail;
    int i;

    /* Append incoming data */
    avail = (int)sizeof(p->buf) - p->buf_len;
    if (len > avail) len = avail;
    if (len <= 0) return;

    memcpy(p->buf + p->buf_len, data, (size_t)len);
    p->buf_len += len;

    /* Scan for complete APC frames: ESC _ ... ESC \ */
    for (;;) {
        int frame_start = -1;
        int frame_end = -1;

        /* Find ESC _ */
        for (i = 0; i < p->buf_len - 1; i++) {
            if (p->buf[i] == 0x1B && p->buf[i + 1] == 0x5F) {
                frame_start = i + 2; /* payload starts after ESC _ */
                break;
            }
        }
        if (frame_start < 0) break;

        /* Find ESC \ after frame_start */
        for (i = frame_start; i < p->buf_len - 1; i++) {
            if (p->buf[i] == 0x1B && p->buf[i + 1] == 0x5C) {
                frame_end = i; /* payload ends before ESC \ */
                break;
            }
        }
        if (frame_end < 0) {
            /* Incomplete frame — need more data. Compact if possible,
             * keeping the ESC _ introducer so the frame can still be
             * recognised once the remainder arrives. */
            int keep = frame_start - 2;
            if (keep > 0) {
                memmove(p->buf, p->buf + keep,
                        (size_t)(p->buf_len - keep));
                p->buf_len -= keep;
            }
            break;
        }

        /* Complete frame found — decode and dispatch */
        if (p->callback) {
            tgs_frame frame;
            int plen = frame_end - frame_start;
            if (tgs_frame_decode((const char *)p->buf + frame_start,
                                 plen, &frame) == 0) {
                p->callback(&frame, p->user_data);
            }
        }

        /* Remove consumed bytes (ESC _ payload ESC \ = 4 + payload) */
        {
            int consumed = frame_end + 2; /* past the ESC \ */
            memmove(p->buf, p->buf + consumed,
                    (size_t)(p->buf_len - consumed));
            p->buf_len -= consumed;
        }
    }

    /* If buffer is full and no frame found, discard */
    if (p->buf_len >= (int)sizeof(p->buf)) {
        p->buf_len = 0;
    }
}
