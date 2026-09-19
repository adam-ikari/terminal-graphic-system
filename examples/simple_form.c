/*
 * simple_form.c — Demo app using the TGS client API.
 * Creates a form with label, input, submit button, and result label.
 * Writes TGS protocol to stdout; debug output goes to stderr.
 * C99, links against tgs_client.
 */
#include "tgs_client.h"

#include <stdio.h>
#include <string.h>

/* Widget IDs */
#define ID_PROMPT_LABEL 1
#define ID_NAME_INPUT   2
#define ID_SUBMIT_BTN   3
#define ID_RESULT_LABEL 4

int main(void)
{
    tgs_event ev;
    tgs_window_info win;

    if (tgs_client_init() != 0) {
        fprintf(stderr, "simple_form: client init failed\n");
        return 1;
    }

    if (tgs_client_create_window(TGS_WINDOW_NORMAL, "Simple Form", &win) != 0) {
        fprintf(stderr, "simple_form: create window failed\n");
        tgs_client_shutdown();
        return 1;
    }

    /* Prompt label */
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_PROMPT_LABEL, win.window_id,
                             20, 20, 200, 30, "Enter your name:");

    /* Name input */
    tgs_client_create_widget(TGS_WIDGET_INPUT, ID_NAME_INPUT, win.window_id,
                             20, 60, 300, 40, "");

    /* Submit button */
    tgs_client_create_widget(TGS_WIDGET_CHECKBOX, ID_SUBMIT_BTN, win.window_id,
                             20, 110, 120, 40, "Submit");

    /* Result label (initially empty) */
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_RESULT_LABEL, win.window_id,
                             20, 160, 400, 30, "");

    /* Bind click on the submit button */
    tgs_client_bind_event(ID_SUBMIT_BTN, TGS_EVENT_CLICK);

    /* Event loop */
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_CLICK && ev.widget_id == ID_SUBMIT_BTN) {
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
