/*
 * TGS Client Library API
 * Communicates with compositor over stdin/stdout.
 */
#ifndef TGS_CLIENT_H
#define TGS_CLIENT_H

#include <stdint.h>
 #include "tgs_protocol.h"

typedef struct {
    int window_id;
    int x, y, w, h;
} tgs_window_info;

typedef struct {
    int type;           /* tgs_event_type */
    int window_id;
    int widget_id;
    char text[256];     /* for VALUE_CHANGED events */
    int key;            /* for KEY events */
    int modifiers;      /* for KEY events */
    int focused;        /* for FOCUS/BLUR events: 1=gained, 0=lost */
    int reason;         /* for FOCUS/BLUR events: tgs_focus_reason */
} tgs_event;

int  tgs_client_init(void);
int  tgs_client_create_window(tgs_window_type type, const char *title,
                              tgs_window_info *info);
int  tgs_client_create_widget(tgs_widget_type type, int id, int parent_id,
                              int x, int y, int w, int h, const char *content);
int  tgs_client_update_widget(int id, const char *text);
int  tgs_client_set_widget_style(int id, tgs_style_prop prop, int32_t value);

/* Programmatic focus request (command 38). widget_id 0 clears focus for
 * win_id. The compositor validates the request and answers with NTF_FOCUS. */
int  tgs_client_set_focus(int win_id, int widget_id);

/* Last focus reported for win_id by NTF_FOCUS, cached client-side; 0 = none. */
int  tgs_client_get_focus(int win_id);

/* Behavioural widget attribute (command 40); attr: tgs_widget_attr, value
 * per that attribute's documented range. Appearance stays in
 * tgs_client_set_widget_style. */
int  tgs_client_set_widget_attr(int id, tgs_widget_attr attr, int32_t value);
int  tgs_client_destroy_widget(int id);
int  tgs_client_bind_event(int widget_id, tgs_event_type event);
int  tgs_client_destroy_window(int win_id);
void tgs_client_shutdown(void);
/* Wait for an event. With a blocking timeout (-1) this returns 0 once *ev is
 * filled and -1 when the compositor connection ends. With a finite timeout:
 * 0 = an event arrived (ev filled), 1 = the timeout expired with no event,
 * -1 = the connection ended or failed. */
int  tgs_client_poll_event(tgs_event *ev, int timeout_ms);
const char *tgs_client_get_widget_text(int widget_id);

/* Real widget geometry (x, y, w, h in compositor screen/absolute pixels),
 * cached from NTF_GEOMETRY (69). The compositor emits it asynchronously after
 * laying the widget out, so poll for events first. Returns 0 on hit, nonzero
 * if the geometry is not yet known. Any of x/y/w/h may be NULL. */
int tgs_client_get_widget_geometry(int widget_id, int *x, int *y, int *w, int *h);

/* Send IME_COMMIT event from IME app to compositor */
int tgs_client_send_ime_commit(int win_id, int widget_id, const char *text);

/* Send IME_PREEDIT event from IME app to compositor */
int tgs_client_send_ime_preedit(int win_id, int widget_id, const char *text, int cursor);

/* Send IME_CANCEL (100) — compositor → IME app, drop the active composition. */
int tgs_client_send_ime_cancel(int win_id, int widget_id);

/* Change a container's layout at runtime. Optional: a container's layout is
 * implied by its widget type (VLAYOUT/HLAYOUT/GLAYOUT/SCROLL) at creation.
 * cols/rows apply to GRID (rows 0 = auto); pass TGS_LAYOUT_DEFAULT_COLS/ROWS
 * for the defaults. */
int tgs_client_set_widget_layout(int id, tgs_layout_type layout,
                                 int cols, int rows);

/* Send IME candidate list from IME app to compositor */
int tgs_client_send_ime_candidates(int win_id, int widget_id,
                                   const char *candidates[], int count);

/* Send IME candidate selection from app to compositor */
int tgs_client_send_ime_select(int win_id, int widget_id, int index);

#endif /* TGS_CLIENT_H */
