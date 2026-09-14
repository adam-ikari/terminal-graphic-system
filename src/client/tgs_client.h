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
    int focused;        /* for FOCUS events: 1=gained, 0=lost */
} tgs_event;

int  tgs_client_init(void);
int  tgs_client_create_window(tgs_window_type type, const char *title,
                              tgs_window_info *info);
int  tgs_client_create_widget(tgs_widget_type type, int id, int parent_id,
                              int x, int y, int w, int h, const char *content);
int  tgs_client_update_widget(int id, const char *text);
int  tgs_client_set_widget_style(int id, tgs_style_prop prop, int32_t value);
int  tgs_client_destroy_widget(int id);
int  tgs_client_bind_event(int widget_id, tgs_event_type event);
int  tgs_client_destroy_window(int win_id);
void tgs_client_shutdown(void);
int  tgs_client_poll_event(tgs_event *ev, int timeout_ms);
const char *tgs_client_get_widget_text(int widget_id);

/* Send IME_COMMIT event from IME app to compositor */
int tgs_client_send_ime_commit(int win_id, int widget_id, const char *text);

/* Send IME_PREEDIT event from IME app to compositor */
int tgs_client_send_ime_preedit(int win_id, int widget_id, const char *text, int cursor);

#endif /* TGS_CLIENT_H */
