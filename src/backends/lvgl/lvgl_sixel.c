/*
 * LVGL Sixel Output — flush callback + minimal inline Sixel encoder.
 * Converts ARGB8888 framebuffer to Sixel and writes to stdout.
 * C99, no external dependencies.
 */
#include "lv_conf.h"
#include <lvgl/lvgl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Minimal Sixel Encoder ---- */

#define SIXEL_MAX_COLORS 256
#define SIXEL_QUANT_BITS 3 /* 3 bits per channel → 8 levels → 512 max unique */

typedef struct {
    uint8_t rgb[SIXEL_MAX_COLORS][3]; /* RGB in 0-255 */
    int     ncolors;
    uint8_t index_of[512];            /* R3G3B3 → palette index */
    uint8_t used[512];                /* 1 if bucket occupied */
} sixel_palette_t;

static inline int quant3(unsigned char v)
{
    return v >> (8 - SIXEL_QUANT_BITS); /* 0-7 */
}

static inline int color_key(unsigned char r, unsigned char g, unsigned char b)
{
    return (quant3(r) << 6) | (quant3(g) << 3) | quant3(b);
}

/* Build palette from ARGB8888 pixel buffer. Returns ncolors (≤ 256). */
static int build_palette(sixel_palette_t *p, const uint8_t *px, int npx)
{
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < npx; i++) {
        unsigned char b = px[i * 4 + 0];
        unsigned char g = px[i * 4 + 1];
        unsigned char r = px[i * 4 + 2];
        /* skip fully transparent */
        unsigned char a = px[i * 4 + 3];
        if (a == 0) continue;
        int key = color_key(r, g, b);
        if (!p->used[key]) {
            if (p->ncolors >= SIXEL_MAX_COLORS) continue;
            p->used[key]     = 1;
            p->index_of[key] = (uint8_t)p->ncolors;
            p->rgb[p->ncolors][0] = (uint8_t)(quant3(r) * 255 / 7);
            p->rgb[p->ncolors][1] = (uint8_t)(quant3(g) * 255 / 7);
            p->rgb[p->ncolors][2] = (uint8_t)(quant3(b) * 255 / 7);
            p->ncolors++;
        }
    }
    /* ensure at least 1 color (black fallback) */
    if (p->ncolors == 0) {
        p->rgb[0][0] = p->rgb[0][1] = p->rgb[0][2] = 0;
        p->ncolors = 1;
    }
    return p->ncolors;
}

static inline int palette_index(const sixel_palette_t *p,
                                unsigned char r, unsigned char g, unsigned char b)
{
    int key = color_key(r, g, b);
    if (p->used[key]) return p->index_of[key];
    return 0; /* fallback to color 0 */
}

/* Encode full ARGB8888 buffer as Sixel to the given file. */
static void sixel_encode_buffer(FILE *fp, const uint8_t *px,
                                int width, int height)
{
    sixel_palette_t pal;
    build_palette(&pal, px, width * height);

    /* DCS introducer */
    fprintf(fp, "\033P0;0;q\"1;1;%d;%d", width, height);

    /* Define color registers */
    for (int i = 0; i < pal.ncolors; i++) {
        /* sixel uses 0-100 range for RGB */
        int r64 = pal.rgb[i][0] * 100 / 255;
        int g64 = pal.rgb[i][1] * 100 / 255;
        int b64 = pal.rgb[i][2] * 100 / 255;
        fprintf(fp, "#%d;2;%d;%d;%d", i, r64, g64, b64);
    }

    /* Encode 6-row bands */
    for (int band = 0; band < height; band += 6) {
        for (int c = 0; c < pal.ncolors; c++) {
            fprintf(fp, "#%d", c);
            int run_ch = -1;
            int run_len = 0;
            for (int x = 0; x < width; x++) {
                int sixel_val = 0;
                for (int row = 0; row < 6; row++) {
                    int y = band + row;
                    if (y >= height) break;
                    int off = (y * width + x) * 4;
                    unsigned char pb = px[off + 0];
                    unsigned char pg = px[off + 1];
                    unsigned char pr = px[off + 2];
                    unsigned char pa = px[off + 3];
                    if (pa > 0 && palette_index(&pal, pr, pg, pb) == c) {
                        sixel_val |= (1 << row);
                    }
                }
                int ch = sixel_val + 63;
                if (ch == run_ch) {
                    run_len++;
                } else {
                    if (run_len > 2)
                        fprintf(fp, "!%d%c", run_len, run_ch);
                    else
                        for (int i = 0; i < run_len; i++)
                            fputc(run_ch, fp);
                    run_ch = ch;
                    run_len = 1;
                }
            }
            /* flush trailing run */
            if (run_len > 2)
                fprintf(fp, "!%d%c", run_len, run_ch);
            else
                for (int i = 0; i < run_len; i++)
                    fputc(run_ch, fp);
            fputc('$', fp); /* next color plane */
        }
        fputc('-', fp); /* end of band */
    }

    /* ST — string terminator */
    fprintf(fp, "\033\\");
}

/* ---- LVGL Flush Callback ---- */

void sixel_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area; /* DIRECT mode: px covers full screen */
    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);

    /* Move cursor to top-left */
    fputs("\033[H", stdout);

    sixel_encode_buffer(stdout, px, w, h);

    lv_display_flush_ready(disp);
    fflush(stdout);
}
