/*
 * TGS evdev Input Backend
 * Reads /dev/input/event* for keyboard and mouse/touchscreen.
 */
#include "input.h"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

static tgs_backend *g_backend;
static int kbd_fd  = -1;
static int mouse_fd = -1;
static int shift_state;

/* Pending absolute position (touchscreen) — injected on SYN_REPORT */
static int abs_x, abs_y;

/* Linux keycode → ASCII.  Index = linux keycode. */
static int keycode_to_ascii(int code, int shift)
{
    static const char normal[] =
        "  1234567890-=  "
        "qwertyuiop[]  "
        "asdfghjkl;'`  "
        "\\zxcvbnm,./  ";
    static const char shifted[] =
        "  !@#$%^&*()_+  "
        "QWERTYUIOP{}  "
        "ASDFGHJKL:\"~  "
        "|ZXCVBNM<>?  ";

    if (code >= 0 && code < (int)sizeof(normal))
        return shift ? shifted[code] : normal[code];
    return 0;
}

int input_init(tgs_backend *backend)
{
    g_backend = backend;

    kbd_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (kbd_fd < 0)
        fprintf(stderr, "input: no /dev/input/event0 (keyboard)\n");

    mouse_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (mouse_fd < 0)
        fprintf(stderr, "input: no /dev/input/event1 (mouse/touch)\n");

    return 0;
}

void input_poll(void)
{
    struct pollfd fds[2];
    int nfds = 0;

    if (kbd_fd >= 0)  { fds[nfds].fd = kbd_fd;  fds[nfds].events = POLLIN; nfds++; }
    if (mouse_fd >= 0) { fds[nfds].fd = mouse_fd; fds[nfds].events = POLLIN; nfds++; }
    if (nfds == 0) return;

    if (poll(fds, nfds, 0) <= 0) return;

    struct input_event ev;
    for (int i = 0; i < nfds; i++) {
        if (!(fds[i].revents & POLLIN)) continue;

        while (read(fds[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {

            if (ev.type == EV_KEY && ev.code < BTN_MOUSE) {
                /* Keyboard events */
                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                    shift_state = ev.value;
                    continue;
                }
                if (!g_backend || !g_backend->inject_key) continue;

                int tgs_key = 0;
                int pressed = ev.value ? 1 : 0;

                switch (ev.code) {
                case KEY_ENTER:     tgs_key = 13;  break;
                case KEY_BACKSPACE: tgs_key = 8;   break;
                case KEY_TAB:       tgs_key = 9;   break;
                case KEY_ESC:       tgs_key = 27;  break;
                case KEY_UP:        tgs_key = 1;   break;
                case KEY_DOWN:      tgs_key = 2;   break;
                case KEY_RIGHT:     tgs_key = 3;   break;
                case KEY_LEFT:      tgs_key = 4;   break;
                case KEY_DELETE:    tgs_key = 127; break;
                case KEY_HOME:      tgs_key = 1004; break;
                case KEY_END:       tgs_key = 1005; break;
                default:
                    tgs_key = keycode_to_ascii(ev.code, shift_state);
                    break;
                }

                if (tgs_key > 0)
                    g_backend->inject_key(tgs_key, shift_state, pressed);

            } else if (ev.type == EV_REL) {
                /* Relative mouse movement */
                if (!g_backend || !g_backend->inject_mouse) continue;
                static int mx, my;
                if (ev.code == REL_X) mx += ev.value;
                if (ev.code == REL_Y) my += ev.value;
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;
                g_backend->inject_mouse(mx, my, 0, 0);

            } else if (ev.type == EV_ABS) {
                /* Absolute positioning (touchscreen) — accumulate until SYN */
                if (ev.code == ABS_X) abs_x = ev.value;
                if (ev.code == ABS_Y) abs_y = ev.value;

            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                /* Flush accumulated absolute position */
                if (abs_x != 0 || abs_y != 0) {
                    if (g_backend && g_backend->inject_mouse)
                        g_backend->inject_mouse(abs_x, abs_y, 0, 0);
                }

            } else if (ev.type == EV_KEY && ev.code >= BTN_MOUSE &&
                       ev.code <= BTN_MOUSE + 2) {
                /* Mouse buttons */
                if (!g_backend || !g_backend->inject_mouse) continue;
                int btn = ev.code - BTN_MOUSE;
                g_backend->inject_mouse(abs_x, abs_y, btn, ev.value ? 1 : 0);
            }
        }
    }
}

void input_cleanup(void)
{
    if (kbd_fd >= 0)  { close(kbd_fd);  kbd_fd  = -1; }
    if (mouse_fd >= 0) { close(mouse_fd); mouse_fd = -1; }
}
