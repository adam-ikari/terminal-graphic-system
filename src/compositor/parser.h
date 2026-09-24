/*
 * TGS stream demultiplexer.
 *
 * A program talks to the compositor over ONE byte stream. The parser splits
 * it into its two kinds of content:
 *
 *   - TGS APC frames (ESC _ TGS;… ESC \) → the frame callback (graphics);
 *   - everything else                     → the text callback (characters).
 *
 * That is the whole "character compatibility first" contract: a program that
 * never emits a frame is a pure character program, and its bytes reach the
 * terminal unchanged.
 */
#ifndef TGS_PARSER_H
#define TGS_PARSER_H

#include "tgs_frame.h"
#include <stdint.h>

typedef void (*tgs_frame_callback)(const tgs_frame *frame, void *user_data);

/* Ordinary output: the bytes that are not part of any TGS frame. */
typedef void (*tgs_text_callback)(const uint8_t *data, int len, void *user_data);

/* A kitty-native graphics payload: the APC body starting at 'G', but not the
 * TGS sub-namespace ("G1;..."). Handed over whole, never to the TGS decoder. */
typedef void (*tgs_kitty_callback)(const uint8_t *payload, int len, void *user_data);

typedef struct {
    /* 8192: one kitty-native chunk is base64(4096 binary) ≈ 5.5KB plus the
     * control keys — a full-size chunk must fit to be accepted at all. */
    uint8_t buf[8192];
    int buf_len;
    tgs_frame_callback callback;
    void *user_data;
    tgs_text_callback text_cb;
    void *text_user_data;
    tgs_kitty_callback kitty_cb;
    void *kitty_user_data;
} tgs_parser;

void tgs_parser_init(tgs_parser *p, tgs_frame_callback cb, void *ud);
void tgs_parser_set_text_cb(tgs_parser *p, tgs_text_callback cb, void *ud);
void tgs_parser_set_kitty_cb(tgs_parser *p, tgs_kitty_callback cb, void *ud);
void tgs_parser_feed(tgs_parser *p, const uint8_t *data, int len);

#endif /* TGS_PARSER_H */
