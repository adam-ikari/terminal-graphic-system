/*
 * TGS Compositor Main
 * PTY fork, poll loop, handshake, backend wiring.
 * Uses output abstraction (SDL display) + input abstraction (SDL events).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <sys/wait.h>

#include <sys/ioctl.h>
#include "tgs_protocol.h"
#include "tgs_frame.h"
#include "tgs_backend.h"
#include "parser.h"
#include "window_manager.h"
#include "output.h"
#include "input.h"
#include "term.h"
#include "lvgl_term.h"

/* LVGL backend registration — defined in lvgl_backend.c */
extern void lvgl_backend_register(void);
extern void lvgl_backend_set_display(tgs_display *display);

/* ---- Character base (L0) ------------------------------------------------
 * A program that never sends a TGS frame is a character program. Its output
 * drives this terminal and its keys go back as terminal bytes; a program that
 * speaks TGS first (HELLO) gets the widget path instead. Both may happen in
 * one program — the stream is split, character output is never lost. */
static tgs_term   *g_term;
static lv_obj_t   *g_term_view;
static int         g_master_fd = -1;
static window_manager *g_wm;

static void term_text_cb(const uint8_t *data, int len, void *ud)
{
    (void)ud;
    tgs_term_feed(g_term, data, len);
}

static void term_reply_cb(const char *bytes, int len, void *ud)
{
    (void)ud;
    if (g_master_fd >= 0) {
        ssize_t n = write(g_master_fd, bytes, (size_t)len);
        (void)n;
    }
}

static void term_key_sink(int key, int mods, int pressed, void *ud)
{
    (void)ud;
    /* Until the program speaks TGS it is a character program, and a character
     * program gets exactly the bytes a terminal would send it. */
    if (!g_wm || g_wm->hello_received || !pressed) return;
    tgs_term_key(g_term, key, mods);
}

/* Debug aid: dump the character grid as text (TGS_TERM_DUMP=<path>). When the
 * picture and the bytes disagree, the grid is the arbitration artifact. */
static void term_dump(void)
{
    const char *path = getenv("TGS_TERM_DUMP");
    const tgs_term_cell *cells;
    FILE *f;
    int x, y, cols, rows;

    if (!path || !g_term) return;
    f = fopen(path, "w");
    if (!f) return;
    cols = tgs_term_cols(g_term);
    rows = tgs_term_rows(g_term);
    cells = tgs_term_cells(g_term);
    fprintf(f, "# grid %dx%d cursor %d,%d visible %d\n",
            cols, rows, tgs_term_cx(g_term), tgs_term_cy(g_term),
            tgs_term_cursor_visible(g_term));
    for (y = 0; y < rows; y++) {
        for (x = 0; x < cols; x++) {
            uint32_t cp = cells[(size_t)y * (size_t)cols + (size_t)x].cp;
            fputc((cp >= 32 && cp < 127) ? (int)cp : (cp ? '.' : ' '), f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

/* Debug aid: append the bytes read from the program (TGS_RAW_DUMP=<path>).
 * Comparing this with what the emulator produced localises a rendering fault
 * to the input side or the emulator side. */
static void raw_dump(const uint8_t *data, int len)
{
    const char *path = getenv("TGS_RAW_DUMP");
    FILE *f;

    if (!path || len <= 0) return;
    f = fopen(path, "ab");
    if (!f) return;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
}

static volatile sig_atomic_t running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Put the app's PTY slave into raw mode so binary protocol frames are
 * delivered byte-for-byte without line-discipline buffering or echo. */
static void pty_set_raw(void)
{
    struct termios tio;

    if (tcgetattr(STDIN_FILENO, &tio) != 0) return;
    cfmakeraw(&tio);
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

int main(int argc, char *argv[])
{
    int master_fd;
    int ime_master_fd = -1;
    int status;
    tgs_backend *be;
    window_manager wm;
    tgs_parser parser;
    pid_t child_pid;
    pid_t ime_pid = -1;
    tgs_display disp;
    uint8_t rbuf[4096];
    struct pollfd fds[3];
    int term_cols, term_rows;   /* the character grid the compositor emulates */
    struct winsize ws;

    (void)argc;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <app> [args...]\n", argv[0]);
        return 1;
    }

    /* Initialize display */
    if (output_init(&disp, 800, 600) < 0) {
        fprintf(stderr, "Display init failed\n");
        return 1;
    }

    /* Signal handling */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Register LVGL backend */
    lvgl_backend_register();
    be = tgs_backend_get();
    if (!be) {
        fprintf(stderr, "No backend registered\n");
        output_cleanup(&disp);
        return 1;
    }

    /* Pass display to backend before init */
    lvgl_backend_set_display(&disp);

    /* Initialize backend */
    if (be->init(disp.width, disp.height) < 0) {
        fprintf(stderr, "Backend init failed\n");
        output_cleanup(&disp);
        return 1;
    }

    /* The grid this compositor emulates. The child must see it through
     * TIOCGWINSZ *before* it execs: a TUI reads the size once at startup and
     * lays its entire screen out from it — htop would otherwise draw itself an
     * 80x24 screen inside a 100x37 grid. */
    term_cols = disp.width / tgs_term_view_cell_w();
    term_rows = disp.height / tgs_term_view_cell_h();
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)term_cols;
    ws.ws_row = (unsigned short)term_rows;

    /* Fork PTY */
    child_pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (child_pid < 0) {
        perror("forkpty");
        be->deinit();
        output_cleanup(&disp);
        return 1;
    }

    if (child_pid == 0) {
        pty_set_raw();
        /* The compositor *is* the terminal, so it advertises what it actually
         * emulates. Inheriting the ambient TERM is wrong: under a script or a
         * dumb parent (TERM=dumb) ncurses degrades every TUI to plain text,
         * and the emulator would never be exercised at all. */
        setenv("TERM", "xterm-256color", 1);
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(1);
    }

    /* Parent: compositor */

    /* Try to launch IME app */
    {
        const char *ime_path = "ime_app";
        ime_pid = forkpty(&ime_master_fd, NULL, NULL, NULL);
        if (ime_pid == 0) {
            pty_set_raw();
            execlp(ime_path, ime_path, (char *)NULL);
            _exit(1);
        }
        if (ime_pid < 0) {
            ime_master_fd = -1;
        }
    }

    wm_init(&wm, be, master_fd, ime_master_fd);
    wm.disp_w = disp.width;
    wm.disp_h = disp.height;

    be->set_event_callback(wm_backend_event, &wm);

    tgs_parser_init(&parser, wm_handle_frame, &wm);

    /* The handshake is program-initiated (see wm_handle_frame): a program
     * that wants TGS sends HELLO and gets READY; a character program is never
     * written to. Nothing to do here but run. */

    /* Initialize input */
    input_init(be);

    /* Character base: always present, widgets composite above it. */
    {
        int cols = term_cols;
        int rows = term_rows;

        g_term = tgs_term_new(cols, rows);
        if (!g_term) {
            fprintf(stderr, "terminal init failed\n");
            be->deinit();
            output_cleanup(&disp);
            return 1;
        }
        g_term_view = tgs_term_view_create(cols, rows);
        if (!g_term_view) {
            fprintf(stderr, "terminal view failed\n");
            tgs_term_free(g_term);
            be->deinit();
            output_cleanup(&disp);
            return 1;
        }
        tgs_term_set_reply_cb(g_term, term_reply_cb, NULL);
        tgs_parser_set_text_cb(&parser, term_text_cb, NULL);
        g_master_fd = master_fd;
        g_wm = &wm;
        input_set_key_sink(term_key_sink, NULL);
    }

    /* Main poll loop */
    fds[0].fd = master_fd;
    fds[0].events = POLLIN;
    fds[1].fd = ime_master_fd;
    fds[1].events = POLLIN;

    while (running) {
        int nfds = (ime_master_fd >= 0) ? 2 : 1;
        int ret = poll(fds, nfds, 5);

        if (ret < 0) break;

        /* App output (protocol frames) */
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(master_fd, rbuf, sizeof(rbuf));
            if (n > 0) {
                raw_dump(rbuf, (int)n);
                tgs_parser_feed(&parser, rbuf, (int)n);
            } else if (n <= 0) {
                break;
            }
        }

        if (fds[0].revents & (POLLHUP | POLLERR)) break;

        /* IME app output */
        if (ime_master_fd >= 0 && (fds[1].revents & POLLIN)) {
            ssize_t n = read(ime_master_fd, rbuf, sizeof(rbuf));
            if (n > 0) {
                tgs_parser_feed(&parser, rbuf, (int)n);
            }
        }

        /* IME app exited */
        if (ime_master_fd >= 0 && (fds[1].revents & (POLLHUP | POLLERR))) {
            ime_master_fd = -1;
            wm.ime_pty_fd = -1;
            wm.ime_connected = 0;
            fds[1].fd = -1;
        }

        /* Poll SDL input */
        input_poll();

        /* LVGL tick */
        be->tick(10);

        /* Pump LVGL's refresh timers — without this the draw buffer is never
         * painted and output_present() uploads an all-zero framebuffer. */

        /* Repaint the character base when it changed. */
        if (tgs_term_take_dirty(g_term)) {
            tgs_term_view_draw(g_term_view, g_term);
            term_dump();
        }
        be->render();

        /* Present framebuffer */
        output_present(&disp);
    }

    /* Cleanup */
    input_cleanup();
    close(master_fd);
    if (ime_master_fd >= 0) {
        close(ime_master_fd);
        if (ime_pid > 0) {
            kill(ime_pid, SIGTERM);
            waitpid(ime_pid, &status, 0);
        }
    }
    waitpid(child_pid, &status, 0);
    tgs_term_free(g_term);
    be->deinit();
    output_cleanup(&disp);

    return 0;
}
