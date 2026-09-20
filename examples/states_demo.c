/*
 * states_demo.c — element interaction states (hover / press / click).
 *
 * The program subscribes to nothing: the renderer forwards HOVER/CLICK
 * events for every element (no subscription gate). The program reacts to
 * HOVER_ENTER by restyling the hovered element (its own policy) and to
 * CLICK by flashing it.
 */
#include "tgs_client.h"
#include <stdio.h>

#define N 4
static const int ids[N]   = { 1, 2, 3, 4 };
static const int xs[N]    = { 20, 180, 340, 500 };

int main(void)
{
    int i;
    tgs_event ev;

    if (tgs_client_init() != 0) return 1;

    for (i = 0; i < N; i++) {
        tgs_client_create_element(TGS_WIDGET_BOX, ids[i], 0,
                                  xs[i], 20, 140, 56, "");
        tgs_client_create_element(TGS_WIDGET_TEXT, 100 + ids[i], ids[i],
                                  0, 0, 140, 56, "hover me");
    }

    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_HOVER_ENTER && ev.id > 0 && ev.id < 100) {
            tgs_client_set_element_style(ev.id, TGS_STYLE_BORDER_COLOR,
                                         0x4C9AFF);
            tgs_client_set_element_style(ev.id, TGS_STYLE_BORDER_WIDTH, 3);
        } else if (ev.type == TGS_EVENT_HOVER_LEAVE && ev.id > 0 && ev.id < 100) {
            tgs_client_set_element_style(ev.id, TGS_STYLE_BORDER_WIDTH, 1);
        } else if (ev.type == TGS_EVENT_CLICK && ev.id > 0 && ev.id < 100) {
            tgs_client_set_element_style(ev.id, TGS_STYLE_BG_COLOR, 0x2D5AA8);
        }
    }
    tgs_client_shutdown();
    return 0;
}
