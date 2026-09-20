/*
 * TGS Client Library API (SVG semantics)
 * Communicates with the renderer over stdin/stdout using TGS frames, which
 * ride the kitty graphics APC channel.
 */
#ifndef TGS_CLIENT_H
#define TGS_CLIENT_H

#include <stdint.h>
#include "tgs_protocol.h"

typedef struct {
    int type;           /* tgs_event_type */
    int id;             /* element id (CLICK/HOVER); -1 for KEY/POINTER */
    int key;            /* KEY events */
    int modifiers;      /* KEY events */
    int x, y, phase;    /* POINTER events */
    char text[256];     /* IME_COMMIT text */
} tgs_event;

int  tgs_client_init(void);

/* Elements. parent 0 attaches to the canvas root. */
int  tgs_client_create_element(tgs_widget_type type, int id, int parent,
                               int x, int y, int w, int h,
                               const char *content);
int  tgs_client_update_element(int id, const char *value);
int  tgs_client_set_element_style(int id, tgs_style_prop prop, int32_t value);
int  tgs_client_destroy_element(int id);
void tgs_client_shutdown(void);

/* Wait for an event. With a blocking timeout (-1) this returns 0 once *ev is
 * filled and -1 when the renderer connection ends. With a finite timeout:
 * 0 = an event arrived (ev filled), 1 = the timeout expired with no event,
 * -1 = the connection ended or failed. */
int  tgs_client_poll_event(tgs_event *ev, int timeout_ms);

/* Send IME_COMMIT as the IME program (this client connects as the input
 * front end when it declares ime=true in HELLO). */
int tgs_client_send_ime_commit(const char *text);

#endif /* TGS_CLIENT_H */