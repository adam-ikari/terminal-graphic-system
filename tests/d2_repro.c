/*
 * d2_repro.c — red→green proof for D2 (stale key in the TGS_NAV_WIDGET path).
 *
 * Drives the scene backend headless (a memory framebuffer, no display
 * server — same wiring as render_demo). A nav hook returns TGS_NAV_WIDGET for
 * every key, isolating backend_inject_key()'s WIDGET branch, which delivers
 * the key synchronously to the focused widget. The event callback reports
 * "key;mods"; the proof is that the reported key equals the injected one.
 *
 * Build: cmake --build build --target d2_repro
 * Run:   ./build/d2_repro   (prints "D2: PASS" or "D2: FAIL (stale key)")
 */
#include "tgs_backend.h"
#include "output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void scene_backend_register(void);
extern void scene_backend_set_display(tgs_display *display);

static int captured_key   = -999;
static int captured_mods  = -999;
static int got_key_event  = 0;

static void ev_cb(void *widget_handle, tgs_event_type type,
                  const char *event_data, void *user_data)
{
    (void)widget_handle;
    (void)user_data;
    if (type == TGS_EVENT_KEY && event_data) {
        sscanf(event_data, "%d;%d", &captured_key, &captured_mods);
        got_key_event = 1;
    }
}

/* WIDGET for every edge — exercises backend_inject_key()'s WIDGET branch in
 * isolation, independent of the compositor's real precedence logic. */
static tgs_nav_key_action nav_cb(int key, int mods, int pressed, void *ud)
{
    (void)key; (void)mods; (void)pressed; (void)ud;
    return TGS_NAV_WIDGET;
}

int main(void)
{
    tgs_display disp;
    tgs_backend *be;
    void *win, *w;
    void *ring[1];

    memset(&disp, 0, sizeof(disp));

    scene_backend_register();
    be = tgs_backend_get();
    if (!be) { fprintf(stderr, "d2: no backend\n"); return 2; }

    scene_backend_set_display(&disp);
    if (be->init(200, 200) < 0) { fprintf(stderr, "d2: init failed\n"); return 2; }

    be->set_event_callback(ev_cb, NULL);
    be->set_nav_key_cb(nav_cb, NULL);

    win = be->create_window(TGS_WINDOW_NORMAL, "t");
    w   = be->create_widget(win, TGS_WIDGET_INPUT); /* textarea: receives KEY */
    be->set_widget_rect(w, 0, 0, 100, 40);
    be->set_widget_content(w, "");
    ring[0] = w;
    be->set_window_ring(win, ring, 1);
    be->set_active_window(win);
    be->set_focus(w);

    /* 'b' = 98, mods 0. The scene backend delivers the key synchronously in
     * the WIDGET branch, so ev_cb has run by the time we return. */
    be->inject_key(98, 0, 1);
    be->tick(5); /* harmless: drains any residual indev state */

    be->deinit();

    if (!got_key_event) {
        fprintf(stderr, "D2: FAIL (no KEY event captured)\n");
        return 1;
    }
    fprintf(stderr, "D2: reported key=%d mods=%d (expected 98 0)\n",
            captured_key, captured_mods);
    if (captured_key == 98 && captured_mods == 0) {
        fprintf(stderr, "D2: PASS\n");
        return 0;
    }
    fprintf(stderr, "D2: FAIL (stale key)\n");
    return 1;
}
