/*
 * test_geometry.cpp — widget geometry notify pass-through (client side).
 *
 * Drives the REAL tgs_client library in-process over pipes, with a fake
 * compositor on the other end: the handshake is completed, then NTF_GEOMETRY
 * frames arrive on the COMMAND stream (as the real compositor sends them after
 * laying a container out). The client must absorb them during poll_event and
 * answer tgs_client_get_widget_geometry() from that cache.
 *
 * This is the consumption half of the path whose emission half is proven by
 * L2Focus::LayoutContainerEmitsGeometry in test_l2_focus.cpp. Deterministic:
 * pipes only, no sleeps beyond the client's own poll timeout.
 */
#include <gtest/gtest.h>

extern "C" {
#include "tgs_frame.h"
#include "tgs_protocol.h"
#include "tgs_client.h"
}

#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <string>

namespace {

/* The client's stdin/stdout are hard-wired to fds 0/1, so the fixture
 * redirects them to pipes for the duration of the test and restores them in
 * TearDown — no other test in this binary touches fds 0/1 in-process. */
class GeometryClient : public ::testing::Test {
protected:
    int to_client[2] = {-1, -1};    /* test writes -> client stdin  */
    int from_client[2] = {-1, -1};  /* client stdout -> test reads  */
    int saved_stdin = -1, saved_stdout = -1;

    void SetUp() override {
        ASSERT_EQ(pipe(to_client), 0);
        ASSERT_EQ(pipe(from_client), 0);
        saved_stdin = dup(STDIN_FILENO);
        saved_stdout = dup(STDOUT_FILENO);
        ASSERT_GE(saved_stdin, 0);
        ASSERT_GE(saved_stdout, 0);
        /* Point the client's stdio at our pipes. */
        ASSERT_GE(dup2(to_client[0], STDIN_FILENO), 0);
        ASSERT_GE(dup2(from_client[1], STDOUT_FILENO), 0);
        close(to_client[0]);    /* the client's side is now fd 0 */
        close(from_client[1]);  /* the client's side is now fd 1 */
        to_client[0] = -1;
        from_client[1] = -1;
    }

    void TearDown() override {
        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        if (to_client[1] >= 0) close(to_client[1]);
        if (from_client[0] >= 0) close(from_client[0]);
    }

    /* Drain one complete APC frame the client wrote to its stdout. */
    bool read_client_frame(tgs_frame &out) {
        char tmp[512];
        struct pollfd pfd{from_client[0], POLLIN, 0};
        int r = poll(&pfd, 1, 2000);
        if (r <= 0 || !(pfd.revents & POLLIN)) return false;
        ssize_t n = read(from_client[0], tmp, sizeof(tmp));
        if (n <= 0) return false;
        std::string buf(tmp, (size_t)n);
        size_t s = buf.find("\x1b_");
        size_t e = buf.find("\x1b\\");
        if (s == std::string::npos || e == std::string::npos) return false;
        std::string payload = buf.substr(s + 2, e - s - 2);
        return tgs_frame_decode(payload.data(), (int)payload.size(), &out) == 0;
    }
};

TEST_F(GeometryClient, CachesCommandStreamGeometry) {
    /* tgs_client_init() blocks THIS thread on stdin waiting for READY, so the
     * READY must already be in the pipe before init is called. */
    const char *caps[1] = {TGS_CAPS_LAYER0};
    ASSERT_GT(tgs_frame_write(to_client[1], TGS_STREAM_HANDSHAKE, 0,
                              TGS_CMD_READY, caps, 1), 0);

    ASSERT_EQ(tgs_client_init(), 0);

    /* Handshake: the client's HELLO landed on our pipe. */
    tgs_frame hello;
    ASSERT_TRUE(read_client_frame(hello));
    ASSERT_EQ(hello.command, TGS_CMD_HELLO);

    /* The compositor lays a container out and reports the result on the
     * COMMAND stream: [widget_id, x, y, w, h]. */
    const char *g1[5] = {"7", "0", "0", "200", "300"};
    const char *g2[5] = {"8", "10", "25", "200", "20"};
    ASSERT_GT(tgs_frame_write(to_client[1], TGS_STREAM_COMMAND, 0,
                              TGS_CMD_NTF_GEOMETRY, g1, 5), 0);
    ASSERT_GT(tgs_frame_write(to_client[1], TGS_STREAM_COMMAND, 0,
                              TGS_CMD_NTF_GEOMETRY, g2, 5), 0);

    /* poll_event absorbs the notifies while scanning: no EVENT frames arrive,
     * so it returns 1 (timeout, no event) — geometry must be cached by then. */
    tgs_event ev;
    ASSERT_EQ(tgs_client_poll_event(&ev, 100), 1);

    int x = -1, y = -1, w = -1, h = -1;
    EXPECT_EQ(tgs_client_get_widget_geometry(7, &x, &y, &w, &h), 0);
    EXPECT_EQ(x, 0);   EXPECT_EQ(y, 0);
    EXPECT_EQ(w, 200); EXPECT_EQ(h, 300);

    EXPECT_EQ(tgs_client_get_widget_geometry(8, &x, &y, &w, &h), 0);
    EXPECT_EQ(x, 10);  EXPECT_EQ(y, 25);
    EXPECT_EQ(w, 200); EXPECT_EQ(h, 20);

    /* A widget that was never notified stays "not known". */
    EXPECT_NE(tgs_client_get_widget_geometry(9, &x, &y, &w, &h), 0);
}

}  // namespace
