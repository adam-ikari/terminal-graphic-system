/*
 * mixed_demo.c — L1: character text and TGS widgets interleaved on one screen.
 *
 * The compositor renders the program's stdout in its character base (deep
 * background, light text). A TRANSPARENT window's root shows no background of
 * its own, so the character text stays visible through it while widgets float
 * on top — character and controls sharing the same screen, the L1 mix.
 *
 * The program prints a text block, opens a transparent window, places a few
 * controls over the text, then prints more text below the controls.
 *
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>

#define ID_TITLE   1
#define ID_INPUT   2
#define ID_BUTTON  3
#define ID_STATUS  4

int main(void)
{
    tgs_window_info win;
    tgs_event ev;
    int i;
    /* Character block first — rendered in the base, visible through the
     * transparent window that comes next. 40 rows (~12px each) span the whole
     * screen, so text is visible around and beneath the control strip. */
    for (i = 0; i < 40; i++) {
        printf("line %2d: terminal text lives in the character base below "
               "the widgets\n", i + 1);
    }
    printf("--- controls float on the text ---\n");
    fflush(stdout);

    /* A TRANSPARENT window: no root background, the text shows through. */
    if (tgs_client_create_window(TGS_WINDOW_TRANSPARENT, "Mixed", &win) != 0)
        return 1;

    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_TITLE, win.window_id,
                             60, 40, 500, 30, "Name:");
    tgs_client_create_widget(TGS_WIDGET_INPUT, ID_INPUT, win.window_id,
                             60, 80, 300, 40, "");
    tgs_client_create_widget(TGS_WIDGET_BUTTON, ID_BUTTON, win.window_id,
                             380, 80, 120, 40, "Submit");
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_STATUS, win.window_id,
                             60, 140, 500, 30, "(widgets above, text below)");

    /* More characters after the widgets — same stream, interleaved. */
    printf("--- text continues below the widgets ---\n");
    fflush(stdout);

    for (;;)
        if (tgs_client_poll_event(&ev, 500) < 0) break;

    tgs_client_shutdown();
    return 0;
}
