/*
 * render_snapshot.c — dump the last rendered canvas as a PNG directly from
 * the scene backend (the ground truth for kitty round-trip comparison).
 * Same wiring as render_demo minus the demo builders: renders a static
 * scene, dumps once, exits.
 */
#include "tgs_backend.h"
#include "output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../deps/stb_image_write.h"

extern void scene_backend_register(void);
extern void scene_backend_set_display(tgs_display *display);

#define W 800
#define H 600

int main(int argc, char *argv[])
{
    tgs_backend *be;
    tgs_display disp;
    void *root, *title, *row, *left, *right;
    uint8_t *rgba;
    size_t i;
    int n;

    if (argc < 2) {
        fprintf(stderr, "usage: render_snapshot <out.png>\n");
        return 2;
    }

    memset(&disp, 0, sizeof(disp));
    scene_backend_register();
    be = tgs_backend_get();
    scene_backend_set_display(&disp);
    if (be->init(W, H) < 0) return 2;

    /* Mirror examples/container_demo.c: box + title + row(left,right). */
    root = be->create_element(NULL, TGS_WIDGET_BOX);
    be->set_element_rect(root, 20, 20, 600, 200);
    title = be->create_element(root, TGS_WIDGET_TEXT);
    be->set_element_rect(title, 10, 10, 200, 30);
    be->set_element_content(title, "program-computed layout");
    row = be->create_element(root, TGS_WIDGET_BOX);
    be->set_element_rect(row, 10, 50, 560, 40);
    left = be->create_element(row, TGS_WIDGET_TEXT);
    be->set_element_rect(left, 0, 0, 300, 40);
    be->set_element_content(left, "left");
    right = be->create_element(row, TGS_WIDGET_TEXT);
    be->set_element_rect(right, 320, 0, 80, 40);
    be->set_element_content(right, "right");

    for (n = 0; n < 10; n++) { be->tick(16); be->render(); }

    /* ARGB (B,G,R,A in memory) → RGBA for stb. */
    rgba = (uint8_t *)malloc((size_t)W * H * 4u);
    if (!rgba) return 2;
    for (i = 0; i < (size_t)W * H; i++) {
        rgba[i * 4 + 0] = disp.buffer[i * 4 + 2];
        rgba[i * 4 + 1] = disp.buffer[i * 4 + 1];
        rgba[i * 4 + 2] = disp.buffer[i * 4 + 0];
        rgba[i * 4 + 3] = 0xFF;
    }
    if (!stbi_write_png(argv[1], W, H, 4, rgba, W * 4)) return 2;

    be->deinit();
    free(rgba);
    return 0;
}
