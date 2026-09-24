/*
 * pty_capture — run a command inside a pty, capture its output stream.
 *
 * This is the live verification harness for the kitty presentation channel:
 * the compositor's stdout goes to a pty slave whose winsize *advertises cell
 * geometry* (ws_xpixel/ws_ypixel), so output_kitty takes the real
 * TIOCGWINSZ path — the same one it takes under a real kitty terminal —
 * instead of the env override or the no-geometry fallback a file redirect
 * would give. The master side is written to a file for scripts/
 * kitty_composite.py to replay, and per-second byte counts are printed so
 * idle cost and dirty-region savings are visible without a decoder.
 *
 * usage: pty_capture <out.bin> <cell_w> <cell_h> <seconds> -- <cmd...>
 *        cell_w/h <= 0 → advertise no pixel geometry (fallback path)
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

int main(int argc, char **argv)
{
    struct winsize ws;
    struct termios tio;
    long t0, deadline, total = 0;
    long *buckets;
    int secs, cols = 80, rows = 30;
    int cw, ch;
    const char *outpath;
    char **cmd;
    FILE *out;
    int master, i, captured_secs;
    pid_t pid;

    if (argc < 7 || strcmp(argv[5], "--") != 0) {
        fprintf(stderr,
                "usage: %s <out.bin> <cell_w> <cell_h> <seconds> -- <cmd...>\n",
                argv[0]);
        return 2;
    }
    outpath = argv[1];
    cw = atoi(argv[2]);
    ch = atoi(argv[3]);
    secs = atoi(argv[4]);
    cmd = &argv[6];
    if (secs < 1 || secs > 3600) {
        fprintf(stderr, "seconds must be 1..3600\n");
        return 2;
    }

    /* Cell geometry the child will observe through TIOCGWINSZ. The canvas
     * is 800x600; an 80x30 grid at 10x20 px cells reports exactly that. */
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    if (cw > 0 && ch > 0) {
        ws.ws_xpixel = (unsigned short)(cols * cw);
        ws.ws_ypixel = (unsigned short)(rows * ch);
    }

    /* Raw slave: the captured bytes must reach the file exactly as written.
     * The compositor sets its children's pty modes itself, not its own. */
    memset(&tio, 0, sizeof(tio));
    cfmakeraw(&tio);

    pid = forkpty(&master, NULL, &tio, &ws);
    if (pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (pid == 0) {
        execvp(cmd[0], cmd);
        perror("execvp");
        _exit(127);
    }

    out = fopen(outpath, "wb");
    if (!out) {
        perror(outpath);
        kill(pid, SIGTERM);
        close(master);
        waitpid(pid, NULL, 0);
        return 1;
    }

    buckets = (long *)calloc((size_t)secs + 1, sizeof(long));
    if (!buckets) {
        fclose(out);
        return 1;
    }

    t0 = now_ms();
    deadline = t0 + secs * 1000L;
    for (;;) {
        long now = now_ms();
        struct pollfd p;
        int timeout, r;

        if (now >= deadline) break;
        timeout = (int)(deadline - now);
        p.fd = master;
        p.events = POLLIN;
        p.revents = 0;
        r = poll(&p, 1, timeout);
        if (r < 0) break;
        if (r > 0 && (p.revents & POLLIN)) {
            char buf[65536];
            ssize_t n = read(master, buf, sizeof(buf));
            if (n <= 0) break;
            fwrite(buf, 1, (size_t)n, out);
            total += n;
            i = (int)((now_ms() - t0) / 1000);
            if (i > secs) i = secs;
            buckets[i] += n;
        }
        if (r > 0 && (p.revents & (POLLHUP | POLLERR))) break;
    }
    fclose(out);

    /* Teardown: the child may be blocked mid-write — after the deadline
     * nothing drains the pty, so a full output buffer stalls it inside
     * write(), and a restarted SIGTERM never reaches its loop. Drain while
     * waiting, escalate to SIGKILL, and only then release the master. */
    kill(pid, SIGTERM);
    {
        long give_up = now_ms() + 2000;
        int status;
        for (;;) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid || r < 0) break;
            if (now_ms() >= give_up) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                break;
            }
            struct pollfd p = { master, POLLIN, 0 };
            if (poll(&p, 1, 20) > 0 && (p.revents & POLLIN)) {
                char sink[65536];
                if (read(master, sink, sizeof(sink)) <= 0) break;
            }
        }
    }
    close(master);

    /* Per-second ledger: the first second holds the initial frame, the rest
     * is steady state — that difference is what dirty presentation moves. */
    fprintf(stderr, "captured %ld bytes over %d s\n", total, secs);
    captured_secs = 0;
    for (i = 0; i <= secs; i++) {
        fprintf(stderr, "  t=%ds %ld bytes\n", i, buckets[i]);
        if (i > 0) captured_secs++;
    }
    if (captured_secs > 0) {
        long steady = 0;
        for (i = 1; i <= secs; i++) steady += buckets[i];
        fprintf(stderr, "  steady-state (t>=1s): %ld B/s\n",
                steady / captured_secs);
    }
    free(buckets);
    return 0;
}
