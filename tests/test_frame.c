/*
 * tests/test_frame.c — Frame encode/decode roundtrip tests.
 * C99, no external dependencies.
 */
#include "tgs_frame.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
static int passed = 0, failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    int prev = failed; \
    printf("  %s ... ", #name); \
    name(); \
    if (failed == prev) { passed++; printf("ok\n"); } \
    else printf("FAIL\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failed++; \
        return; \
    } \
} while(0)


TEST(encode_basic) {
    char buf[1024];
    const char *args[] = {"1", "0", "My Window"};
    int len = tgs_frame_encode(1, 42, 16, args, 3, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strncmp(buf, "TGS;1;42;16", 11) == 0);
    ASSERT(strstr(buf, ";1") != NULL);   /* stream_id */
    ASSERT(strstr(buf, ";42") != NULL);  /* frame_id */
    ASSERT(strstr(buf, ";16") != NULL);  /* command */
    ASSERT(strstr(buf, ";My Window") != NULL);
}

TEST(encode_empty_args) {
    char buf[1024];
    int len = tgs_frame_encode(0, 1, 1, NULL, 0, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strcmp(buf, "TGS;0;1;1") == 0);
}

TEST(encode_overflow) {
    char buf[16];
    const char *args[] = {"verylongargthatexceedsbuffer"};
    int len = tgs_frame_encode(1, 1, 1, args, 1, buf, sizeof(buf));
    ASSERT(len == -1);
}

TEST(decode_basic) {
    const char *payload = "TGS;1;42;16;0;0;My Window";
    tgs_frame f;
    int ret = tgs_frame_decode(payload, strlen(payload), &f);
    ASSERT(ret == 0);
    ASSERT(f.stream_id == 1);
    ASSERT(f.frame_id == 42);
    ASSERT(f.command == 16);
    ASSERT(f.num_args == 3);
    ASSERT(strcmp(f.args[0], "0") == 0);
    ASSERT(strcmp(f.args[1], "0") == 0);
    ASSERT(strcmp(f.args[2], "My Window") == 0);
}

TEST(decode_invalid_prefix) {
    const char *payload = "BAD;1;2;3";
    tgs_frame f;
    int ret = tgs_frame_decode(payload, strlen(payload), &f);
    ASSERT(ret == -1);
}

TEST(decode_empty) {
    tgs_frame f;
    int ret = tgs_frame_decode(NULL, 0, &f);
    ASSERT(ret == -1);
}

TEST(roundtrip) {
    char buf[1024];
    const char *args[] = {"1", "0", "10", "20", "100", "40", "Submit"};
    int len = tgs_frame_encode(1, 99, 32, args, 7, buf, sizeof(buf));
    ASSERT(len > 0);

    tgs_frame f;
    int ret = tgs_frame_decode(buf, len, &f);
    ASSERT(ret == 0);
    ASSERT(f.stream_id == 1);
    ASSERT(f.frame_id == 99);
    ASSERT(f.command == 32);
    ASSERT(f.num_args == 7);
    ASSERT(strcmp(f.args[6], "Submit") == 0);
}

TEST(frame_write) {
    int pipefd[2];
    pipe(pipefd);
    const char *args[] = {"1", "test"};
    int ret = tgs_frame_write(pipefd[1], 1, 1, 16, args, 2);
    ASSERT(ret > 0);
    close(pipefd[0]);
    close(pipefd[1]);
}

int main(void) {
    printf("test_frame:\n");
    RUN(encode_basic);
    RUN(encode_empty_args);
    RUN(encode_overflow);
    RUN(decode_basic);
    RUN(decode_invalid_prefix);
    RUN(decode_empty);
    RUN(roundtrip);
    RUN(frame_write);
    printf("  %d passed, %d failed\n", passed, failed);
    return failed;
}
