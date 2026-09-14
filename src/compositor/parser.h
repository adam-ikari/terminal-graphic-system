/*
 * TGS APC Frame Scanner/Dispatcher
 * Accumulates raw bytes, detects ESC _ ... ESC \ frames,
 * decodes and dispatches via callback.
 */
#ifndef TGS_PARSER_H
#define TGS_PARSER_H

#include "tgs_frame.h"
#include <stdint.h>

typedef void (*tgs_frame_callback)(const tgs_frame *frame, void *user_data);

typedef struct {
    uint8_t buf[4096];
    int buf_len;
    tgs_frame_callback callback;
    void *user_data;
} tgs_parser;

void tgs_parser_init(tgs_parser *p, tgs_frame_callback cb, void *ud);
void tgs_parser_feed(tgs_parser *p, const uint8_t *data, int len);

#endif /* TGS_PARSER_H */
