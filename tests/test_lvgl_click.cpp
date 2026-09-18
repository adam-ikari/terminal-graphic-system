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

/* The LVGL backend is stateful: init() allocates indevs, groups and styles
 * that deinit() does not release, so a second init in the same process
 * accumulates state and wedges the timer loop. All tests in this suite share
 * ONE backend lifetime (suite-level setup), each building its own window. */
class LvglBackend : public ::testing::Test {
protected:
    static tgs_backend *be;
    static tgs_display disp;

    static void SetUpTestSuite() {
        memset(&disp, 0, sizeof(disp));
        lvgl_backend_register();
        be = tgs_backend_get();
        ASSERT_NE(be, nullptr);
        lvgl_backend_set_display(&disp);
        ASSERT_EQ(be->init(800, 600), 0);
    }

    static void TearDownTestSuite() {
        if (be) be->deinit();
    }
};

tgs_backend *LvglBackend::be = nullptr;
tgs_display LvglBackend::disp;

}  // namespace

TEST_F(LvglBackend, ClickAtKnownPixelHitsExpectedWidget)
{
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
    /* The callback's user-data (this test's stack `events`) is about to be
     * destroyed — unregister it or the next test's LVGL events hit a dangling
     * pointer. */
    be->set_event_callback(nullptr, nullptr);
    be->destroy_window(win);
}

/* P3: runtime TGS_LAYOUT_GRID must actually grid children into distinct
 * cells, not silently degrade to a row flex (the pre-fix stub). Three
 * children in a 2-column grid land in two rows: c0 top-left, c1 top-right,
 * c2 bottom-left — so c0 and c1 share a row (same y) with different x, and
 * c2 sits below (larger y). */
TEST_F(LvglBackend, GridLayoutPlacesChildrenInDistinctCells)
{
    void *win = be->create_window(TGS_WINDOW_NORMAL, "T");
    ASSERT_NE(win, nullptr);

    /* A grid container (GLAYOUT), then three children dropped into it. */
    void *grid = be->create_widget(win, TGS_WIDGET_GLAYOUT);
    ASSERT_NE(grid, nullptr);
    be->set_widget_rect(grid, 0, 0, 400, 300);

    void *c0 = be->create_widget(grid, TGS_WIDGET_BUTTON);
    void *c1 = be->create_widget(grid, TGS_WIDGET_BUTTON);
    void *c2 = be->create_widget(grid, TGS_WIDGET_BUTTON);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);

    /* Runtime relayout to a real 2-column grid. */
    be->set_widget_layout(grid, TGS_LAYOUT_GRID, 2, 0);
    for (int i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }

    int x0, y0, w0, h0, x1, y1, w1, h1, x2, y2, w2, h2;
    ASSERT_EQ(be->widget_geometry(c0, &x0, &y0, &w0, &h0), 0);
    ASSERT_EQ(be->widget_geometry(c1, &x1, &y1, &w1, &h1), 0);
    ASSERT_EQ(be->widget_geometry(c2, &x2, &y2, &w2, &h2), 0);

    EXPECT_EQ(y0, y1) << "c0 and c1 share the first grid row";
    EXPECT_NE(x0, x1) << "c0 and c1 are in different columns";
    EXPECT_GT(y2, y0) << "c2 wraps to the second row (grid, not row flex)";
    be->destroy_window(win);
}
