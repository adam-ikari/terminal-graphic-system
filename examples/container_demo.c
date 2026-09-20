/*
 * container_demo.c — program-computed layout with BOX elements.
 *
 * BOX is the only structural primitive: children sit at rects the program
 * computes (SVG scene semantics). This demo builds a label row and a
 * two-element row inside a parent box.
 *
 * Writes TGS protocol to stdout; debug output goes to stderr.
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#define ID_ROOT     1   /* BOX */
#define ID_TITLE    2   /* TEXT child of root */
#define ID_ROW      3   /* BOX child of root */
#define ID_LEFT     4   /* TEXT child of row */
#define ID_RIGHT    5   /* TEXT child of row */

int main(void)
{
    if (tgs_client_init() != 0) return 1;

    tgs_client_create_element(TGS_WIDGET_BOX,  ID_ROOT,  0,
                              20, 20, 600, 200, "");
    tgs_client_create_element(TGS_WIDGET_TEXT, ID_TITLE, ID_ROOT,
                              10, 10, 200, 30, "program-computed layout");
    tgs_client_create_element(TGS_WIDGET_BOX,  ID_ROW,   ID_ROOT,
                              10, 50, 560, 40, "");
    tgs_client_create_element(TGS_WIDGET_TEXT, ID_LEFT,  ID_ROW,
                              0, 0, 300, 40, "left");
    tgs_client_create_element(TGS_WIDGET_TEXT, ID_RIGHT, ID_ROW,
                              320, 0, 80, 40, "right");

    for (;;) {
        tgs_event ev;
        if (tgs_client_poll_event(&ev, 500) < 0) break;
    }
    tgs_client_shutdown();
    return 0;
}
