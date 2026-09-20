#include "tgs_frame.h"
#include <gtest/gtest.h>
#include <cstring>
#include <unistd.h>

class FrameTest : public ::testing::Test {};

TEST_F(FrameTest, EncodeBasic) {
    char buf[1024];
    const char *args[] = {"1", "0", "My Window"};
    int len = tgs_frame_encode(1, 42, 32, args, 3, buf, sizeof(buf));
    EXPECT_GT(len, 0);
    EXPECT_STREQ(buf, "G1;1;42;32;1;0;My Window");
}

TEST_F(FrameTest, EncodeEmptyArgs) {
    char buf[1024];
    int len = tgs_frame_encode(0, 1, 1, NULL, 0, buf, sizeof(buf));
    EXPECT_GT(len, 0);
    EXPECT_STREQ(buf, "G1;0;1;1");
}

TEST_F(FrameTest, EncodeOverflow) {
    char buf[16];
    const char *args[] = {"verylongargthatexceedsbuffer"};
    int len = tgs_frame_encode(1, 1, 1, args, 1, buf, sizeof(buf));
    EXPECT_EQ(len, -1);
}

TEST_F(FrameTest, DecodeBasic) {
    const char *payload = "G1;1;42;32;0;0;My Window";
    tgs_frame f;
    int ret = tgs_frame_decode(payload, strlen(payload), &f);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(f.stream_id, 1);
    EXPECT_EQ(f.frame_id, 42);
    EXPECT_EQ(f.command, 32);
    EXPECT_EQ(f.num_args, 3);
    EXPECT_STREQ(f.args[0], "0");
    EXPECT_STREQ(f.args[2], "My Window");
}

TEST_F(FrameTest, DecodeInvalidPrefix) {
    const char *payload = "BAD;1;2;3";
    tgs_frame f;
    int ret = tgs_frame_decode(payload, strlen(payload), &f);
    EXPECT_EQ(ret, -1);
}

TEST_F(FrameTest, DecodeNull) {
    tgs_frame f;
    int ret = tgs_frame_decode(NULL, 0, &f);
    EXPECT_EQ(ret, -1);
}

TEST_F(FrameTest, Roundtrip) {
    char buf[1024];
    const char *args[] = {"1", "0", "10", "20", "100", "40", "Submit"};
    int len = tgs_frame_encode(1, 99, 32, args, 7, buf, sizeof(buf));
    EXPECT_GT(len, 0);

    tgs_frame f;
    int ret = tgs_frame_decode(buf, len, &f);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(f.stream_id, 1);
    EXPECT_EQ(f.frame_id, 99);
    EXPECT_EQ(f.command, 32);
    EXPECT_EQ(f.num_args, 7);
    EXPECT_STREQ(f.args[6], "Submit");
}

TEST_F(FrameTest, DecodeTrailingEmptyArg) {
    const char *payload = "G1;1;1;32;1;0;2;10;20;100;40;";
    tgs_frame f;
    ASSERT_EQ(tgs_frame_decode(payload, strlen(payload), &f), 0);
    ASSERT_EQ(f.num_args, 8);
    EXPECT_STREQ(f.args[0], "1");
    EXPECT_STREQ(f.args[6], "40");
    EXPECT_STREQ(f.args[7], "");
}

TEST_F(FrameTest, RoundtripEmptyLastArg) {
    char buf[1024];
    const char *args[] = {"1", "0", "2", "10", "20", "100", "40", ""};
    int len = tgs_frame_encode(1, 1, 32, args, 8, buf, sizeof(buf));
    ASSERT_GT(len, 0);

    tgs_frame f;
    ASSERT_EQ(tgs_frame_decode(buf, len, &f), 0);
    EXPECT_EQ(f.num_args, 8);
    EXPECT_STREQ(f.args[7], "");
}

TEST_F(FrameTest, FrameWrite) {
    int pipefd[2];
    pipe(pipefd);
    const char *args[] = {"1", "test"};
    int ret = tgs_frame_write(pipefd[1], 1, 1, 16, args, 2);
    EXPECT_GT(ret, 0);
    close(pipefd[0]);
    close(pipefd[1]);
}
