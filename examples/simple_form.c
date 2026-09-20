/*
 * simple_form.c — reference demo of the TGS element API (SVG semantics).
 *
 * The program owns all state: it composes a label + a clickable box (a
 * "submit button") + a result label out of TEXT and BOX primitives, and it
 * edits an input string itself from KEY events. The renderer paints; the
 * program decides.
 *
 * Writes TGS protocol to stdout; debug output goes to stderr.
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>
#include <string.h>

#define ID_PROMPT    1   /* TEXT */
#define ID_INPUT     2   /* TEXT (echo of the program's edit buffer) */
#define ID_BTN_BOX   3   /* BOX (the clickable area) */
#define ID_BTN_LBL   4   /* TEXT child of the button box */
#define ID_RESULT    5   /* TEXT */

int main(void)
{
    tgs_event ev;
    char buf[256];
    int len = 0;

    buf[0] = '\0';

    if (tgs_client_init() != 0) {
        fprintf(stderr, "simple_form: client init failed\n");
        return 1;
    }

    /* Canvas root = parent 0. Geometry is program-computed. */
    tgs_client_create_element(TGS_WIDGET_TEXT,  ID_PROMPT,  0,
                               20, 20, 200, 30, "Enter your name:");
    tgs_client_create_element(TGS_WIDGET_TEXT,  ID_INPUT,   0,
                               20, 60, 300, 40, "");
    /* A "submit button" = a BOX (clickable) + a TEXT child centered on it. */
    tgs_client_create_element(TGS_WIDGET_BOX,   ID_BTN_BOX, 0,
                               20, 110, 120, 40, "");
    tgs_client_create_element(TGS_WIDGET_TEXT,  ID_BTN_LBL, ID_BTN_BOX,
                               0, 0, 120, 40, "Submit");
    tgs_client_create_element(TGS_WIDGET_TEXT,  ID_RESULT,  0,
                               20, 160, 400, 30, "");

    /* Event loop: the program routes everything. */
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_KEY) {
            /* Printable ASCII appends to the edit buffer; backspace
             * deletes. The renderer never holds the buffer. */
            if (ev.key >= 0x20 && ev.key <= 0x7e && len < (int)sizeof(buf) - 1) {
                buf[len++] = (char)ev.key;
                buf[len] = '\0';
                tgs_client_update_element(ID_INPUT, buf);
            } else if (ev.key == 8 && len > 0) {
                buf[--len] = '\0';
                tgs_client_update_element(ID_INPUT, buf);
            } else if (ev.key == 10 || ev.key == 13) {
                /* Enter submits. */
                char greeting[300];
                snprintf(greeting, sizeof(greeting), "Hello, %s!",
                         len ? buf : "World");
                tgs_client_update_element(ID_RESULT, greeting);
            }
        } else if (ev.type == TGS_EVENT_CLICK && ev.id == ID_BTN_BOX) {
            char greeting[300];
            snprintf(greeting, sizeof(greeting), "Hello, %s!",
                     len ? buf : "World");
            tgs_client_update_element(ID_RESULT, greeting);
        }
    }

    tgs_client_shutdown();
    return 0;
}