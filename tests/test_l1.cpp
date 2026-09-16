/*
 * L1 client regression test (tui-testing-debugging layer 4).
 *
 * Drives the real char_and_graphics binary with a fake compositor over pipes —
 * no display is involved, so it runs in CI. The handshake is done, then the
 * fake compositor stays quiet. A correct client keeps polling (tgs_client_poll_event
 * returns 1 = no event, not -1 = broken); a regressed client exits the instant
 * the first timeout fires.
 */
#include <gtest/gtest.h>

extern "C" {
#include "tgs_frame.h"
#include "tgs_protocol.h"
}

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {

/* Read one APC frame (ESC_ ... ESC\) from fd, into `payload`. Returns the
 * payload length, or -1 on EOF/error/timeout. Non-frame bytes (a program's
 * ordinary text output) are discarded up to the next ESC_. */
int read_frame(int fd, std::string &payload, int timeout_ms)
{
    auto t0 = std::chrono::steady_clock::now();
    std::string buf;
    for (;;) {
        struct pollfd pfd{fd, POLLIN, 0};
        int r = poll(&pfd, 1, 100);
        if (r > 0 && (pfd.revents & POLLIN)) {
            char tmp[512];
            ssize_t n = read(fd, tmp, sizeof tmp);
            if (n <= 0) return -1;
            buf.append(tmp, (size_t)n);
            size_t start = buf.find("\x1b_");
            if (start == std::string::npos) {
                /* No frame header yet. Drop everything except a lone trailing
                 * ESC (0x1b): a read() can split the 2-byte ESC_ header across
                 * calls, and discarding that byte would orphan the next read's
                 * '_' and lose the frame. */
                size_t esc = buf.rfind('\x1b');
                if (esc != std::string::npos && esc + 1 == buf.size())
                    buf.erase(0, esc);
                else
                    buf.clear();
                continue;
            }
            size_t end = buf.find("\x1b\\", start + 2);
            if (end == std::string::npos) continue;
            payload = buf.substr(start + 2, end - start - 2);
            return (int)payload.size();
        }
        if (r < 0 && errno != EINTR) return -1;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count() > timeout_ms)
            return -1;
    }
}

}  // namespace

TEST(L1Client, stays_alive_during_a_quiet_poll)
{
    int to_child[2], from_child[2];
    ASSERT_EQ(pipe(to_child), 0);
    ASSERT_EQ(pipe(from_child), 0);

    pid_t pid = fork();
    ASSERT_GE(pid, 0) << "fork failed";
    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        const char *path = std::getenv("CHAR_AND_GRAPHICS");
        if (!path) path = "./build/char_and_graphics";
        execl(path, path, (char *)nullptr);
        _exit(127);
    }
    close(to_child[0]); close(from_child[1]);

    std::string payload;

    /* HELLO -> READY */
    ASSERT_GT(read_frame(from_child[0], payload, 3000), 0)
        << "no HELLO from the client";
    {
        const char *args[] = { TGS_CAPS_LAYER0 };
        tgs_frame_write(to_child[1], TGS_STREAM_HANDSHAKE, 2, TGS_CMD_READY,
                        args, 1);
    }

    /* WIN_CREATE -> NTF_RESIZE */
    ASSERT_GT(read_frame(from_child[0], payload, 3000), 0)
        << "no WIN_CREATE from the client";
    {
        const char *args[] = { "1", "0", "0", "800", "600" };
        tgs_frame_write(to_child[1], TGS_STREAM_COMMAND, 1, TGS_CMD_NTF_RESIZE,
                        args, 5);
    }

    /* CREATE_WIDGET (the client does not wait for a reply) */
    ASSERT_GT(read_frame(from_child[0], payload, 3000), 0)
        << "no CREATE_WIDGET from the client";

    /* The client is now in its poll loop: `poll_event(&ev, 500)` in a `for(;;)`
     * that breaks when the call returns < 0. The fake compositor sends nothing
     * further. A correct client stays alive — a quiet timeout is "no event"
     * (returns 1), not a broken connection. The buggy client folded the quiet
     * timeout into -1, broke the loop and exited.
     *
     * Poll for the child's death rather than sleeping a fixed span: the poll
     * timeout inside the client is 500 ms, so a buggy exit lands ~500 ms after
     * the handshake. A deadline-driven probe catches the exit whenever it
     * happens within the window; a fixed sleep could be too short on a loaded
     * box (false green) or just barely long enough (flaky). */
    int status = 0;
    bool alive = true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t reaped = waitpid(pid, &status, WNOHANG);  /* reap-probe: kill(pid,0)
                                                         * cannot tell a running
                                                         * child from a zombie */
        if (reaped == pid) { alive = false; break; }    /* child exited in the window */
        usleep(50 * 1000);
    }

    if (alive)
        kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    close(to_child[1]);
    close(from_child[0]);

    EXPECT_TRUE(alive) << "client exited during a quiet poll — "
                          "tgs_client_poll_event treated a timeout as a failure";
}

/* read_frame must not lose a frame when the 2-byte ESC_ header is split across
 * reads. The naive "clear the buffer if no header found" drops a trailing lone
 * ESC, orphaning the next read's '_' and losing the frame forever — a silent
 * byte-loss bug in the harness itself, not the code under test. */
TEST(L1Client, read_frame_reassembles_a_split_ESC_header)
{
    int pfd[2];
    ASSERT_EQ(pipe(pfd), 0);

    /* Feed the frame one byte at a time so every read() sees a partial frame,
     * including the moment the buffer ends on a lone '\x1b' (start of ESC_). */
    std::string frame = "\x1b_TGS;1;0;10;HELLO\x1b\\";

    pid_t wpid = fork();
    ASSERT_GE(wpid, 0);
    if (wpid == 0) {
        close(pfd[0]);
        for (char c : frame) {
            ssize_t n = write(pfd[1], &c, 1);  /* deliberately split */
            (void)n;
            usleep(5000);
        }
        close(pfd[1]);
        _exit(0);
    }
    close(pfd[1]);

    std::string payload;
    int len = read_frame(pfd[0], payload, 2000);
    close(pfd[0]);
    int st;
    waitpid(wpid, &st, 0);

    EXPECT_GT(len, 0) << "read_frame lost a frame whose ESC_ header was split";
    EXPECT_EQ(payload, "TGS;1;0;10;HELLO");
}
