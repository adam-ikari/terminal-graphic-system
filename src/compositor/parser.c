/*
 * TGS stream demultiplexer — see parser.h.
 */
#include "parser.h"
#include <string.h>

#define ESC 0x1B
#define APC_INTRO 0x5F   /* '_' — the byte after ESC that opens a TGS frame */
#define ST_ESC 0x1B      /* first byte of the String Terminator, ESC \ */
#define ST_BACKSLASH 0x5C

void tgs_parser_init(tgs_parser *p, tgs_frame_callback cb, void *ud)
{
    memset(p, 0, sizeof(*p));
    p->callback = cb;
    p->user_data = ud;
}

void tgs_parser_set_text_cb(tgs_parser *p, tgs_text_callback cb, void *ud)
{
    p->text_cb = cb;
    p->text_user_data = ud;
}

/* Hand the first n buffered bytes to the text callback and drop them. */
static void flush_text(tgs_parser *p, int n)
{
    if (n <= 0) return;
    if (p->text_cb) p->text_cb(p->buf, n, p->text_user_data);
    memmove(p->buf, p->buf + n, (size_t)(p->buf_len - n));
    p->buf_len -= n;
}

void tgs_parser_feed(tgs_parser *p, const uint8_t *data, int len)
{
    int avail;

    avail = (int)sizeof(p->buf) - p->buf_len;
    if (len > avail) len = avail;
    if (len <= 0) return;

    memcpy(p->buf + p->buf_len, data, (size_t)len);
    p->buf_len += len;

    for (;;) {
        int i;
        int frame_start = -1;
        int frame_end = -1;

        for (i = 0; i + 1 < p->buf_len; i++) {
            if (p->buf[i] == ESC && p->buf[i + 1] == APC_INTRO) {
                frame_start = i;
                break;
            }
        }

        if (frame_start < 0) {
            /* Nothing that could begin a frame: all of it is text. Hold back
             * a trailing ESC — the next chunk may complete an introducer. */
            int flush = p->buf_len;
            if (flush > 0 && p->buf[flush - 1] == ESC) flush--;
            flush_text(p, flush);
            break;
        }

        if (frame_start > 0) {
            flush_text(p, frame_start);
            continue;   /* the frame introducer is now at the head */
        }

        /* Buffer starts with ESC _ — find the terminator. */
        for (i = 2; i + 1 < p->buf_len; i++) {
            if (p->buf[i] == ST_ESC && p->buf[i + 1] == ST_BACKSLASH) {
                frame_end = i;
                break;
            }
        }
        if (frame_end < 0) break;   /* incomplete frame — wait for more bytes */

        /* Dual identification in the shared APC channel (TGS ⊃ kitty):
         *   "G1;..."  → TGS frame (the G1 sub-namespace)
         *   "G..."    → kitty-native graphics frame (pixel placement);
         *               accepted as part of the superset — not yet wired
         *               to canvas ops, but never fed to the TGS decoder.
         * Everything else on this channel is not a TGS frame. */
        if (p->callback && p->buf_len > 3 &&
            p->buf[2] == 'G' && p->buf[3] == '1') {
            tgs_frame frame;
            if (tgs_frame_decode((const char *)p->buf + 2, frame_end - 2,
                                 &frame) == 0)
                p->callback(&frame, p->user_data);
        }

        {
            int consumed = frame_end + 2;   /* past the ESC \ */
            memmove(p->buf, p->buf + consumed,
                    (size_t)(p->buf_len - consumed));
            p->buf_len -= consumed;
        }
    }

    /* A frame larger than the whole buffer can never complete — drop it
     * rather than stall the stream forever. */
    if (p->buf_len >= (int)sizeof(p->buf)) p->buf_len = 0;
}
