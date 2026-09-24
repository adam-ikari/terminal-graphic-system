#include "tgs_protocol.h"
#include <gtest/gtest.h>

#include <cstring>
#include <unistd.h>

extern "C" {
#include "tgs_client.h"
#include "tgs_frame.h"
}

class ProtocolTest : public ::testing::Test {};

TEST_F(ProtocolTest, StreamIDs) {
    EXPECT_EQ(TGS_STREAM_HANDSHAKE, 0);
    EXPECT_EQ(TGS_STREAM_COMMAND, 1);
    EXPECT_EQ(TGS_STREAM_EVENT, 4);
}

TEST_F(ProtocolTest, CommandRanges) {
    EXPECT_LT(TGS_CMD_HELLO, TGS_CMD_WGT_CREATE);
    EXPECT_LT(TGS_CMD_WGT_DESTROY, TGS_CMD_NTF_RESIZE);
    EXPECT_LT(TGS_CMD_WGT_DESTROY, TGS_CMD_NTF_RESIZE);
    EXPECT_LT(TGS_CMD_NTF_DESTROY, TGS_CMD_EVT_KEY);
    EXPECT_LT(TGS_CMD_EVT_POINTER, TGS_CMD_IME_COMMIT);
}

TEST_F(ProtocolTest, WidgetCount) {
    EXPECT_GE(TGS_WIDGET_COUNT, 20);
}

TEST_F(ProtocolTest, IMECommands) {
    
    EXPECT_EQ(TGS_CMD_IME_COMMIT, 97);
}

TEST_F(ProtocolTest, ProtocolVersionDefined) {
    EXPECT_STREQ(TGS_PROTOCOL_VERSION, "2.0");
}

/* ── Shared APC channel, client side (spec §8.1 dual identification) ───── */

namespace {

/* Redirect fd 0/1 to a pair of pipes for the duration of a scope and
 * restore them however the scope exits (ASSERT returns early). */
struct StdioPipes {
    int in[2] = {-1, -1};
    int out[2] = {-1, -1};
    int saved_in = -1, saved_out = -1;

    StdioPipes()
    {
        if (::pipe(in) != 0 || ::pipe(out) != 0) return;
        saved_in = ::dup(0);
        saved_out = ::dup(1);
        if (saved_in >= 0) ::dup2(in[0], 0);
        if (saved_out >= 0) ::dup2(out[1], 1);
        ::close(in[0]);
        ::close(out[1]);
    }
    ~StdioPipes()
    {
        if (saved_in >= 0) { ::dup2(saved_in, 0); ::close(saved_in); }
        if (saved_out >= 0) { ::dup2(saved_out, 1); ::close(saved_out); }
        if (in[1] >= 0) ::close(in[1]);
        if (out[0] >= 0) ::close(out[0]);
    }
};

} // namespace

/* The compositor writes a kitty-native graphics acknowledgement to the
 * program's stdin whenever a native frame with i= comes back (q=0/1). The
 * client reads that same stdin — only "G1;" frames are TGS, every other
 * "G…" APC payload must be consumed and skipped, at the handshake and at
 * the event path alike. The ordering below (ack queued before READY) is
 * exactly what a program that transmits a frame just before exec'ing a
 * tgs_client app sees — it used to kill the handshake. */
TEST(ClientInput, nativeAckIsSkippedOnBothReadPaths) {
    StdioPipes io;
    ASSERT_GE(io.saved_in, 0);
    ASSERT_GE(io.saved_out, 0);

    const char *ack = "\x1b_Gi=7,OK\x1b\\";
    const char *ready_args[] = {"0"};
    const char *key_args[] = {"65", "0"};

    ASSERT_GT(::write(io.in[1], ack, std::strlen(ack)), 0);
    ASSERT_GT(tgs_frame_write(io.in[1], TGS_STREAM_HANDSHAKE, 0,
                              TGS_CMD_READY, ready_args, 1), 0);

    int rc = tgs_client_init();
    EXPECT_EQ(rc, 0);

    if (rc == 0) {
        /* The same interleaving on the event path. */
        ASSERT_GT(::write(io.in[1], ack, std::strlen(ack)), 0);
        ASSERT_GT(tgs_frame_write(io.in[1], TGS_STREAM_EVENT, 0,
                                  TGS_CMD_EVT_KEY, key_args, 2), 0);
        tgs_event ev;
        ASSERT_EQ(tgs_client_poll_event(&ev, 500), 0);
        EXPECT_EQ(ev.type, TGS_EVENT_KEY);
        EXPECT_EQ(ev.key, 65);
    }
}
