/*
 * TGS Terminal Output Implementation
 * Handles raw mode, alt screen buffer, SGR mouse mode.
 */
#define _DEFAULT_SOURCE
#include "output.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

static struct termios saved_termios;

int output_get_size(int *cols, int *rows, int *pix_w, int *pix_h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) return -1;
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    *pix_w = ws.ws_xpixel;
    *pix_h = ws.ws_ypixel;
    return 0;
}

int output_init(void)
{
    struct termios raw;

    /* Save current terminal state */
    if (tcgetattr(STDIN_FILENO, &saved_termios) < 0) return -1;

    /* Switch to raw mode */
    raw = saved_termios;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return -1;

    /* Enable alternate screen, hide cursor, enable SGR mouse */
    write(STDOUT_FILENO, "\x1b[?1049h", 8); /* alt screen */
    write(STDOUT_FILENO, "\x1b[?25l",   6); /* hide cursor */
    write(STDOUT_FILENO, "\x1b[?1006h", 8); /* SGR mouse mode */
    write(STDOUT_FILENO, "\x1b[?1000h", 8); /* mouse tracking */

    return 0;
}

void output_cleanup(void)
{
    /* Disable mouse, show cursor, leave alt screen */
    write(STDOUT_FILENO, "\x1b[?1000l", 8); /* mouse off */
    write(STDOUT_FILENO, "\x1b[?1006l", 8); /* SGR mouse off */
    write(STDOUT_FILENO, "\x1b[?25h",   6); /* show cursor */
    write(STDOUT_FILENO, "\x1b[?1049l", 8); /* leave alt screen */

    /* Restore terminal */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}
