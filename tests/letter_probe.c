/*
 * letter_probe — draw one rectangle and one glyph into a canvas, dump the
 * pixels, and report how many each produced.
 *
 * This isolates the single question the terminal view cannot answer for
 * itself: does lv_draw_letter rasterise into a canvas layer at all? If the
 * rectangle lands and the glyph does not, the fault is in the letter draw
 * path, not in the cell grid or the terminal state feeding it.
 */
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

extern const lv_font_t lv_font_unscii_8;

#define W 32
#define H 24

static uint8_t buf[W * H * 4];

static int lit(int x, int y)
{
    const uint8_t *p = &buf[((size_t)y * W + (size_t)x) * 4u];
    return (p[0] | p[1] | p[2]) != 0;
}

int main(void)
{
    lv_obj_t *canvas;
    lv_layer_t layer;
    lv_draw_letter_dsc_t ld;
    lv_draw_rect_dsc_t rd;
    lv_point_t p;
    lv_area_t a;
    int x, y, rect_px = 0, glyph_px = 0;

    lv_init();
    memset(buf, 0, sizeof(buf));

    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, buf, W, H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);

    lv_canvas_init_layer(canvas, &layer);

    /* Reference: a plain rect, to prove the layer writes at all. */
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(0xFF0000);
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = 0;
    rd.border_width = 0;
    a.x1 = 0; a.y1 = 0; a.x2 = 7; a.y2 = 7;
    lv_draw_rect(&layer, &rd, &a);

    /* The question: a glyph, same layer, same canvas. */
    lv_draw_letter_dsc_init(&ld);
    ld.unicode = 'M';
    ld.color = lv_color_hex(0x00FF00);
    ld.font = &lv_font_unscii_8;
    p.x = 8;
    p.y = 0;
    lv_draw_letter(&layer, &ld, &p);

    lv_canvas_finish_layer(canvas, &layer);

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            if (!lit(x, y)) continue;
            if (x < 8 && y < 8) rect_px++;
            else if (x >= 8) glyph_px++;
        }
    }

    printf("rect pixels=%d  glyph pixels=%d\n", rect_px, glyph_px);
    printf("glyph box: adv=%d\n",
           (int)lv_font_get_glyph_width(&lv_font_unscii_8, 'M', 'M'));
    return glyph_px > 0 ? 0 : 1;
}
