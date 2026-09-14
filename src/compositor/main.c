/*
 * TGS Compositor Main
 * PTY fork, poll loop, handshake, backend wiring.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <pty.h>
#include <sys/wait.h>

#include "tgs_protocol.h"
#include "tgs_frame.h"
#include "tgs_backend.h"
#include "parser.h"
#include "window_manager.h"
#include "event_engine.h"
#include "output.h"


/* LVGL backend registration — defined in lvgl_backend.c */
extern void lvgl_backend_register(void);

static volatile sig_atomic_t running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[])
{
    int cols, rows, pix_w, pix_h;
    int cell_w, cell_h, disp_w, disp_h;
    int master_fd;
    int ime_master_fd = -1;
    int status;
    tgs_backend *be;
    window_manager wm;
    event_engine ee;
    tgs_parser parser;
    pid_t child_pid;
    pid_t ime_pid = -1;
    struct pollfd fds[3];
    uint8_t rbuf[4096];

    (void)argc;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <app> [args...]\n", argv[0]);
        return 1;
    }

    /* Signal handling */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Get terminal size */
    if (output_get_size(&cols, &rows, &pix_w, &pix_h) < 0) {
        fprintf(stderr, "Failed to get terminal size\n");
        return 1;
    }

    /* Calculate pixel dimensions */
    cell_w = (pix_w > 0 && cols > 0) ? pix_w / cols : 10;
    cell_h = (pix_h > 0 && rows > 0) ? pix_h / rows : 20;
    disp_w = cols * cell_w;
    disp_h = rows * cell_h;

    /* Enter raw mode + alt screen + mouse */
    if (output_init() < 0) {
        fprintf(stderr, "Failed to initialize output\n");
        return 1;
    }

    /* Register LVGL backend */
    lvgl_backend_register();
    be = tgs_backend_get();
    if (!be) {
        fprintf(stderr, "No backend registered\n");
        output_cleanup();
        return 1;
    }

    /* Initialize backend */
    if (be->init(disp_w, disp_h) < 0) {
        fprintf(stderr, "Backend init failed\n");
        output_cleanup();
        return 1;
    }

    /* Fork PTY */
    child_pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (child_pid < 0) {
        perror("forkpty");
        be->deinit();
        output_cleanup();
        return 1;
    }

    if (child_pid == 0) {
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
            /* IME child: exec the IME app */
            execlp(ime_path, ime_path, (char *)NULL);
            _exit(1); /* IME app not found — continue without IME */
        }
        if (ime_pid < 0) {
            ime_master_fd = -1; /* fork failed — continue without IME */
        }
    }

    wm_init(&wm, be, master_fd, ime_master_fd);
    wm.disp_w = disp_w;
    wm.disp_h = disp_h;

    be->set_event_callback(wm_backend_event, &wm);

    tgs_parser_init(&parser, wm_handle_frame, &wm);

    /* Handshake: send HELLO, wait for HELLO from app, send READY */
    {
        const char *hello_args[2] = {TGS_PROTOCOL_VERSION, TGS_CAPS_LAYER0};
        tgs_frame_write(master_fd, TGS_STREAM_HANDSHAKE, 1,
                        TGS_CMD_HELLO, hello_args, 2);

        /* Blocking read until hello_received */
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

    /* Init event engine */
    ee_init(&ee, be, cell_w, cell_h);

    /* Main poll loop */
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = master_fd;
    fds[1].events = POLLIN;
    fds[2].fd = ime_master_fd;
    fds[2].events = POLLIN;

    while (running) {
        int nfds = (ime_master_fd >= 0) ? 3 : 2;
        int ret = poll(fds, nfds, 10);

        if (ret < 0) break;

        /* Terminal input */
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, rbuf, sizeof(rbuf));
            if (n > 0) ee_feed(&ee, rbuf, (int)n);
        }

        /* App output (protocol frames) */
        if (fds[1].revents & POLLIN) {
            ssize_t n = read(master_fd, rbuf, sizeof(rbuf));
            if (n > 0) {
                tgs_parser_feed(&parser, rbuf, (int)n);
            } else if (n <= 0) {
                break; /* app exited */
            }
        }

        /* IME app output (IME_COMMIT, IME_PREEDIT frames) */
        if (ime_master_fd >= 0 && (fds[2].revents & POLLIN)) {
            ssize_t n = read(ime_master_fd, rbuf, sizeof(rbuf));
            if (n > 0) {
                tgs_parser_feed(&parser, rbuf, (int)n);
            }
        }

        /* IME app exited */
        if (ime_master_fd >= 0 && (fds[2].revents & (POLLHUP | POLLERR))) {
            ime_master_fd = -1;
            wm.ime_pty_fd = -1;
            wm.ime_connected = 0;
            fds[2].fd = -1;
        }

        /* Child exited */
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            break;
        }

        /* LVGL tick */
        be->tick(10);
    }

    /* Cleanup */
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
    output_cleanup();

    return 0;
}
