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

/* Canonical TGS key space and modifier mask (docs/navigation.md §D.4) — the
 * same values input_sdl.c emits and lvgl_backend.c translates to LVGL.
 * Printable ASCII maps to itself, 1000+ is reserved for keys without one. */
#define TGS_KEY_LEFT   1000
#define TGS_KEY_RIGHT  1001
#define TGS_KEY_UP     1002
#define TGS_KEY_DOWN   1003
#define TGS_KEY_HOME   1004
#define TGS_KEY_END    1005

#define TGS_MOD_SHIFT  0x01
#define TGS_MOD_CTRL   0x02
#define TGS_MOD_ALT    0x04

static tgs_backend *g_backend;
static int kbd_fd  = -1;
static int mouse_fd = -1;
static int mods_state;  /* modifier mask, one bit per class (left/right merged) */
static int btn_state;   /* button held — motion must not fake a release */

/* Compositor-owned sink (input.h) — see input_sdl.c. */
static tgs_input_key_sink g_key_sink;
static void *g_key_sink_ud;

void input_set_key_sink(tgs_input_key_sink sink, void *ud)
{
    g_key_sink = sink;
    g_key_sink_ud = ud;
}

/* Pointer position: relative deltas accumulate, absolute (touch) on SYN.
 * Button events carry it, so a click lands where the pointer actually is. */
static int ptr_x, ptr_y;

/* Pending absolute position (touchscreen) — injected on SYN_REPORT */
static int abs_x, abs_y;

static void set_mod(int *mask, int bit, int value)
{
    if (value) *mask |= bit;
    else       *mask &= ~bit;
}

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
                    set_mod(&mods_state, TGS_MOD_SHIFT, ev.value);
                    continue;
                }
                if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
                    set_mod(&mods_state, TGS_MOD_CTRL, ev.value);
                    continue;
                }
                if (ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT) {
                    set_mod(&mods_state, TGS_MOD_ALT, ev.value);
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
                case KEY_UP:        tgs_key = TGS_KEY_UP;    break;
                case KEY_DOWN:      tgs_key = TGS_KEY_DOWN;  break;
                case KEY_RIGHT:     tgs_key = TGS_KEY_RIGHT; break;
                case KEY_LEFT:      tgs_key = TGS_KEY_LEFT;  break;
                case KEY_DELETE:    tgs_key = 127; break;
                case KEY_HOME:      tgs_key = TGS_KEY_HOME;  break;
                case KEY_END:       tgs_key = TGS_KEY_END;   break;
                default:
                    tgs_key = keycode_to_ascii(ev.code,
                                               (mods_state & TGS_MOD_SHIFT) != 0);
                }

                if (tgs_key > 0) {
                    if (g_key_sink) g_key_sink(tgs_key, mods_state, pressed, g_key_sink_ud);
                    g_backend->inject_key(tgs_key, mods_state, pressed);
                }

            } else if (ev.type == EV_REL) {
                /* Relative mouse movement */
                if (!g_backend || !g_backend->inject_mouse) continue;
                if (ev.code == REL_X) ptr_x += ev.value;
                if (ev.code == REL_Y) ptr_y += ev.value;
                if (ptr_x < 0) ptr_x = 0;
                if (ptr_y < 0) ptr_y = 0;
                g_backend->inject_mouse(ptr_x, ptr_y, 0, btn_state);

            } else if (ev.type == EV_ABS) {
                /* Absolute positioning (touchscreen) — accumulate until SYN */
                if (ev.code == ABS_X) abs_x = ev.value;
                if (ev.code == ABS_Y) abs_y = ev.value;

            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                /* Flush accumulated absolute position */
                if (abs_x != 0 || abs_y != 0) {
                    ptr_x = abs_x;
                    ptr_y = abs_y;
                    if (g_backend && g_backend->inject_mouse)
                        g_backend->inject_mouse(ptr_x, ptr_y, 0, btn_state);
                }


            } else if (ev.type == EV_KEY && ev.code >= BTN_MOUSE &&
                       ev.code <= BTN_MOUSE + 2) {
                /* Mouse buttons — at the pointer position, and remembered so
                 * motion events keep reporting the button as held. */
                if (!g_backend || !g_backend->inject_mouse) continue;
                int btn = ev.code - BTN_MOUSE;
                btn_state = ev.value ? 1 : 0;
                g_backend->inject_mouse(ptr_x, ptr_y, btn, btn_state);
            }
        }
    }
}

void input_cleanup(void)
{
    if (kbd_fd >= 0)  { close(kbd_fd);  kbd_fd  = -1; }
    if (mouse_fd >= 0) { close(mouse_fd); mouse_fd = -1; }
}
