/*
 * container_demo.c — program-computed layout with plain containers.
 *
 * A container is a widget; the program computes child geometry itself (SVG-
 * scene semantics) and positions each child with its own rect. Nesting is
 * plain parent_id hierarchy.
 *
 * Writes TGS protocol to stdout; debug output goes to stderr.
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>

/* Widget IDs */
#define ID_VLAYOUT      10
#define ID_HLAYOUT      11
#define ID_NAME_LABEL   1
#define ID_NAME_INPUT   2
#define ID_OK_BTN       3
#define ID_RESULT_LABEL 4

int main(void)
{
    tgs_event ev;
    tgs_window_info win;

    if (tgs_client_init() != 0) {
        fprintf(stderr, "container_demo: client init failed\n");
        return 1;
    }

    if (tgs_client_create_window(TGS_WINDOW_NORMAL, "Container Demo", &win) != 0) {
        fprintf(stderr, "container_demo: create window failed\n");
        tgs_client_shutdown();
        return 1;
    }

    /* Container under the window: the program places its children. */
    tgs_client_create_widget(TGS_WIDGET_CONTAINER, ID_VLAYOUT, win.window_id,
                             20, 20, 600, 200, "");

    /* Children at program-computed rects, relative to the container. */
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_NAME_LABEL, ID_VLAYOUT,
                             10, 10, 200, 30, "Name:");

    /* Nested container. */
    tgs_client_create_widget(TGS_WIDGET_CONTAINER, ID_HLAYOUT, ID_VLAYOUT,
                             10, 50, 560, 40, "");

    tgs_client_create_widget(TGS_WIDGET_INPUT, ID_NAME_INPUT, ID_HLAYOUT,
                             0, 0, 300, 40, "");
    tgs_client_create_widget(TGS_WIDGET_CHECKBOX, ID_OK_BTN, ID_HLAYOUT,
                             320, 0, 80, 40, "OK");

    /* Result label sits below the nested container. */
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_RESULT_LABEL, ID_VLAYOUT,
                             0, 0, 400, 30, "");

    /* Bind click on the OK button */
    tgs_client_bind_event(ID_OK_BTN, TGS_EVENT_CLICK);

    /* Event loop */
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_CLICK && ev.widget_id == ID_OK_BTN) {
            const char *name = tgs_client_get_widget_text(ID_NAME_INPUT);
            char greeting[296];
            snprintf(greeting, sizeof(greeting), "Hello, %s!",
                     (name && name[0]) ? name : "World");
            tgs_client_update_widget(ID_RESULT_LABEL, greeting);
        }
    }

    tgs_client_shutdown();
    return 0;
}
