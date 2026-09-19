/*
 * nav_probe.c — end-to-end navigation probe (not a demo app).
 *
 * Every event the compositor sends is appended to a trace file (argv[1],
 * default /tmp/tgs-nav-probe.log): stdout is the protocol stream, so a probe
 * cannot log there. The widget set is fixed and documented, so a driver
 * (xdotool over Xvfb) can assert on the trace:
 *
 *   id 1  LABEL  "probe"    never focusable (§A.3), first child of the window
 *   id 10 SLIDER            first focusable -> initial focus, consumes arrows
 *   id 11 LABEL  "slider"   never focusable, transparent for Tab (§B.3)
 *   id 12 BUTTON "Second"   second tab stop
 *
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>
#include <stdlib.h>

#define ID_TITLE     1
#define ID_SLIDER    10
#define ID_SLIDER_LABEL 11
#define ID_BUTTON    12

static const char *reason_name(int reason)
{
    switch (reason) {
    case TGS_REASON_NONE:            return "NONE";
    case TGS_REASON_TAB:             return "TAB";
    case TGS_REASON_SHIFT_TAB:       return "SHIFT_TAB";
    case TGS_REASON_ARROW:           return "ARROW";
    case TGS_REASON_POINTER:         return "POINTER";
    case TGS_REASON_PROGRAMMATIC:    return "PROGRAMMATIC";
    case TGS_REASON_INIT:            return "INIT";
    case TGS_REASON_WINDOW_ACTIVATE: return "WINDOW_ACTIVATE";
    case TGS_REASON_WINDOW_RESTORE:  return "WINDOW_RESTORE";
    case TGS_REASON_SCOPE_RESTORE:   return "SCOPE_RESTORE";
    case TGS_REASON_DESTROYED:       return "DESTROYED";
    case TGS_REASON_HIDDEN:          return "HIDDEN";
    default:                         return "?";
    }
}

int main(int argc, char *argv[])
{
    const char *trace_path = argc > 1 ? argv[1] : "/tmp/tgs-nav-probe.log";
    FILE *trace = fopen(trace_path, "w");
    tgs_event ev;
    tgs_window_info win;

    if (!trace) return 1;
    setvbuf(trace, NULL, _IOLBF, 0);

    if (tgs_client_init() != 0) {
        fprintf(trace, "error client-init\n");
        return 1;
    }
    if (tgs_client_create_window(TGS_WINDOW_NORMAL, "Nav Probe", &win) != 0) {
        fprintf(trace, "error create-window\n");
        return 1;
    }

    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_TITLE, win.window_id,
                             20, 15, 200, 30, "probe");
    tgs_client_create_widget(TGS_WIDGET_SLIDER, ID_SLIDER, win.window_id,
                             20, 60, 320, 30, "");
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_SLIDER_LABEL, win.window_id,
                             20, 100, 320, 30, "slider");
    tgs_client_create_widget(TGS_WIDGET_CHECKBOX, ID_BUTTON, win.window_id,
                             380, 60, 200, 40, "Second");
    tgs_client_bind_event(ID_BUTTON, TGS_EVENT_CLICK);

    fprintf(trace, "ready win=%d\n", win.window_id);

    while (tgs_client_poll_event(&ev, -1) == 0) {
        switch (ev.type) {
        case TGS_EVENT_FOCUS:
        case TGS_EVENT_BLUR:
            fprintf(trace, "focus widget=%d focused=%d reason=%s(%d)\n",
                    ev.widget_id, ev.focused, reason_name(ev.reason), ev.reason);
            break;
        case TGS_EVENT_KEY:
            fprintf(trace, "key widget=%d key=%d mods=%d\n",
                    ev.widget_id, ev.key, ev.modifiers);
            break;
        case TGS_EVENT_VALUE_CHANGED:
            fprintf(trace, "value widget=%d text=%s\n", ev.widget_id, ev.text);
            break;
        case TGS_EVENT_CLICK:
            fprintf(trace, "click widget=%d\n", ev.widget_id);
            break;
        default:
            break;
        }
    }

    fprintf(trace, "app-exit\n");
    tgs_client_shutdown();
    return 0;
}
