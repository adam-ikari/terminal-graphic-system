/*
 * mixed_demo.c — character text and TGS elements interleaved on one screen.
 *
 * The compositor renders the program's stdout in its character base. A
 * TRANSPARENT canvas (character base visible) lets elements float on top of
 * the text: one stdout stream carries both characters and TGS frames.
 */
#include "tgs_client.h"
#include <stdio.h>

#define ID_LABEL 2

int main(void)
{
    tgs_event ev;

    printf("CHAR-BEFORE-HELLO\n");
    fflush(stdout);

    if (tgs_client_init() != 0) return 1;
    /* Canvas root element (parent 0) over the character base. */
    tgs_client_create_element(TGS_WIDGET_TEXT, ID_LABEL, 0,
                              20, 20, 400, 40, "element over the char base");

    printf("CHAR-AFTER-HELLO\n");
    fflush(stdout);

    for (;;) {
        tgs_event ev;
        if (tgs_client_poll_event(&ev, 500) < 0) break;
        (void)ev;
    }
    tgs_client_shutdown();
    return 0;
}
