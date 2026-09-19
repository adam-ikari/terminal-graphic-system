/*
 * Focus model and ring computation tests (src/compositor/nav.c).
 *
 * Pure policy: no LVGL, no display, no compositor. Handles are fake pointers —
 * nav.c only carries them, it never dereferences one.
 */
#include "nav.h"
#include <gtest/gtest.h>
#include <cstdint>

namespace {

const int WIN = 1;

class NavTest : public ::testing::Test {
protected:
    nav_model m;

    void SetUp() override
    {
        nav_init(&m);
        nav_add_window(&m, WIN, TGS_WINDOW_NORMAL, (void *)1);
    }

    /* parent == WIN means the window itself is the parent; the two id spaces
     * are independent, so the kind must be passed explicitly. */
    void add(int id, int parent, tgs_widget_type type)
    {
        ASSERT_EQ(0, nav_add_widget(&m, id, WIN, parent, parent == WIN ? 1 : 0,
                                    type, (void *)(intptr_t)id));
    }

    /* Mirrors the compositor's flow: nav_step decides the target and the
     * caller commits it (window_manager.c focus_commit -> nav_set_focus). */
    int step(int backwards = 0, int *reason = NULL)
    {
        int id = 0, r = TGS_REASON_NONE;

        if (!nav_step(&m, WIN, backwards, &id, &r)) return 0;
        nav_set_focus(&m, WIN, id);
        if (reason) *reason = r;
        return id;
    }

    void focus(int id) { nav_set_focus(&m, WIN, id); }
};


/* Window ids and widget ids are independent spaces (the compositor resolves
 * the parent first). A widget whose id equals its window's id must still be a
 * child of that window — mistaking it for the parent walks the tree forever. */
TEST_F(NavTest, WidgetIdMayEqualWindowId)
{
    add(WIN, WIN, TGS_WIDGET_BUTTON);
    add(2, WIN, TGS_WIDGET_BUTTON);

    focus(WIN);
    EXPECT_EQ(2, step());
    EXPECT_EQ(WIN, step());
}
/* §A.3, §B.3 — containers, labels and progress are never tab stops. */
TEST_F(NavTest, RingSkipsNonFocusableTypes)
{
    add(100, WIN, TGS_WIDGET_CONTAINER);
    add(101, 100, TGS_WIDGET_BUTTON);
    add(102, WIN, TGS_WIDGET_LABEL);
    add(103, 100, TGS_WIDGET_BUTTON);
    add(104, WIN, TGS_WIDGET_PROGRESS);
    add(105, 100, TGS_WIDGET_BUTTON);

    EXPECT_EQ(101, nav_first_focusable(&m, WIN));
    focus(101);
    EXPECT_EQ(103, step());
    EXPECT_EQ(105, step());
    EXPECT_EQ(101, step());          /* wrap forward (§D.2) */
    EXPECT_EQ(105, step(1));         /* wrap backward */
}

/* §B.2 — explicit FOCUS_INDEX sorts first, ascending, ahead of auto entries. */
TEST_F(NavTest, FocusIndexReordersRing)
{
    add(200, WIN, TGS_WIDGET_BUTTON);
    add(201, WIN, TGS_WIDGET_BUTTON);
    add(202, WIN, TGS_WIDGET_BUTTON);

    ASSERT_EQ(1, nav_attr_set(&m, 200, TGS_ATTR_FOCUS_INDEX, 5));
    ASSERT_EQ(1, nav_attr_set(&m, 201, TGS_ATTR_FOCUS_INDEX, 1));

    focus(201);
    EXPECT_EQ(200, step());          /* 1 (201) -> 5 (200) -> auto (202) */
    EXPECT_EQ(202, step());
    EXPECT_EQ(201, step());
}

/* §A.3, §J.4 — the type default can be overridden per widget, and a value
 * outside the documented range is ignored with the old value kept. */
TEST_F(NavTest, AttributeOverridesAndRangeValidation)
{
    nav_widget *label;

    add(300, WIN, TGS_WIDGET_LABEL);
    add(301, WIN, TGS_WIDGET_SLIDER);
    add(302, WIN, TGS_WIDGET_BUTTON);
    add(303, WIN, TGS_WIDGET_CONTAINER);

    label = nav_widget_find(&m, 300);
    EXPECT_EQ(0, nav_widget_focusable(label));
    EXPECT_EQ(1, nav_attr_set(&m, 300, TGS_ATTR_FOCUSABLE, 1));
    EXPECT_EQ(1, nav_widget_focusable(nav_widget_find(&m, 300)));

    EXPECT_EQ(1, nav_widget_consumes_arrows(nav_widget_find(&m, 301)));
    EXPECT_EQ(1, nav_attr_set(&m, 301, TGS_ATTR_NAV_ARROWS, 0));
    EXPECT_EQ(0, nav_widget_consumes_arrows(nav_widget_find(&m, 301)));

    EXPECT_EQ(0, nav_widget_consumes_arrows(nav_widget_find(&m, 302)));
    EXPECT_EQ(0, nav_widget_consumes_tab(nav_widget_find(&m, 302)));
    EXPECT_EQ(1, nav_attr_set(&m, 302, TGS_ATTR_NAV_ARROWS, 1));
    EXPECT_EQ(1, nav_attr_set(&m, 302, TGS_ATTR_NAV_TAB, 1));
    EXPECT_EQ(1, nav_widget_consumes_arrows(nav_widget_find(&m, 302)));
    EXPECT_EQ(1, nav_widget_consumes_tab(nav_widget_find(&m, 302)));

    /* Out of range, unknown widget, scope write on a leaf: all ignored. */
    EXPECT_EQ(0, nav_attr_set(&m, 302, TGS_ATTR_NAV_TAB, 5));
    EXPECT_EQ(1, nav_widget_consumes_tab(nav_widget_find(&m, 302)));
    EXPECT_EQ(0, nav_attr_set(&m, 302, TGS_ATTR_FOCUS_INDEX, -2));
    EXPECT_EQ(0, nav_attr_set(&m, 999, TGS_ATTR_FOCUSABLE, 0));
    EXPECT_EQ(0, nav_attr_set(&m, 302, TGS_ATTR_FOCUS_SCOPE, 2));
    EXPECT_EQ(0, nav_widget_scope(nav_widget_find(&m, 302)));
    EXPECT_EQ(1, nav_attr_set(&m, 303, TGS_ATTR_FOCUS_SCOPE, 1));
    EXPECT_EQ(1, nav_widget_scope(nav_widget_find(&m, 303)));
}

/* §C.1, criterion L10 — a GROUP container is one tab stop; inside it the
 * arrows walk its members and the next Tab leaves the container entirely. */
TEST_F(NavTest, GroupScopeIsSingleTabStop)
{
    void *handles[NAV_RING_MAX];
    int reason = TGS_REASON_NONE;

    add(400, WIN, TGS_WIDGET_SCROLL);
    add(401, 400, TGS_WIDGET_BUTTON);
    add(402, 400, TGS_WIDGET_BUTTON);
    add(403, WIN, TGS_WIDGET_BUTTON);
    ASSERT_EQ(1, nav_attr_set(&m, 400, TGS_ATTR_FOCUS_SCOPE, 1));

    /* The ring is [scope(400), 403]: first member resolves inside the scope. */
    EXPECT_EQ(401, nav_first_focusable(&m, WIN));

    focus(403);
    EXPECT_EQ(401, step(0, &reason)); /* Tab enters it as one stop (§C.1) */
    EXPECT_EQ(TGS_REASON_TAB, reason);
    EXPECT_EQ(2, nav_ring_handles(&m, WIN, handles, NAV_RING_MAX));

    EXPECT_EQ(403, step());          /* next Tab leaves the container */

    /* Re-entering is a scope restore: the member is remembered (§C.2). */
    focus(403);
    EXPECT_EQ(401, step(0, &reason));
    EXPECT_EQ(TGS_REASON_SCOPE_RESTORE, reason);

    focus(402);
    EXPECT_EQ(403, step());
    EXPECT_EQ(402, step(1, &reason));
    EXPECT_EQ(TGS_REASON_SCOPE_RESTORE, reason);

    /* Enrollment still covers every focusable widget of the window (§I.1). */
    EXPECT_EQ(3, nav_all_handles(&m, WIN, handles, NAV_RING_MAX));
}

/* §C.1, criterion L8 — a TRAP wraps inside and never leaks to the outer ring. */
TEST_F(NavTest, TrapScopeWrapsInside)
{
    add(500, WIN, TGS_WIDGET_CONTAINER);
    add(501, 500, TGS_WIDGET_BUTTON);
    add(502, 500, TGS_WIDGET_BUTTON);
    add(503, WIN, TGS_WIDGET_BUTTON);
    ASSERT_EQ(1, nav_attr_set(&m, 500, TGS_ATTR_FOCUS_SCOPE, 2));

    focus(501);
    EXPECT_EQ(502, step());
    EXPECT_EQ(501, step());          /* wraps inside, never reaches 503 */
    EXPECT_EQ(502, step(1));
}

/* §C.1 — a DIALOG window's root ring is a TRAP. */
TEST_F(NavTest, DialogWindowRootIsTrap)
{
    void *handles[NAV_RING_MAX];

    add(600, WIN, TGS_WIDGET_BUTTON);
    ASSERT_EQ(0, nav_add_window(&m, 2, TGS_WINDOW_DIALOG, (void *)2));
    ASSERT_EQ(0, nav_add_widget(&m, 601, 2, 2, 1, TGS_WIDGET_BUTTON, (void *)601));
    ASSERT_EQ(0, nav_add_widget(&m, 602, 2, 2, 1, TGS_WIDGET_BUTTON, (void *)602));

    focus(600);
    EXPECT_EQ(601, nav_first_focusable(&m, 2));
    EXPECT_EQ(2, nav_all_handles(&m, 2, handles, NAV_RING_MAX));
}

/* §D.2, §F.1 — reason map: init, Tab, Shift+Tab and the Home/End edges. */
TEST_F(NavTest, ReasonMap)
{
    int reason = TGS_REASON_NONE;

    add(700, WIN, TGS_WIDGET_BUTTON);
    add(701, WIN, TGS_WIDGET_BUTTON);

    EXPECT_EQ(700, step(0, &reason)); /* nothing focused yet: INIT */
    EXPECT_EQ(TGS_REASON_INIT, reason);
    EXPECT_EQ(701, step(0, &reason));
    EXPECT_EQ(TGS_REASON_TAB, reason);
    EXPECT_EQ(700, step(1, &reason));
    EXPECT_EQ(TGS_REASON_SHIFT_TAB, reason);

    /* Home/End are ring edges and carry ARROW (no HOME/END reason exists). */
    {
        int id = 0;

        focus(700);
        EXPECT_EQ(1, nav_edge(&m, WIN, 1, &id, &reason));
        EXPECT_EQ(701, id);
        EXPECT_EQ(TGS_REASON_ARROW, reason);
        nav_set_focus(&m, WIN, id);
        EXPECT_EQ(1, nav_edge(&m, WIN, 0, &id, &reason));
        EXPECT_EQ(700, id);
    }
}

/* §G.2 — destroy moves to the next ring member, else the previous. */
TEST_F(NavTest, SuccessorAfterDestroy)
{
    add(800, WIN, TGS_WIDGET_BUTTON);
    add(801, WIN, TGS_WIDGET_BUTTON);
    add(802, WIN, TGS_WIDGET_BUTTON);

    focus(801);
    EXPECT_EQ(802, nav_successor(&m, WIN, 801));
    nav_remove_widget(&m, 802);
    EXPECT_EQ(800, nav_successor(&m, WIN, 801));
    nav_remove_widget(&m, 800);
    EXPECT_EQ(0, nav_successor(&m, WIN, 801));
}

/* §G.2 — activation restores the remembered widget, or the first member. */
TEST_F(NavTest, RestoreRemembersLastFocus)
{
    int target = 0;

    add(900, WIN, TGS_WIDGET_BUTTON);
    add(901, WIN, TGS_WIDGET_BUTTON);

    focus(901);
    nav_set_focus(&m, WIN, 0);
    EXPECT_EQ(1, nav_restore(&m, WIN, &target));
    EXPECT_EQ(901, target);

    nav_remove_widget(&m, 901);
    EXPECT_EQ(0, nav_restore(&m, WIN, &target));
    EXPECT_EQ(900, target);
}

/* A widget that stops being focusable leaves the ring, and the next traversal
 * recovers instead of stalling on it (§J.4). */
TEST_F(NavTest, UnfocusableFocusedWidgetRecoversOnNextStep)
{
    add(1000, WIN, TGS_WIDGET_BUTTON);
    add(1001, WIN, TGS_WIDGET_BUTTON);

    focus(1000);
    ASSERT_EQ(1, nav_attr_set(&m, 1000, TGS_ATTR_FOCUSABLE, 0));
    EXPECT_EQ(1001, step());
}

} /* namespace */
