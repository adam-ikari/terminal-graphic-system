/*
 * font_probe — print the metrics the terminal renderer keys its cell size on.
 *
 * The cell must match the font's real advance, not a guess: a 16-px-wide glyph
 * drawn in an 8-px cell overlaps its neighbours, and that is exactly the kind
 * of "the font looks wrong" defect no screenshot should hide behind.
 */
#include "lvgl.h"

#include <stdio.h>

extern const lv_font_t lv_font_unscii_16;

int main(void)
{
    static const uint32_t cps[] = {' ', '0', 'M', 'm', '|', '[', ']', 'g', 'y', ':'};
    lv_font_glyph_dsc_t d;
    size_t i;

    lv_init();

    for (i = 0; i < sizeof(cps) / sizeof(cps[0]); i++) {
        if (lv_font_get_glyph_dsc(&lv_font_unscii_16, &d, cps[i], 0)) {
            printf("U+%04X '%c'  adv_w=%u  box=%ux%u  ofs=(%d,%d)\n",
                   (unsigned)cps[i], (int)cps[i],
                   (unsigned)d.adv_w, (unsigned)d.box_w, (unsigned)d.box_h,
                   (int)d.ofs_x, (int)d.ofs_y);
        } else {
            printf("U+%04X  NOT FOUND\n", (unsigned)cps[i]);
        }
    }
    printf("--- line_height=%d  glyph_width('M')=%d\n",
           (int)lv_font_get_line_height(&lv_font_unscii_16),
           (int)lv_font_get_glyph_width(&lv_font_unscii_16, 'M', 'M'));
    return 0;
}
