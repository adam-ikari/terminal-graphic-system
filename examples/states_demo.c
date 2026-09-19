/*
 * states_demo.c — widget interaction-state demo: hover / click / focus.
 *
 * Lays out a grid of every interactive widget type, binds HOVER_ENTER /
 * HOVER_LEAVE / CLICK, and reacts visibly:
 *
 *   - hover:   widget border turns blue (program's response to the
 *              compositor's HOVER_ENTER event — the compositor owns the hit
 *              test, the program owns the policy)
 *   - click:   a status label reports which widget was clicked
 *   - focus:   the compositor draws the focus outline on the focused widget
 *
 * Drive it with a mouse under the compositor and screenshot each state.
 *
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>

#define ID_STATUS 100

static int g_ids[64];
static int g_n;

static int add_widget(tgs_window_info *win, tgs_widget_type type,
                      int x, int y, int w, int h, const char *content)
{
    int id = 1000 + g_n;

    g_ids[g_n++] = id;
    if (tgs_client_create_widget(type, id, win->window_id,
                                 x, y, w, h, content) != 0)
        return -1;
    /* Subscribe to the interaction notifications this demo reacts to. */
    tgs_client_bind_event(id, TGS_EVENT_HOVER_ENTER);
    tgs_client_bind_event(id, TGS_EVENT_HOVER_LEAVE);
    tgs_client_bind_event(id, TGS_EVENT_CLICK);
    return id;
}

int main(void)
{
    tgs_window_info win;
    tgs_event ev;
    int i;

    if (tgs_client_init() != 0) return 1;
    if (tgs_client_create_window(TGS_WINDOW_NORMAL, "States", &win) != 0)
        return 1;

    /* 5x4 grid of interactive widgets, primitives only. Derived looks are
     * style/attr compositions: radio = round checkbox, "switch" = wide
     * round checkbox. Compound widgets (list/table/tab/dropdown/pickers)
     * are CONTAINER + child widgets at program-computed rects. */
    add_widget(&win, TGS_WIDGET_BUTTON,    20,  20, 140, 56, "Button");
    add_widget(&win, TGS_WIDGET_INPUT,     20,  96, 140, 56, "");
    add_widget(&win, TGS_WIDGET_CHECKBOX,  20, 172, 140, 40, "Check");
    add_widget(&win, TGS_WIDGET_CHECKBOX,  20, 232, 140, 40, "Radio");
    add_widget(&win, TGS_WIDGET_SLIDER,    20, 292, 140, 48, "");
    add_widget(&win, TGS_WIDGET_CHECKBOX,  20, 360, 140, 40, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,180,  20, 160, 48, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,180,  96, 160, 96, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,180, 212, 160, 88, "");
    add_widget(&win, TGS_WIDGET_SCROLL,   180, 320, 160, 80, "");
    add_widget(&win, TGS_WIDGET_SLIDER,   360,  20, 160, 40, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,360,  80, 160, 120, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,360, 220, 160, 100, "");
    add_widget(&win, TGS_WIDGET_CONTAINER,360, 340, 160, 80, "");

    if (tgs_client_create_widget(TGS_WIDGET_LABEL, ID_STATUS, win.window_id,
                                 20, 440, 560, 30,
                                 "hover a widget, then click it") != 0)
        return 1;

    for (;;) {
        int r = tgs_client_poll_event(&ev, 500);

        if (r < 0) break;
        if (r > 0) continue;

        switch (ev.type) {
        case TGS_EVENT_HOVER_ENTER:
            /* Program-side hover policy: blue border on the hovered widget. */
            tgs_client_set_widget_style(ev.widget_id, TGS_STYLE_BORDER_WIDTH, 3);
            tgs_client_set_widget_style(ev.widget_id, TGS_STYLE_BORDER_COLOR,
                                        0x4C9AFF);
            break;
        case TGS_EVENT_HOVER_LEAVE:
            tgs_client_set_widget_style(ev.widget_id, TGS_STYLE_BORDER_WIDTH, 0);
            break;
        case TGS_EVENT_CLICK:
            for (i = 0; i < g_n; i++)
                if (g_ids[i] == ev.widget_id)
                    break;
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "clicked widget %d (%s)",
                         ev.widget_id,
                         i < g_n ? "interactive" : "?");
                tgs_client_update_widget(ID_STATUS, msg);
            }
            break;
        default:
            break;
        }
    }

    tgs_client_shutdown();
    return 0;
}
