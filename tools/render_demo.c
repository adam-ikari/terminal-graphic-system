/*
 * render_demo.c — Headless renderer for the TGS widget tree.
 *
 * Drives the *real* LVGL backend (same code path the compositor uses) with a
 * memory framebuffer instead of the SDL window, then dumps the ARGB8888 draw
 * buffer to a PNG. No display server required.
 *
 * Build: cmake --build build --target render_demo
 * Run:   ./build/render_demo [out.png]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tgs_backend.h"
#include "output.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../deps/libsixel/src/stb_image_write.h"
/* LVGL backend registration — defined in lvgl_backend.c */
extern void lvgl_backend_register(void);
extern void lvgl_backend_set_display(tgs_display *display);

#define WIDTH  800
#define HEIGHT 600

/* Widget IDs mirror examples/simple_form.c */
static void *g_widgets[8];

int main(int argc, char *argv[])
{
    const char *out_path = (argc > 1) ? argv[1] : "demo.png";
    tgs_backend *be;
    tgs_display disp;
    int i;

    memset(&disp, 0, sizeof(disp));

    /* --- Same wiring as src/compositor/main.c, minus the SDL window --- */
    lvgl_backend_register();
    be = tgs_backend_get();
    if (!be) {
        fprintf(stderr, "render_demo: no backend registered\n");
        return 1;
    }

    lvgl_backend_set_display(&disp);
    if (be->init(WIDTH, HEIGHT) < 0) {
        fprintf(stderr, "render_demo: backend init failed\n");
        return 1;
    }

    /* --- Build the simple_form widget tree --- */
    void *win = be->create_window(TGS_WINDOW_NORMAL, "Simple Form");

    g_widgets[0] = be->create_widget(win, TGS_WIDGET_LABEL);
    be->set_widget_rect(g_widgets[0], 20, 20, 200, 30);
    be->set_widget_content(g_widgets[0], "Enter your name:");

    g_widgets[1] = be->create_widget(win, TGS_WIDGET_INPUT);
    be->set_widget_rect(g_widgets[1], 20, 60, 300, 40);
    be->set_widget_content(g_widgets[1], "");

    g_widgets[2] = be->create_widget(win, TGS_WIDGET_BUTTON);
    be->set_widget_rect(g_widgets[2], 20, 110, 120, 40);
    be->set_widget_content(g_widgets[2], "Submit");

    g_widgets[3] = be->create_widget(win, TGS_WIDGET_LABEL);
    be->set_widget_rect(g_widgets[3], 20, 160, 400, 30);
    be->set_widget_content(g_widgets[3], "");

    /* --- Let LVGL run its refresh timers into the shared draw buffer --- */
    for (i = 0; i < 10; i++) {
        be->tick(16);
        be->render();
    }

    if (!disp.buffer) {
        fprintf(stderr, "render_demo: backend did not expose a draw buffer\n");
        be->deinit();
        return 1;
    }

    /* --- Dump: ARGB8888 is BGRA in memory on little-endian; PNG wants RGBA --- */
    {
        const size_t npx = (size_t)WIDTH * (size_t)HEIGHT;
        uint8_t *rgba = (uint8_t *)malloc(npx * 4);

        if (!rgba) {
            fprintf(stderr, "render_demo: out of memory\n");
            be->deinit();
            return 1;
        }

        for (size_t p = 0; p < npx; p++) {
            const uint8_t *src = disp.buffer + p * 4;
            uint8_t *dst = rgba + p * 4;
            dst[0] = src[2]; /* R */
            dst[1] = src[1]; /* G */
            dst[2] = src[0]; /* B */
            dst[3] = 0xFF;   /* opaque */
        }

        if (!stbi_write_png(out_path, WIDTH, HEIGHT, 4, rgba, WIDTH * 4)) {
            fprintf(stderr, "render_demo: failed to write %s\n", out_path);
            free(rgba);
            be->deinit();
            return 1;
        }
        free(rgba);
    }

    fprintf(stderr, "render_demo: wrote %s (%dx%d)\n", out_path, WIDTH, HEIGHT);

    be->deinit();
    return 0;
}
