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

#include "tgs_protocol.h"
#include "tgs_frame.h"
#include "tgs_backend.h"
#include "parser.h"
#include "window_manager.h"
#include "output.h"
#include "input.h"

/* LVGL backend registration — defined in lvgl_backend.c */
extern void lvgl_backend_register(void);
extern void lvgl_backend_set_display(tgs_display *display);

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

    /* Fork PTY */
    child_pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (child_pid < 0) {
        perror("forkpty");
        be->deinit();
        output_cleanup(&disp);
        return 1;
    }

    if (child_pid == 0) {
        pty_set_raw();
        /* Child: exec the app */
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

    /* Handshake: send HELLO, wait for HELLO from app, send READY */
    {
        const char *hello_args[2] = {TGS_PROTOCOL_VERSION, TGS_CAPS_LAYER0};
        tgs_frame_write(master_fd, TGS_STREAM_HANDSHAKE, 1,
                        TGS_CMD_HELLO, hello_args, 2);

        while (!wm.hello_received && running) {
            ssize_t n = read(master_fd, rbuf, sizeof(rbuf));
            if (n <= 0) break;
            tgs_parser_feed(&parser, rbuf, (int)n);
        }

        if (wm.hello_received) {
            const char *ready_args[1] = {TGS_CAPS_LAYER0};
            tgs_frame_write(master_fd, TGS_STREAM_HANDSHAKE, 2,
                            TGS_CMD_READY, ready_args, 1);
        }
    }

    /* Initialize input */
    input_init(be);

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
    be->deinit();
    output_cleanup(&disp);

    return 0;
}
