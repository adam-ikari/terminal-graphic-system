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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <string>

namespace {

struct Pty {
    int master = -1;
    pid_t pid = -1;
    tgs_term *term = nullptr;
    std::string out;          /* raw program output, kept for debugging */
    std::string sent;         /* bytes the terminal sent back to the program */
    bool eof = false;         /* the master reported EOF: the program is gone */
    int write_err = 0;        /* errno from the last write to the master, if any */
};

static void reply_cb(const char *bytes, int len, void *ud)
{
    /* What libvterm wants to send to the program (a key, a device reply) goes
     * straight onto the PTY — the same wire the compositor uses. */
    Pty *p = static_cast<Pty *>(ud);
    if (p->master < 0) return;
    p->sent.append(bytes, (size_t)len);
    if (write(p->master, bytes, (size_t)len) != (ssize_t)len)
        p->write_err = errno ? errno : -1;
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
                p.eof = true;
                break;
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

/* A failing PTY test is awkward to reproduce by hand, so it leaves artifacts
 * behind: the final grid, the program's raw bytes, and those bytes with escape
 * boundaries marked (the debugging playbook's first step when a protocol fault
 * is suspected). Set TGS_ARTIFACT_DIR to collect them somewhere findable. */
static std::string annotate(const std::string &s)
{
    std::string out;
    char b[16];
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1b) {
            out += "\n<ESC>";
        } else if (c < 32 || c == 127) {
            std::snprintf(b, sizeof b, "<%02X>", c);
            out += b;
        } else {
            out += (char)c;
        }
    }
    return out;
}

static void capture_on_failure(const Pty &p)
{
    const ::testing::TestInfo *info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    const char *dir;
    std::string path;
    std::ofstream f;
    int rows;

    if (!info || !::testing::Test::HasFailure() || !p.term) return;

    dir = std::getenv("TGS_ARTIFACT_DIR");
    path = std::string(dir ? dir : ".") + "/" + info->name() + ".artifact.txt";
    f.open(path, std::ios::trunc);
    if (!f.good()) return;

    f << "test " << info->test_suite_name() << "." << info->name() << "\n";
    f << "geometry " << tgs_term_cols(p.term) << "x" << tgs_term_rows(p.term)
      << "\n";
    f << "--- final grid ---\n";
    rows = tgs_term_rows(p.term);
    for (int y = 0; y < rows; y++) f << "|" << row_text(p, y) << "|\n";
    f << "--- program output (" << p.out.size() << " bytes) ---\n";
    f << p.out;
    f << "\n--- annotated ---\n" << annotate(p.out) << "\n";

    std::cerr << "[artifact] " << path << "\n";
}

static void teardown(Pty &p)
{
    capture_on_failure(p);
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

/* Poll until the program has visibly reached a point, rather than sleeping a
 * fixed time and hoping (the skill's rule: polling with timeouts, not sleeps).
 * Every test below waits for a marker the script prints before acting. */
static bool pump_until(Pty &p, const std::string &needle, int timeout_ms)
{
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        if (grid_contains(p, needle)) return true;
        pump(p, 40);
        if (elapsed_ms(t0) > timeout_ms) return grid_contains(p, needle);
    }
}

static bool pump_until_eof(Pty &p, int timeout_ms)
{
    auto t0 = std::chrono::steady_clock::now();
    while (!p.eof) {
        pump(p, 40);
        if (elapsed_ms(t0) > timeout_ms) break;
    }
    return p.eof;
}

/* Wait up to timeout_ms for the child to be reaped, pumping output meanwhile.
 *
 * Deadline-based, not iteration-based: once the pty is at EOF poll() returns
 * POLLHUP immediately, so pump() comes back in microseconds and counting
 * iterations collapses the window to nothing — which is exactly how a child
 * that needs a few more milliseconds to finish exiting gets reported as
 * "never exited". */
static bool wait_for_exit(Pty &p, int *status, int timeout_ms)
{
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        pid_t r = waitpid(p.pid, status, WNOHANG);
        if (r == p.pid) return true;
        if (r < 0 && errno != EINTR) return false;
        if (elapsed_ms(t0) >= timeout_ms) return false;

        pump(p, 20);
        if (p.eof) usleep(5000);   /* nothing left to pump; don't spin */
    }
}

}  // namespace
/* Evidence for a signal that never arrived: the line discipline's flags are the
 * thing in question, so report them rather than guessing. */
static std::string tty_flags(int fd)
{
    struct termios t;
    char buf[160];

    if (tcgetattr(fd, &t) != 0) return "tcgetattr(master) failed";
    std::snprintf(buf, sizeof buf,
                  "lflag=%08lX ISIG=%d ICANON=%d ECHO=%d",
                  (unsigned long)t.c_lflag, (t.c_lflag & ISIG) ? 1 : 0,
                  (t.c_lflag & ICANON) ? 1 : 0, (t.c_lflag & ECHO) ? 1 : 0);
    return buf;
}


/* A child's state from /proc: the question when a signal "did not arrive" is
 * whether the process is still there and what it is doing. */
static std::string child_state(pid_t pid)
{
    char path[64];
    std::ifstream f;
    std::string line;
    std::istringstream rest;
    std::string state, ppid, pgrp, session, tty;

    std::snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    f.open(path);
    if (!f.good()) return "gone from /proc";
    std::getline(f, line);

    size_t close = line.rfind(')');
    if (close == std::string::npos || close + 2 >= line.size()) return "unparsable";
    rest.str(line.substr(close + 2));
    rest >> state >> ppid >> pgrp >> session >> tty;
    return "state=" + state + " ppid=" + ppid + " pgrp=" + pgrp +
           " session=" + session + " tty_nr=" + tty;
}
/* What the child is actually running, and its signal dispositions — the answer
 * when a signal is delivered but nothing happens. */
static std::string child_signals(pid_t pid)
{
    char path[64];
    std::ifstream f;
    std::string line, out;

    std::snprintf(path, sizeof path, "/proc/%d/status", (int)pid);
    f.open(path);
    if (!f.good()) return "no status";
    while (std::getline(f, line)) {
        if (line.compare(0, 5, "Name:") == 0 ||
            line.compare(0, 6, "SigBlk") == 0 ||
            line.compare(0, 6, "SigIgn") == 0 ||
            line.compare(0, 6, "SigCgt") == 0)
            out += " | " + line;
    }
    f.close();

    std::snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
    f.open(path);
    if (f.good()) {
        std::getline(f, line, '\0');
        out += " | cmd=" + line;
    }
    return out;
}


/* --- ONLCR: a program's "\n" starts the next line at column 0 --------------- */
TEST(Pty, plain_newlines_render_at_column_zero)
{
    Pty p;
    spawn(p, "printf 'AB\\nCD\\n'; sleep 1", 8, 4);
    ASSERT_TRUE(pump_until(p, "CD", 2000));

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
    spawn(p, "printf READY; read r; printf 'GOT:%s' \"$r\"; sleep 1", 16, 4);
    ASSERT_TRUE(pump_until(p, "READY", 2000));

    tgs_term_key(p.term, 'a', 0);
    tgs_term_key(p.term, 13, 0);   /* Enter -> \r -> ICRNL -> \n */

    EXPECT_TRUE(pump_until(p, "GOT:a", 2000));

    teardown(p);
}

/* --- TIOCSWINSZ reaches the program as SIGWINCH ----------------------------- */
TEST(Pty, resize_reaches_the_program_as_sigwinch)
{
    Pty p;
    /* A short sleep loop, not one long sleep: SIGWINCH lands on the foreground
     * child (which ignores it by default), so the shell only runs the trap
     * between sleeps. */
    spawn(p, "trap 'stty size' WINCH; printf READY; while :; do sleep 0.1; done",
          20, 4);
    ASSERT_TRUE(pump_until(p, "READY", 2000));

    set_winsize(p.master, 30, 8);
    tgs_term_resize(p.term, 30, 8);

    /* stty size prints "<rows> <cols>"; the program saw the new geometry. */
    EXPECT_TRUE(pump_until(p, "8 30", 2000));

    teardown(p);
}

/* --- ISIG: Ctrl-C raises SIGINT instead of reaching the program's stdin ----- */
TEST(Pty, ctrl_c_signals_the_program)
{
    Pty p;
    /* No trap: SIGINT's default action is to kill the program, and that is
     * exactly the terminal's contract. A shell trap would instead test the
     * shell's signal semantics — a different question, and the one that made
     * this test flaky. */
    spawn(p, "printf READY; sleep 30", 20, 2);
    ASSERT_TRUE(pump_until(p, "READY", 2000));

    tgs_term_key(p.term, 3, 0);
    /* The terminal's half of the contract: it put ETX on the wire. */
    ASSERT_NE(p.sent.find('\x03'), std::string::npos)
        << "terminal did not emit ETX for Ctrl-C";

    /* The kernel's half: ISIG turned that into SIGINT. */
    int st = 0;
    bool reaped = wait_for_exit(p, &st, 4000);

    ASSERT_TRUE(reaped) << "child never exited within the poll window; "
                        << tty_flags(p.master)
                        << " write_err=" << p.write_err
                        << " sent=" << p.sent.size() << "B"
                        << " child[" << child_state(p.pid) << "]"
                        << child_signals(p.pid);
    ASSERT_TRUE(WIFSIGNALED(st))
        << "child exited normally (status " << WEXITSTATUS(st)
        << "): Ctrl-C never became a signal; " << tty_flags(p.master);
    EXPECT_EQ(WTERMSIG(st), SIGINT);

    p.pid = -1;   /* teardown must not wait again */
    teardown(p);
}

/* --- both streams are the terminal's: stderr is not special ---------------- */
TEST(Pty, program_stderr_reaches_the_terminal_too)
{
    Pty p;
    spawn(p, "printf 'out'; printf 'err' 1>&2; sleep 1", 16, 3);

    EXPECT_TRUE(pump_until(p, "outerr", 2000));

    teardown(p);
}

/* --- the compositor learns the program is gone from EOF on the PTY --------- */
TEST(Pty, the_terminal_sees_eof_when_the_program_exits)
{
    Pty p;
    spawn(p, "printf 'bye'", 16, 3);

    EXPECT_TRUE(pump_until(p, "bye", 2000));
    /* The same read that delivers output reports the end: that is how the
     * compositor knows to stop, with no extra signal. */
    EXPECT_TRUE(pump_until_eof(p, 2000));

    teardown(p);
}
