/*
 * TGS Event Engine
 * Parses terminal input (SGR mouse, keys) and injects into backend.
 */
#ifndef TGS_EVENT_ENGINE_H
#define TGS_EVENT_ENGINE_H

#include "tgs_backend.h"

typedef struct {
    tgs_backend *backend;
    int cell_w, cell_h;
    int mouse_x, mouse_y;
    int mouse_pressed;
} event_engine;

void ee_init(event_engine *ee, tgs_backend *backend, int cell_w, int cell_h);
void ee_feed(event_engine *ee, const uint8_t *data, int len);

#endif /* TGS_EVENT_ENGINE_H */
