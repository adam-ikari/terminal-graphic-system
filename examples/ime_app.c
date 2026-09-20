/*
 * ime_app.c — the IME program (reference implementation).
 *
 * A separate process connected to the compositor as the input front end: it
 * receives EVT_KEY from the compositor and commits text back via IME_COMMIT.
 * The candidate window is NOT this protocol's concern — the outer window
 * manager presents it.
 */
#include "tgs_client.h"
#include <string.h>
#include <stdio.h>

int main(void)
{
    if (tgs_client_init() != 0) return 1;

    tgs_event ev;
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type != TGS_EVENT_KEY) continue;
        if (ev.key >= 0x20 && ev.key <= 0x7e) {
            char committed[2] = { (char)ev.key, '\0' };
            tgs_client_send_ime_commit(committed);
        } else if (ev.key == 10 || ev.key == 13) {
            tgs_client_send_ime_commit("\n");
        } else if (ev.key == 8) {
            tgs_client_send_ime_commit("\b");
        }
    }
    return 0;
}
