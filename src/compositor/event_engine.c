/*
 * TGS Event Engine Implementation
 * Parses SGR mouse and key sequences, injects into backend.
 */
#include "event_engine.h"
#include <string.h>

void ee_init(event_engine *ee, tgs_backend *backend, int cell_w, int cell_h)
{
    ee->backend = backend;
    ee->cell_w = cell_w;
    ee->cell_h = cell_h;
    ee->mouse_x = 0;
    ee->mouse_y = 0;
    ee->mouse_pressed = 0;
}

/* State for CSI sequence parsing */
enum {
    PARSE_GROUND,
    PARSE_CSI,
    PARSE_CSI_PARAM
};

static int parse_sgr_mouse(event_engine *ee, const uint8_t *data, int len,
                           int *consumed)
{
    /* SGR mouse: \e[<button;x;y;M or \e[<button;x;y;m */
    int i = 0;
    int button = 0, x = 0, y = 0;
    char type_ch = 'M';

    /* Skip \e[< */
    i = 3;
    if (i >= len) return 0;

    /* Parse button */
    for (; i < len && data[i] != ';'; i++) {
        if (data[i] < '0' || data[i] > '9') return 0;
        button = button * 10 + (data[i] - '0');
    }
    if (i >= len) return 0;
    i++; /* skip ; */

    /* Parse x */
    for (; i < len && data[i] != ';'; i++) {
        if (data[i] < '0' || data[i] > '9') return 0;
        x = x * 10 + (data[i] - '0');
    }
    if (i >= len) return 0;
    i++; /* skip ; */

    /* Parse y */
    for (; i < len && data[i] != 'M' && data[i] != 'm'; i++) {
        if (data[i] < '0' || data[i] > '9') return 0;
        y = y * 10 + (data[i] - '0');
    }
    if (i >= len) return 0;

    type_ch = (char)data[i];
    i++; /* past M or m */

    *consumed = i;

    /* Convert cell coords to pixel */
    int pixel_x = (x - 1) * ee->cell_w;
    int pixel_y = (y - 1) * ee->cell_h;
    int btn = button & 3;
    int pressed = (type_ch == 'M') ? 1 : 0;

    if (pixel_x < 0) pixel_x = 0;
    if (pixel_y < 0) pixel_y = 0;

    ee->mouse_x = pixel_x;
    ee->mouse_y = pixel_y;
    ee->mouse_pressed = pressed;

    ee->backend->inject_mouse(pixel_x, pixel_y, btn, pressed);
    return 1;
}

void ee_feed(event_engine *ee, const uint8_t *data, int len)
{
    int i = 0;

    while (i < len) {
        /* SGR mouse: \e[< */
        if (i + 2 < len && data[i] == 0x1B && data[i + 1] == '[' &&
            data[i + 2] == '<') {
            int consumed = 0;
            if (parse_sgr_mouse(ee, data + i, len - i, &consumed)) {
                i += consumed;
                continue;
            }
        }

        /* ESC [A/B/C/D — arrow keys */
        if (i + 2 < len && data[i] == 0x1B && data[i + 1] == '[') {
            int key = 0;
            switch (data[i + 2]) {
            case 'A': key = 1; break; /* up */
            case 'B': key = 2; break; /* down */
            case 'C': key = 3; break; /* right */
            case 'D': key = 4; break; /* left */
            }
            if (key) {
                ee->backend->inject_key(key, 0, 1);
                i += 3;
                continue;
            }
        }

        /* ESC alone */
        if (data[i] == 0x1B) {
            /* Check if standalone (not start of a longer sequence) */
            if (i + 1 >= len || data[i + 1] != '[') {
                ee->backend->inject_key(27, 0, 1);
                i++;
                continue;
            }
            /* Unknown CSI sequence — skip past next alpha char */
            i += 3;
            while (i < len && !((data[i] >= 'A' && data[i] <= 'Z') ||
                                (data[i] >= 'a' && data[i] <= 'z')))
                i++;
            if (i < len) i++;
            continue;
        }

        /* Enter: \r or \n */
        if (data[i] == '\r' || data[i] == '\n') {
            ee->backend->inject_key(13, 0, 1);
            i++;
            continue;
        }

        /* Backspace: 0x7f or 0x08 */
        if (data[i] == 0x7F || data[i] == 0x08) {
            ee->backend->inject_key(8, 0, 1);
            i++;
            continue;
        }

        /* Tab */
        if (data[i] == '\t') {
            ee->backend->inject_key(9, 0, 1);
            i++;
            continue;
        }

        /* Regular ASCII */
        if (data[i] >= 0x20 && data[i] <= 0x7E) {
            ee->backend->inject_key((int)data[i], 0, 1);
            i++;
            continue;
        }

        /* Skip unknown byte */
        i++;
    }
}
