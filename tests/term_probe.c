/*
 * term_probe — replay a captured byte stream through the terminal emulator and
 * dump the resulting grid.
 *
 * When a program renders wrongly the question is always one of three: is it the
 * input, the emulator, or the renderer? This probe answers the middle one. Give
 * it the raw bytes a program wrote and it prints the grid those bytes produce,
 * so the emulator can be judged against the bytes alone — no display server, no
 * timing, no renderer.
 *
 * Usage: term_probe <raw-file> [cols rows]
 */
#include "term.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    tgs_term *t;
    const tgs_term_cell *cells;
    FILE *f;
    uint8_t buf[4096];
    size_t n;
    int cols = (argc > 2) ? atoi(argv[2]) : 100;
    int rows = (argc > 3) ? atoi(argv[3]) : 37;
    int x, y;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <raw-file> [cols rows]\n", argv[0]);
        return 2;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }

    t = tgs_term_new(cols, rows);
    if (!t) {
        fclose(f);
        return 1;
    }

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        tgs_term_feed(t, buf, (int)n);
    fclose(f);

    printf("# grid %dx%d cursor %d,%d visible %d\n", cols, rows,
           tgs_term_cx(t), tgs_term_cy(t), tgs_term_cursor_visible(t));

    cells = tgs_term_cells(t);
    for (y = 0; y < rows; y++) {
        for (x = 0; x < cols; x++) {
            uint32_t cp = cells[y * cols + x].cp;
            putchar((cp >= 32 && cp < 127) ? (int)cp : (cp ? '.' : ' '));
        }
        putchar('\n');
    }

    tgs_term_free(t);
    return 0;
}
