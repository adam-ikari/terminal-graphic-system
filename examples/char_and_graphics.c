/*
 * char_and_graphics.c — L1: character output and TGS widgets in one program.
 *
 * A TGS program is a terminal program first: it prints ordinary text (which the
 * compositor renders in its character base) and then speaks TGS to open widgets.
 * Both share one stdout; the compositor's stream demultiplexer splits them, and
 * the character path must stay alive across the HELLO/READY handshake.
 *
 * This prints a marker before and after the handshake and opens one window with
 * a label — so a screen at the same time shows the terminal text and the widget.
 *
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>

#define ID_LABEL 2

int main(void)
{
    tgs_event ev;

    /* Character output before any TGS traffic — the terminal base shows it. */
    printf("CHAR-BEFORE-HELLO\n");
    fflush(stdout);

    if (tgs_client_init() != 0) return 1;
    tgs_client_create_element(TGS_WIDGET_TEXT, ID_LABEL, 0,
                              20, 20, 400, 40, "hello from L1");

    /* Character output after the handshake — the character path is still alive. */
    printf("CHAR-AFTER-HELLO\n");
    fflush(stdout);

    /* A finite-timeout poll: 1 = no event this round, 0 = an event arrived,
     * -1 = the compositor connection ended. Idle periods are normal, not errors:
     * the loop just keeps waiting, the way an event loop should. */
    for (;;)
        if (tgs_client_poll_event(&ev, 500) < 0) break;

    tgs_client_shutdown();
    return 0;
}
