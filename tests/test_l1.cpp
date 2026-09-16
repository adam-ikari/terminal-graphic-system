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
            if (start == std::string::npos) { buf.clear(); continue; }
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

    /* The client is now in its poll loop: `poll_event(&ev, 500)` in a loop,
     * breaking when it returns < 0. The fake compositor sends nothing. A
     * correct client stays alive — a quiet timeout is "no event", not a broken
     * connection. The buggy client turned the timeout into -1, broke the loop
     * and exited. Wait past one poll timeout, then reap-probe: kill(pid,0)
     * cannot tell a running child from a zombie, so use waitpid(WNOHANG). */
    int status = 0;
    sleep(1);
    pid_t reaped = waitpid(pid, &status, WNOHANG);
    bool alive = (reaped == 0);           /* 0 = still running, not yet changed */

    if (alive)
        kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    close(to_child[1]);
    close(from_child[0]);

    EXPECT_TRUE(alive) << "client exited during a quiet poll — "
                          "tgs_client_poll_event treated a timeout as a failure";
}
