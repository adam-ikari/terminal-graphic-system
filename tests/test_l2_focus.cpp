/*
 * test_l2_focus.cpp — headless integration of the compositor focus authority.
 *
 * Drives wm_handle_frame directly with a FAKE tgs_backend (no LVGL, no display,
 * no real PTY): the compositor writes its outgoing frames (NTF_FOCUS, EVT_*,
 * NTF_RESIZE, READY) to a pipe; the test reads and decodes them. The fake
 * backend records set_focus / set_window_ring / set_widget_focusable calls and
 * captures the nav-key precedence hook and the event callback, so the test can
 * (a) invoke the nav hook as input_sdl/inject_key would, and (b) invoke the
 * event callback as lvgl_event_handler would — exercising the same
 * window_manager paths the real backend drives, without a render loop.
 *
 * Proves the L2 contracts the survey flagged unproven:
 *   - WIN_CREATE + first focusable widget → NTF_FOCUS(win,widget,1,INIT)
 *   - Tab / Shift+Tab → NTF_FOCUS with TAB / SHIFT_TAB reason and next/prev widget
 *   - Arrow precedence: non-consuming widget → residual forward (EVT_KEY);
 *     arrow-consuming widget → TGS_NAV_PASS (LVGL owns it)
 *   - SET_FOCUS (programmatic) → NTF_FOCUS(PROGRAMMATIC)
 *   - EVT_KEY / EVT_CLICK delivered to the client as EVT_* frames
 *   - Character output is NOT lost when a window exists — the stream demuxer
 *     still routes non-frame bytes to the text callback
 *
 * Deterministic: poll the pipe (10 ms slices) until a frame arrives, like
 * test_l1/test_pty. No fixed sleeps.
 */
#include <gtest/gtest.h>

extern "C" {
#include "tgs_frame.h"
#include "tgs_protocol.h"
#include "tgs_backend.h"
#include "window_manager.h"
#include "nav.h"
#include "parser.h"
}

#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

/* Canonical key/modifier space (docs/navigation.md §D.4) — the same values
 * input_sdl.c/input_fb.c emit and wm_nav_key switches on. */
enum { KEY_TAB = 9, KEY_LEFT = 1000, KEY_RIGHT = 1001,
       KEY_UP = 1002, KEY_DOWN = 1003 };
enum { MOD_SHIFT = 0x01, MOD_CTRL = 0x02 };

/* ---- Fake backend ----------------------------------------------------------
 * The tgs_backend vtable holds plain C function pointers (no closure), so the
 * fake's state lives in file-scope statics, reset per-test by install_fake().
 * focus_dir returns NULL so a non-consuming widget's arrow becomes residual
 * forward (§D.3 rule 4) — the path the real backend exercises geometrically.
 */
static int            g_widget_seq;     /* creation counter → handle basis   */
static tgs_event_cb   g_event_cb;
static void          *g_event_ud;
static tgs_nav_key_cb g_nav_cb;
static void          *g_nav_cb_ud;
static void          *g_focus_handle;
static int            g_set_focus_calls;
static void          *g_ring_widgets[256];
static int            g_ring_count;
static int            g_focusable_calls;

static int   fb_init(int, int) { return 0; }
static void  fb_tick(uint32_t) {}
static void  fb_deinit(void) {}
static void  fb_render(void) {}
static void  fb_set_size(int, int) {}
static void *fb_create_window(tgs_window_type, const char *) { return (void *)0xAB; }
static void  fb_destroy_window(void *) {}
static void *fb_create_widget(void *, tgs_widget_type) {
    return (void *)(intptr_t)(0x1000 + g_widget_seq++);
}
static void  fb_set_widget_rect(void *, int, int, int, int) {}
static void  fb_set_widget_content(void *, const char *) {}
static void  fb_insert_widget_text(void *, const char *) {}
static void  fb_set_widget_style(void *, tgs_style_prop, int32_t) {}
static void  fb_set_widget_layout(void *, tgs_layout_type) {}
static void  fb_destroy_widget(void *) {}
static void  fb_set_event_callback(tgs_event_cb cb, void *ud) {
    g_event_cb = cb; g_event_ud = ud;
}
static void  fb_inject_mouse(int, int, int, int) {}
static void  fb_inject_key(int, int, int) {}
static void  fb_set_window_ring(void *window, void **widgets, int count) {
    (void)window; g_ring_count = count;
    for (int i = 0; i < count && i < 256; i++) g_ring_widgets[i] = widgets[i];
}
static void  fb_set_active_window(void *) {}
static void  fb_set_focus(void *handle) { g_focus_handle = handle; g_set_focus_calls++; }
static void *fb_focus_dir(void *, void **, int, tgs_nav_dir) { return NULL; }
static void  fb_set_widget_focusable(void *, int) { g_focusable_calls++; }
static void  fb_set_nav_key_cb(tgs_nav_key_cb cb, void *ud) { g_nav_cb = cb; g_nav_cb_ud = ud; }
static const char *fb_clipboard_text(void) { return NULL; }

struct fake_be { tgs_backend base; };

static void install_fake(fake_be *fb) {
    g_widget_seq = 0;
    g_event_cb = nullptr; g_event_ud = nullptr;
    g_nav_cb = nullptr; g_nav_cb_ud = nullptr;
    g_focus_handle = nullptr; g_set_focus_calls = 0;
    g_ring_count = 0; g_focusable_calls = 0;
    memset(g_ring_widgets, 0, sizeof(g_ring_widgets));

    tgs_backend &b = fb->base;
    b.init = fb_init; b.tick = fb_tick; b.deinit = fb_deinit;
    b.render = fb_render; b.set_size = fb_set_size;
    b.create_window = fb_create_window; b.destroy_window = fb_destroy_window;
    b.create_widget = fb_create_widget; b.set_widget_rect = fb_set_widget_rect;
    b.set_widget_content = fb_set_widget_content;
    b.insert_widget_text = fb_insert_widget_text;
    b.set_widget_style = fb_set_widget_style;
    b.set_widget_layout = fb_set_widget_layout;
    b.destroy_widget = fb_destroy_widget;
    b.set_event_callback = fb_set_event_callback;
    b.inject_mouse = fb_inject_mouse; b.inject_key = fb_inject_key;
    b.set_window_ring = fb_set_window_ring;
    b.set_active_window = fb_set_active_window;
    b.set_focus = fb_set_focus;
    b.focus_dir = fb_focus_dir;
    b.set_widget_focusable = fb_set_widget_focusable;
    b.set_nav_key_cb = fb_set_nav_key_cb;
    b.clipboard_text = fb_clipboard_text;
}

/* ---- Pipe transport for compositor→client frames -------------------------
 * The compositor can emit several frames in one write (focus_commit sends
 * lost+gained together), and a single read() may pull them all — the leftover
 * bytes must survive to the next call. g_pipe_buf is cleared per-test. */
static std::string g_pipe_buf;
static int read_frame(int fd, std::string &payload, int timeout_ms) {
    std::string &buf = g_pipe_buf;
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        /* Drain a complete frame already in the buffer BEFORE blocking on
         * poll(): several frames can arrive in one read() and the leftover
         * must be returned by the next call even if the pipe stays quiet. */
        size_t s = buf.find("\x1b_");
        if (s != std::string::npos) {
            size_t e = buf.find("\x1b\\", s + 2);
            if (e != std::string::npos) {
                payload = buf.substr(s + 2, e - s - 2);
                buf.erase(0, e + 2);   /* drop the consumed frame, keep the rest */
                return (int)payload.size();
            }
        } else {
            size_t esc = buf.rfind('\x1b');
            if (esc != std::string::npos && esc + 1 == buf.size())
                buf.erase(0, esc);     /* keep a split ESC for the next read */
            else
                buf.clear();           /* no frame start: drop junk */
        }
        struct pollfd pfd{fd, POLLIN, 0};
        int r = poll(&pfd, 1, 10);
        if (r > 0 && (pfd.revents & POLLIN)) {
            char tmp[512];
            ssize_t n = read(fd, tmp, sizeof(tmp));
            if (n <= 0) return -1;
            buf.append(tmp, (size_t)n);
            continue;
        }
        if (r < 0 && errno != EINTR) return -1;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count() > timeout_ms)
            return -1;
    }
}

static bool decode_frame(const std::string &payload, tgs_frame &f) {
    return tgs_frame_decode(payload.data(), (int)payload.size(), &f) == 0;
}

/* Build a tgs_frame for direct wm_handle_frame calls (no wire encoding). */
static void mkframe(tgs_frame &f, int cmd, std::vector<std::string> args) {
    memset(&f, 0, sizeof(f));
    f.stream_id = TGS_STREAM_COMMAND;
    f.command = cmd;
    f.num_args = (int)args.size();
    for (int i = 0; i < f.num_args && i < TGS_MAX_ARGS; i++)
        snprintf(f.args[i], TGS_MAX_ARG_LEN, "%s", args[i].c_str());
}

/* ---- Test fixture --------------------------------------------------------- */
class L2Focus : public ::testing::Test {
protected:
    fake_be fb;
    window_manager wm;
    int pty_rd = -1, pty_wr = -1;

    void SetUp() override {
        int p[2];
        ASSERT_EQ(pipe(p), 0);
        pty_rd = p[0]; pty_wr = p[1];
        g_pipe_buf.clear();

        install_fake(&fb);
        wm_init(&wm, &fb.base, pty_wr, -1);
        wm.disp_w = 800; wm.disp_h = 600;
        fb.base.set_event_callback(wm_backend_event, &wm);

        do_hello();
        do_win_create(1, "0");
    }

    void TearDown() override {
        if (pty_rd >= 0) close(pty_rd);
        if (pty_wr >= 0) close(pty_wr);
    }

    void do_hello() {
        tgs_frame f;
        mkframe(f, TGS_CMD_HELLO, {TGS_PROTOCOL_VERSION, TGS_CAPS_LAYER0});
        wm_handle_frame(&f, &wm);
        std::string p;
        ASSERT_GT(read_frame(pty_rd, p, 500), 0);
        tgs_frame r; ASSERT_TRUE(decode_frame(p, r));
        ASSERT_EQ(r.command, TGS_CMD_READY);
    }

    void do_win_create(int wid, const char *type) {
        tgs_frame f;
        mkframe(f, TGS_CMD_WIN_CREATE, {std::to_string(wid), type, "T"});
        wm_handle_frame(&f, &wm);
        std::string p;
        ASSERT_GT(read_frame(pty_rd, p, 500), 0);
        tgs_frame r; ASSERT_TRUE(decode_frame(p, r));
        ASSERT_EQ(r.command, TGS_CMD_NTF_RESIZE);
    }

    /* Create a widget via the frame path; returns its backend handle. */
    void *mk_widget(int wid, int parent, const char *type) {
        tgs_frame f;
        mkframe(f, TGS_CMD_WGT_CREATE,
                {std::to_string(wid), std::to_string(parent), type,
                 "0", "0", "100", "40", ""});
        wm_handle_frame(&f, &wm);
        return (void *)(intptr_t)(0x1000 + g_widget_seq - 1);
    }

    /* Read the next emitted frame, asserting it is `cmd`. */
    tgs_frame expect_cmd(int cmd, int timeout_ms = 500) {
        std::string p;
        EXPECT_GT(read_frame(pty_rd, p, timeout_ms), 0)
            << "no frame for cmd " << cmd;
        tgs_frame f; EXPECT_TRUE(decode_frame(p, f));
        EXPECT_EQ(f.command, cmd);
        return f;
    }

    /* Invoke the compositor's nav hook as input would. */
    tgs_nav_key_action nav(int key, int mods, int pressed) {
        return g_nav_cb ? g_nav_cb(key, mods, pressed, g_nav_cb_ud) : TGS_NAV_PASS;
    }
};

/* 1. WIN_CREATE + first focusable widget → NTF_FOCUS(INIT). */
TEST_F(L2Focus, FirstFocusableWidgetGetsInitFocus) {
    void *h10 = mk_widget(10, 1, "button");
    ASSERT_NE(h10, nullptr);

    tgs_frame f = expect_cmd(TGS_CMD_NTF_FOCUS);
    ASSERT_EQ(f.num_args, 4);
    EXPECT_EQ(atoi(f.args[0]), 1);             /* win */
    EXPECT_EQ(atoi(f.args[1]), 10);            /* widget */
    EXPECT_EQ(atoi(f.args[2]), 1);             /* focused */
    EXPECT_EQ(atoi(f.args[3]), TGS_REASON_INIT);
    EXPECT_GT(g_set_focus_calls, 0);
    EXPECT_EQ(g_focus_handle, h10);
}

/* 2. Tab moves to the next widget; reason TAB. */
TEST_F(L2Focus, TabMovesFocusForward) {
    mk_widget(10, 1, "button");
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* INIT */
    void *h11 = mk_widget(11, 1, "button");
    ASSERT_NE(h11, nullptr);

    EXPECT_EQ(nav(KEY_TAB, 0, 1), TGS_NAV_CONSUMED);
    tgs_frame lost = expect_cmd(TGS_CMD_NTF_FOCUS);
    tgs_frame gained = expect_cmd(TGS_CMD_NTF_FOCUS);
    EXPECT_EQ(atoi(lost.args[1]), 10);
    EXPECT_EQ(atoi(lost.args[2]), 0);
    EXPECT_EQ(atoi(lost.args[3]), TGS_REASON_TAB);
    EXPECT_EQ(atoi(gained.args[1]), 11);
    EXPECT_EQ(atoi(gained.args[2]), 1);
    EXPECT_EQ(atoi(gained.args[3]), TGS_REASON_TAB);
    EXPECT_EQ(g_focus_handle, h11);
}

/* 3. Shift+Tab moves back; reason SHIFT_TAB. */
TEST_F(L2Focus, ShiftTabMovesFocusBack) {
    mk_widget(10, 1, "button");
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* INIT on 10 */
    mk_widget(11, 1, "button");
    ASSERT_EQ(nav(KEY_TAB, 0, 1), TGS_NAV_CONSUMED);
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* 10 lost */
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* 11 gained */

    EXPECT_EQ(nav(KEY_TAB, MOD_SHIFT, 1), TGS_NAV_CONSUMED);
    tgs_frame lost = expect_cmd(TGS_CMD_NTF_FOCUS);
    tgs_frame gained = expect_cmd(TGS_CMD_NTF_FOCUS);
    EXPECT_EQ(atoi(lost.args[1]), 11);
    EXPECT_EQ(atoi(lost.args[3]), TGS_REASON_SHIFT_TAB);
    EXPECT_EQ(atoi(gained.args[1]), 10);
    EXPECT_EQ(atoi(gained.args[3]), TGS_REASON_SHIFT_TAB);
}

/* 4. Arrow precedence: non-consuming widget → residual forward (EVT_KEY);
 *    arrow-consuming widget (slider) → TGS_NAV_PASS. */
TEST_F(L2Focus, ArrowPrecedence) {
    mk_widget(10, 1, "button");
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* INIT on 10 (button) */

    /* Button does not consume arrows → nav_dir_move; fake focus_dir = NULL →
     * residual forward_key → EVT_KEY with the raw arrow code. */
    EXPECT_EQ(nav(KEY_LEFT, 0, 1), TGS_NAV_CONSUMED);
    tgs_frame ek = expect_cmd(TGS_CMD_EVT_KEY);
    ASSERT_EQ(ek.num_args, 4);
    EXPECT_EQ(atoi(ek.args[1]), 10);
    EXPECT_EQ(atoi(ek.args[2]), KEY_LEFT);
    EXPECT_EQ(atoi(ek.args[3]), 0);

    /* Slider consumes arrows → PASS (LVGL owns it, no compositor frame). */
    mk_widget(20, 1, "slider");
    tgs_frame sf;
    mkframe(sf, TGS_CMD_SET_FOCUS, {"1", "20"});
    wm_handle_frame(&sf, &wm);
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* 10 lost PROGRAMMATIC */
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* 20 gained PROGRAMMATIC */

    EXPECT_EQ(nav(KEY_LEFT, 0, 1), TGS_NAV_PASS);
    /* No frame should be pending for a PASS. */
    std::string p;
    EXPECT_LT(read_frame(pty_rd, p, 120), 0);
}

/* 5. EVT_KEY / EVT_CLICK delivered to the client as EVT_* frames. */
TEST_F(L2Focus, BackendEventsDelivered) {
    void *h10 = mk_widget(10, 1, "button");
    ASSERT_NE(h10, nullptr);
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* INIT on 10 */

    /* Simulate LVGL reporting a key event on the focused widget. */
    g_event_cb(h10, TGS_EVENT_KEY, "97;0", g_event_ud);
    tgs_frame ek = expect_cmd(TGS_CMD_EVT_KEY);
    ASSERT_EQ(ek.num_args, 4);
    EXPECT_EQ(atoi(ek.args[0]), 1);
    EXPECT_EQ(atoi(ek.args[1]), 10);
    EXPECT_EQ(atoi(ek.args[2]), 97);
    EXPECT_EQ(atoi(ek.args[3]), 0);

    /* Simulate LVGL reporting a click on widget 10. */
    g_event_cb(h10, TGS_EVENT_CLICK, nullptr, g_event_ud);
    tgs_frame ec = expect_cmd(TGS_CMD_EVT_CLICK);
    ASSERT_EQ(ec.num_args, 2);
    EXPECT_EQ(atoi(ec.args[0]), 1);
    EXPECT_EQ(atoi(ec.args[1]), 10);
}

/* 6. Character output is not lost when a window exists: the stream demuxer
 *    routes non-frame bytes to the text callback while frames reach wm. */
TEST_F(L2Focus, CharacterDemuxSurvivesWindow) {
    tgs_parser parser;
    std::string text;
    tgs_parser_init(&parser, wm_handle_frame, &wm);
    tgs_parser_set_text_cb(&parser,
        [](const uint8_t *data, int len, void *ud) {
            static_cast<std::string *>(ud)->append((const char *)data, (size_t)len);
        }, &text);

    const char *wa[] = {"2", "0", "W"};
    char win_payload[128];
    int wl = tgs_frame_encode(TGS_STREAM_COMMAND, 0, TGS_CMD_WIN_CREATE,
                              wa, 3, win_payload, (int)sizeof(win_payload));
    const char *ga[] = {"30", "2", "button", "0", "0", "10", "10", ""};
    char wgt_payload[128];
    int gl = tgs_frame_encode(TGS_STREAM_COMMAND, 0, TGS_CMD_WGT_CREATE,
                              ga, 8, wgt_payload, (int)sizeof(wgt_payload));

    std::string stream;
    stream += "CHAR-BEFORE\n";
    stream += "\x1b_"; stream.append(win_payload, wl); stream += "\x1b\\";
    stream += "CHAR-BETWEEN\n";
    stream += "\x1b_"; stream.append(wgt_payload, gl); stream += "\x1b\\";
    stream += "CHAR-AFTER\n";

    tgs_parser_feed(&parser, (const uint8_t *)stream.data(), (int)stream.size());

    /* All three text markers survived in order. */
    EXPECT_NE(text.find("CHAR-BEFORE\n"), std::string::npos);
    EXPECT_NE(text.find("CHAR-BETWEEN\n"), std::string::npos);
    EXPECT_NE(text.find("CHAR-AFTER\n"), std::string::npos);
    /* And the frame path still emitted NTF_RESIZE + NTF_FOCUS(INIT). */
    expect_cmd(TGS_CMD_NTF_RESIZE);
    expect_cmd(TGS_CMD_NTF_FOCUS);             /* INIT on widget 30 */
}

}  // namespace
