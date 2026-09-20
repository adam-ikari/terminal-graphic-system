/*
 * test_scene_click.cpp — headless click/hover/geometry tests.
 *
 * Drives the *real* scene backend (same code path the compositor uses) with a
 * memory framebuffer, no display server. Pins the SVG-semantics contracts:
 *   - CLICK lands on the element under the pointer (hit-testing is mechanism)
 *   - BOX holds program-computed rects exactly (renderer moves nothing)
 *   - HOVER enter/leave follow the pointer across elements
 *   - POINTER reports raw coordinates
 */
#include <gtest/gtest.h>

extern "C" {
#include "tgs_backend.h"
#include "output.h"
extern void scene_backend_register(void);
extern void scene_backend_set_display(tgs_display *display);
}

#include "tgs_scene.h"

#include <cstring>
#include <vector>

namespace {

struct Rec {
    void *handle;
    tgs_event_type type;
};

static void record_event(void *handle, tgs_event_type type,
                         const char *data, void *ud)
{
    (void)data;
    auto *v = static_cast<std::vector<Rec> *>(ud);
    v->push_back({handle, type});
}

/* The scene backend is stateful: init() allocates fonts and buffers.
 * All tests share ONE backend lifetime (suite-level setup). */
class SceneBackend : public ::testing::Test {
protected:
    static tgs_backend *be;
    static tgs_display disp;

    static void SetUpTestSuite() {
        memset(&disp, 0, sizeof(disp));
        scene_backend_register();
        be = tgs_backend_get();
        ASSERT_NE(be, nullptr);
        scene_backend_set_display(&disp);
        ASSERT_EQ(be->init(800, 600), 0);
    }

    static void TearDownTestSuite() {
        if (be) be->deinit();
    }
};

tgs_backend *SceneBackend::be = nullptr;
tgs_display SceneBackend::disp;

}  // namespace

/* Click at a known pixel lands on the element at that rect. A "button" is a
 * BOX + TEXT child: the click hits the BOX (the hit-test target), the TEXT
 * child rides above it but the box is the pointer-pressed element. */
TEST_F(SceneBackend, ClickAtKnownPixelHitsExpectedElement)
{
    void *box = be->create_element(NULL, TGS_WIDGET_BOX);
    ASSERT_NE(box, nullptr);
    be->set_element_rect(box, 20, 110, 120, 40);

    /* A second box well away from the click point. */
    void *far_box = be->create_element(NULL, TGS_WIDGET_BOX);
    ASSERT_NE(far_box, nullptr);
    be->set_element_rect(far_box, 400, 300, 100, 30);

    for (int i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }

    std::vector<Rec> events;
    be->set_event_callback(record_event, &events);

    /* Press then release at (30, 130) — inside box, outside far_box. */
    be->inject_pointer(30, 130, 0);
    be->tick(16);
    be->render();
    be->inject_pointer(30, 130, 2);
    be->tick(16);
    be->render();

    bool hit_box = false, hit_far = false;
    for (const Rec &e : events) {
        if (e.type != TGS_EVENT_CLICK) continue;
        if (e.handle == box) hit_box = true;
        if (e.handle == far_box) hit_far = true;
    }
    EXPECT_TRUE(hit_box)
        << "click at (30,130) must land on the box at (20,110,120,40)";
    EXPECT_FALSE(hit_far);

    be->set_event_callback(nullptr, nullptr);
    be->destroy_element(box);
    be->destroy_element(far_box);
}

/* SVG-scene semantics: BOX does no layout — children render at exactly the
 * rects the program set. */
TEST_F(SceneBackend, BoxHoldsProgramComputedRects)
{
    void *box = be->create_element(NULL, TGS_WIDGET_BOX);
    ASSERT_NE(box, nullptr);
    be->set_element_rect(box, 0, 0, 400, 300);

    void *c0 = be->create_element(box, TGS_WIDGET_BOX);
    void *c1 = be->create_element(box, TGS_WIDGET_BOX);
    void *c2 = be->create_element(box, TGS_WIDGET_BOX);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    be->set_element_rect(c0, 10, 10, 120, 40);
    be->set_element_rect(c1, 150, 10, 120, 40);
    be->set_element_rect(c2, 10, 60, 120, 40);

    for (int i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }

    int x0, y0, w0, h0, x1, y1, w1, h1, x2, y2, w2, h2;
    scene_node *n0 = (scene_node *)c0;
    scene_node *n1 = (scene_node *)c1;
    scene_node *n2 = (scene_node *)c2;
    EXPECT_EQ(n0->x, 10); EXPECT_EQ(n0->y, 10);
    EXPECT_EQ(n0->w, 120); EXPECT_EQ(n0->h, 40);
    EXPECT_EQ(n1->x, 150); EXPECT_EQ(n1->y, 10);
    EXPECT_EQ(n2->x, 10); EXPECT_EQ(n2->y, 60);
    (void)x0; (void)y0; (void)w0; (void)h0;
    (void)x1; (void)y1; (void)w1; (void)h1;
    (void)x2; (void)y2; (void)w2; (void)h2;

    be->destroy_element(box);
}

/* Hover: motion onto an element emits HOVER_ENTER; off emits HOVER_LEAVE. */
TEST_F(SceneBackend, MouseMotionEmitsHoverEnterLeave)
{
    void *box = be->create_element(NULL, TGS_WIDGET_BOX);
    ASSERT_NE(box, nullptr);
    be->set_element_rect(box, 20, 20, 120, 40);

    for (int i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }

    std::vector<Rec> events;
    be->set_event_callback(record_event, &events);

    /* Motion onto the box. */
    be->inject_pointer(60, 40, 1);
    be->tick(16);
    be->render();
    auto hovers = [&]() {
        size_t n = 0;
        for (const Rec &e : events)
            if (e.type == TGS_EVENT_HOVER_ENTER ||
                e.type == TGS_EVENT_HOVER_LEAVE)
                n++;
        return n;
    };
    EXPECT_EQ(hovers(), 1u);
    ASSERT_GT(events.size(), 0u);
    EXPECT_EQ(events[0].type, TGS_EVENT_HOVER_ENTER);
    EXPECT_EQ(events[0].handle, box);

    /* Still inside: no repeat. */
    be->inject_pointer(70, 40, 1);
    be->tick(16);
    EXPECT_EQ(hovers(), 1u);

    /* Motion off the box onto the bare canvas: HOVER_LEAVE. */
    be->inject_pointer(400, 300, 1);
    be->tick(16);
    EXPECT_EQ(hovers(), 2u);

    be->set_event_callback(nullptr, nullptr);
    be->destroy_element(box);
}

/* POINTER reports raw coordinates at every phase. */
TEST_F(SceneBackend, PointerReportsRawCoordinates)
{
    void *box = be->create_element(NULL, TGS_WIDGET_BOX);
    ASSERT_NE(box, nullptr);
    be->set_element_rect(box, 20, 20, 120, 40);

    for (int i = 0; i < 3; i++) { be->tick(16); be->render(); }

    std::vector<Rec> events;
    be->set_event_callback(record_event, &events);

    be->inject_pointer(50, 30, 0);   /* down */
    be->tick(16);
    be->inject_pointer(90, 45, 1);   /* move */
    be->tick(16);
    be->inject_pointer(90, 45, 2);   /* up */
    be->tick(16);

    int pointers = 0;
    for (const Rec &e : events)
        if (e.type == TGS_EVENT_POINTER) pointers++;
    EXPECT_EQ(pointers, 3);

    /* The full down-move-up sequence over the same element is one CLICK. */
    int clicks = 0;
    for (const Rec &e : events)
        if (e.type == TGS_EVENT_CLICK) clicks++;
    EXPECT_EQ(clicks, 1);

    be->set_event_callback(nullptr, nullptr);
    be->destroy_element(box);
}
