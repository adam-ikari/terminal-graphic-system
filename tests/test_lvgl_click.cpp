/*
 * test_lvgl_click.cpp — headless click-coordinate test (D4).
 *
 * Drives the *real* LVGL backend (same code path the compositor uses) with a
 * memory framebuffer, exactly like tools/render_demo.c. No display server.
 *
 * D4: the window root must sit at 0,0 with zero padding so compositor pixel
 * coordinates line up with widget coordinates — a click at a known pixel
 * lands on the widget whose rect contains it, and on no other widget. Before
 * the fix the default theme's card style padded the root's content area
 * (PAD_DEF = 20 px at the 800x600 medium display size, plus a 2 px border),
 * shifting every child by that inset: a click inside the widget's *reported*
 * rect missed it entirely.
 */
#include <gtest/gtest.h>

extern "C" {
#include "tgs_backend.h"
#include "output.h"
extern void lvgl_backend_register(void);
extern void lvgl_backend_set_display(tgs_display *display);
}

#include <cstring>
#include <vector>

namespace {

struct ClickRec {
    void *handle;
    tgs_event_type type;
};

static void record_event(void *handle, tgs_event_type type,
                         const char *data, void *ud)
{
    (void)data;
    auto *v = static_cast<std::vector<ClickRec> *>(ud);
    v->push_back({handle, type});
}

}  // namespace

TEST(LvglBackend, ClickAtKnownPixelHitsExpectedWidget)
{
    tgs_display disp;

    memset(&disp, 0, sizeof(disp));
    lvgl_backend_register();
    tgs_backend *be = tgs_backend_get();
    ASSERT_NE(be, nullptr);
    lvgl_backend_set_display(&disp);
    ASSERT_EQ(be->init(800, 600), 0);

    void *win = be->create_window(TGS_WINDOW_NORMAL, "T");
    ASSERT_NE(win, nullptr);

    std::vector<ClickRec> events;
    be->set_event_callback(record_event, &events);

    /* Button at (20, 110), 120x40 — absolute screen pixels. */
    void *btn = be->create_widget(win, TGS_WIDGET_BUTTON);
    ASSERT_NE(btn, nullptr);
    be->set_widget_rect(btn, 20, 110, 120, 40);
    be->set_widget_content(btn, "OK");

    /* A second clickable widget well away from the click point. */
    void *btn2 = be->create_widget(win, TGS_WIDGET_BUTTON);
    ASSERT_NE(btn2, nullptr);
    be->set_widget_rect(btn2, 400, 300, 100, 30);
    be->set_widget_content(btn2, "No");

    /* Let LVGL apply layout so coords are settled. */
    for (int i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }

    /* Press then release at (30, 120) — inside btn's rect, outside btn2's.
     * LVGL's click-focus consumes the first tap of a click-focusable widget
     * (FOCUSED only, no CLICKED), so tap twice; the second must report the
     * click. With the root padded (pre-fix) the button renders at an offset
     * and even the second tap misses it entirely. */
    for (int tap = 0; tap < 2; tap++) {
        be->inject_mouse(30, 120, 0, 1);
        be->tick(16);
        be->render();
        be->inject_mouse(30, 120, 0, 0);
        be->tick(16);
        be->render();
    }

    bool hit_btn = false, hit_btn2 = false;
    for (const ClickRec &e : events) {
        if (e.type != TGS_EVENT_CLICK) continue;
        if (e.handle == btn) hit_btn = true;
        if (e.handle == btn2) hit_btn2 = true;
    }
    EXPECT_TRUE(hit_btn)
        << "click at (30,120) must land on the button at (20,110,120,40) "
           "— is the window root still padded?";
    EXPECT_FALSE(hit_btn2);

    be->deinit();
}
