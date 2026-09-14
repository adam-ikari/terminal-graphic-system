/*
 * render_demo.c — Headless renderer for the TGS widget tree.
 *
 * Drives the *real* LVGL backend (same code path the compositor uses) with a
 * memory framebuffer instead of the SDL window, then dumps the ARGB8888 draw
 * buffer to a PNG. No display server required.
 *
 * Build: cmake --build build --target render_demo
 * Run:   ./build/render_demo [out.png] [simple_form|container|library]
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

/* --- Demo trees, mirroring the example apps --- */

/* Mirrors examples/simple_form.c */
static void build_simple_form(tgs_backend *be, void *win)
{
    void *w;

    w = be->create_widget(win, TGS_WIDGET_LABEL);
    be->set_widget_rect(w, 20, 20, 200, 30);
    be->set_widget_content(w, "Enter your name:");

    w = be->create_widget(win, TGS_WIDGET_INPUT);
    be->set_widget_rect(w, 20, 60, 300, 40);
    be->set_widget_content(w, "");

    w = be->create_widget(win, TGS_WIDGET_BUTTON);
    be->set_widget_rect(w, 20, 110, 120, 40);
    be->set_widget_content(w, "Submit");

    w = be->create_widget(win, TGS_WIDGET_LABEL);
    be->set_widget_rect(w, 20, 160, 400, 30);
    be->set_widget_content(w, "");
}

/* Mirrors examples/container_demo.c: VLAYOUT > (LABEL, HLAYOUT > (INPUT, BUTTON), LABEL) */
static void build_container_demo(tgs_backend *be, void *win)
{
    void *vlayout = be->create_widget(win, TGS_WIDGET_VLAYOUT);
    be->set_widget_rect(vlayout, 20, 20, 600, 200);
    be->set_widget_content(vlayout, "");

    void *label = be->create_widget(vlayout, TGS_WIDGET_LABEL);
    be->set_widget_rect(label, 0, 0, 200, 30);
    be->set_widget_content(label, "Name:");

    void *hlayout = be->create_widget(vlayout, TGS_WIDGET_HLAYOUT);
    be->set_widget_rect(hlayout, 0, 0, 560, 40);
    be->set_widget_content(hlayout, "");

    void *input = be->create_widget(hlayout, TGS_WIDGET_INPUT);
    be->set_widget_rect(input, 0, 0, 300, 40);
    be->set_widget_content(input, "");

    void *btn = be->create_widget(hlayout, TGS_WIDGET_BUTTON);
    be->set_widget_rect(btn, 0, 0, 80, 40);
    be->set_widget_content(btn, "OK");

    void *result = be->create_widget(vlayout, TGS_WIDGET_LABEL);
    be->set_widget_rect(result, 0, 0, 400, 30);
    be->set_widget_content(result, "");
}

/* One widget of every tgs_widget_type — smoke test for the backend mapping. */
static void build_library(tgs_backend *be, void *win)
{
    static const struct { tgs_widget_type type; const char *text; } items[] = {
        { TGS_WIDGET_LABEL,    "label" },
        { TGS_WIDGET_BUTTON,   "button" },
        { TGS_WIDGET_INPUT,    "input" },
        { TGS_WIDGET_CHECKBOX, "checkbox" },
        { TGS_WIDGET_RADIO,    "radio" },
        { TGS_WIDGET_SLIDER,   "" },
        { TGS_WIDGET_PROGRESS, "60" },
        { TGS_WIDGET_SWITCH,   "" },
        { TGS_WIDGET_LIST,     "list item" },
        { TGS_WIDGET_TABLE,    "cell" },
        { TGS_WIDGET_MENU,     "" },
        { TGS_WIDGET_TAB,      "tab" },
        { TGS_WIDGET_DROPDOWN, "one\ntwo\nthree" },
        { TGS_WIDGET_IMAGE,    "" },
        { TGS_WIDGET_TIMEPICK, "00\n01\n02" },
        { TGS_WIDGET_DATEPICK, "2026-09-14" },
        { TGS_WIDGET_VLAYOUT,  "" },
        { TGS_WIDGET_HLAYOUT,  "" },
        { TGS_WIDGET_GLAYOUT,  "" },
        { TGS_WIDGET_SCROLL,   "" },
    };
    size_t n = sizeof(items) / sizeof(items[0]);
    int x = 10, y = 10;
    size_t k;

    for (k = 0; k < n; k++) {
        void *w = be->create_widget(win, items[k].type);
        be->set_widget_rect(w, x, y, 180, 70);
        be->set_widget_content(w, items[k].text);
        x += 195;
        if (x > 700) { x = 10; y += 95; }
    }
}

int main(int argc, char *argv[])
{
    const char *out_path = (argc > 1) ? argv[1] : "demo.png";
    const char *demo = (argc > 2) ? argv[2] : "simple_form";
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

    /* --- Build the widget tree of the selected demo --- */
    void *win = be->create_window(TGS_WINDOW_NORMAL, "Simple Form");

    if (strcmp(demo, "container") == 0) {
        build_container_demo(be, win);
    } else if (strcmp(demo, "library") == 0) {
        build_library(be, win);
    } else if (strcmp(demo, "simple_form") == 0) {
        build_simple_form(be, win);
    } else {
        fprintf(stderr,
                "render_demo: unknown demo '%s' (expected simple_form|container|library)\n",
                demo);
        be->deinit();
        return 1;
    }

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
