/*
 * PTY integration tests (tui-testing-debugging layer 4).
 *
 * These drive the real (forkpty + libvterm) pair the compositor uses: a program
 * is spawned on a PTY, its output feeds the terminal, keys go back through the
 * reply callback, and a resize is signalled with TIOCSWINSZ. No display is
 * involved, so the suite runs in CI exactly as it does locally.
 *
 * The line discipline is the compositor's (ICANON|ECHO off; ICRNL, ISIG,
 * OPOST|ONLCR on), mirrored here, so these tests also pin what broke when the
 * PTY was put through cfmakeraw: a program's "\n" rendering as CR LF at column
 * 0, Enter arriving as a newline, and a resize reaching the program.
 */
#include <gtest/gtest.h>

extern "C" {
#include "term.h"
}

#include <pty.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <poll.h>

#include <chrono>
#include <cstring>
#include <string>

namespace {

struct Pty {
    int master = -1;
    pid_t pid = -1;
    tgs_term *term = nullptr;
    std::string out;          /* raw program output, kept for debugging */
};

static void reply_cb(const char *bytes, int len, void *ud)
{
    /* What libvterm wants to send to the program (a key, a device reply) goes
     * straight onto the PTY — the same wire the compositor uses. */
    Pty *p = static_cast<Pty *>(ud);
    if (p->master >= 0)
        (void)write(p->master, bytes, (size_t)len);
}

static void set_tty(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_iflag |= ICRNL;
    t.c_oflag |= OPOST | ONLCR;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

static void set_winsize(int fd, int cols, int rows)
{
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    (void)ioctl(fd, TIOCSWINSZ, &ws);
}

static long elapsed_ms(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

/* Pump for up to timeout_ms, feeding program output into the terminal. */
static void pump(Pty &p, int timeout_ms)
{
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        struct pollfd fd{p.master, POLLIN, 0};
        int r = poll(&fd, 1, 40);
        if (r > 0 && (fd.revents & (POLLIN | POLLHUP))) {
            char buf[4096];
            ssize_t n = read(p.master, buf, sizeof buf);
            if (n > 0) {
                p.out.append(buf, (size_t)n);
                tgs_term_feed(p.term, (const uint8_t *)buf, (int)n);
            } else {
                break;   /* EOF */
            }
        }
        if (elapsed_ms(t0) > timeout_ms) break;
    }
}

static std::string row_text(const Pty &p, int row)
{
    const tgs_term_cell *line = tgs_term_view_line(p.term, row);
    int cols = tgs_term_cols(p.term);
    std::string out;
    for (int x = 0; x < cols; x++) {
        uint32_t cp = line ? line[x].cp : ' ';
        out += (cp != 0 && cp < 127) ? (char)cp : ' ';
    }
    return out;
}

static void spawn(Pty &p, const char *script, int cols, int rows)
{
    p.term = tgs_term_new(cols, rows);
    ASSERT_NE(p.term, nullptr);
    tgs_term_set_reply_cb(p.term, reply_cb, &p);

    p.pid = forkpty(&p.master, nullptr, nullptr, nullptr);
    ASSERT_GE(p.pid, 0) << "forkpty failed";
    if (p.pid == 0) {
        set_tty(STDIN_FILENO);
        set_winsize(STDIN_FILENO, cols, rows);
        setenv("TERM", "xterm-256color", 1);
        execlp("sh", "sh", "-c", script, (char *)nullptr);
        _exit(127);
    }
    set_winsize(p.master, cols, rows);
}

static void teardown(Pty &p)
{
    if (p.pid > 0) {
        kill(p.pid, SIGTERM);
        int st;
        waitpid(p.pid, &st, 0);
    }
    if (p.master >= 0) close(p.master);
    if (p.term) tgs_term_free(p.term);
}

/* Stable visible-text search across the grid (the skill prefers final cells to
 * raw bytes). */
static bool grid_contains(Pty &p, const std::string &needle)
{
    int rows = tgs_term_rows(p.term);
    for (int y = 0; y < rows; y++)
        if (row_text(p, y).find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

/* --- ONLCR: a program's "\n" starts the next line at column 0 --------------- */
TEST(Pty, plain_newlines_render_at_column_zero)
{
    Pty p;
    spawn(p, "printf 'AB\\nCD\\n'; sleep 1", 8, 4);
    pump(p, 1500);

    EXPECT_EQ(row_text(p, 0).substr(0, 2), "AB");
    EXPECT_EQ(row_text(p, 1).substr(0, 2), "CD");
    /* The line-discipline failure this guards: LF used to keep the column, so
     * the second line began indented past "AB". */
    EXPECT_EQ(row_text(p, 1)[0], 'C');

    teardown(p);
}

/* --- ICRNL: Enter is a CR on the wire but a NL to the program --------------- */
TEST(Pty, enter_key_arrives_as_a_newline)
{
    Pty p;
    spawn(p, "read r; printf 'GOT:%s' \"$r\"; sleep 1", 16, 4);
    pump(p, 600);   /* let `read` start */

    tgs_term_key(p.term, 'a', 0);
    tgs_term_key(p.term, 13, 0);   /* Enter -> \r -> ICRNL -> \n */
    pump(p, 1500);

    EXPECT_TRUE(grid_contains(p, "GOT:a"));

    teardown(p);
}

/* --- TIOCSWINSZ reaches the program as SIGWINCH ----------------------------- */
TEST(Pty, resize_reaches_the_program_as_sigwinch)
{
    Pty p;
    /* A short sleep loop, not one long sleep: SIGWINCH lands on the foreground
     * child (which ignores it by default), so the shell only runs the trap
     * between sleeps. */
    spawn(p, "trap 'stty size' WINCH; while :; do sleep 0.1; done", 20, 4);
    pump(p, 600);

    set_winsize(p.master, 30, 8);
    tgs_term_resize(p.term, 30, 8);
    pump(p, 2000);

    /* stty size prints "<rows> <cols>"; the program saw the new geometry. */
    EXPECT_TRUE(grid_contains(p, "8 30"));

    teardown(p);
}

/* --- ISIG: Ctrl-C raises SIGINT and the program exits ---------------------- */
TEST(Pty, ctrl_c_signals_the_program)
{
    Pty p;
    spawn(p, "trap 'exit 130' INT; while :; do sleep 0.1; done", 20, 2);
    pump(p, 600);

    tgs_term_key(p.term, 3, 0);   /* Ctrl-C; ISIG -> SIGINT, never reaches stdin */
    int st = 0;
    for (int i = 0; i < 80 && waitpid(p.pid, &st, WNOHANG) == 0; i++)
        pump(p, 50);

    ASSERT_TRUE(WIFSIGNALED(st) ? true : (WIFEXITED(st) && WEXITSTATUS(st) == 130))
        << "child not interrupted; status=" << st;

    p.pid = -1;   /* teardown must not wait again */
    teardown(p);
}
