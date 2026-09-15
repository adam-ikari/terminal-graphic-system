/*
 * Terminal view — renders a tgs_term cell grid onto an LVGL canvas.
 * The canvas is the bottom-most object: the character base, with widget
 * surfaces drawn above it.
 */
#ifndef TGS_LVGL_TERM_H
#define TGS_LVGL_TERM_H

#include "lvgl.h"
#include "term.h"

int tgs_term_view_cell_w(void);
int tgs_term_view_cell_h(void);

/* Create the canvas (cols * cell_w by rows * cell_h) as a child of the active
 * screen. The caller keeps the returned object handle. */
lv_obj_t *tgs_term_view_create(int cols, int rows);

/* Repaint the whole grid. Call when tgs_term_take_dirty() reports a change. */
void tgs_term_view_draw(lv_obj_t *view, const tgs_term *t);

#endif /* TGS_LVGL_TERM_H */
