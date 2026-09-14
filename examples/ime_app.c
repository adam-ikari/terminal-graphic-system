/*
 * TGS IME Application (Reference Implementation)
 * Direct Input engine: each printable keystroke commits immediately.
 * This is a separate process — NOT part of the compositor.
 */
#include "tgs_client.h"
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (tgs_client_init() != 0) {
        fprintf(stderr, "IME: init failed\n");
        return 1;
    }

    /* Create a hidden tool window */
    tgs_window_info win;
    if (tgs_client_create_window(TGS_WINDOW_TOOL, "IME Engine", &win) != 0) {
        fprintf(stderr, "IME: create window failed\n");
        return 1;
    }

    fprintf(stderr, "IME: running (window %d)\n", win.window_id);

    /* Event loop — receive EVT_KEY from compositor, send IME events back */
    tgs_event ev;
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_KEY) {
            int key = ev.key;

            /* Direct Input: printable ASCII commits immediately */
            if (key >= 0x20 && key <= 0x7e) {
                char committed[2] = { (char)key, '\0' };
                tgs_client_send_ime_commit(ev.window_id, ev.widget_id,
                                           committed);
            }
            /* Non-printable keys: pass through as-is (commit the key code) */
            else if (key == 13) { /* Enter — commit newline */
                tgs_client_send_ime_commit(ev.window_id, ev.widget_id, "\n");
            }
            else if (key == 8 || key == 127) { /* Backspace */
                tgs_client_send_ime_commit(ev.window_id, ev.widget_id, "\b");
            }
            /* Arrow keys, Tab, Escape: pass through unchanged */
            /* (For a full IME, these would control the composition UI) */
        }
    }

    return 0;
}
