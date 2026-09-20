#include "tgs_protocol.h"
#include <gtest/gtest.h>

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
