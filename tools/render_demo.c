/*
 * render_demo.c — Headless renderer for the TGS widget tree.
 *
 * Drives the *real* scene backend (same code path the compositor uses) with a
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
#include "../deps/stb_image_write.h"
/* Scene backend registration — defined in scene_backend.c */
extern void scene_backend_register(void);
extern void scene_backend_set_display(tgs_display *display);

#define WIDTH  800
#define HEIGHT 600

/* --- Demo trees, mirroring the example apps --- */

/* Mirrors examples/simple_form.c */
static void build_simple_form(tgs_backend *be, void *win)
{
    void *w;

    w = be->create_element(win, TGS_WIDGET_TEXT);
    be->set_element_rect(w, 20, 20, 200, 30);
    be->set_element_content(w, "Enter your name:");

    w = be->create_element(win, TGS_WIDGET_TEXT);
    be->set_element_rect(w, 20, 60, 300, 40);
    be->set_element_content(w, "");

    w = be->create_element(win, TGS_WIDGET_BOX);
    be->set_element_rect(w, 20, 110, 120, 40);
    be->set_element_content(w, "Submit");

    w = be->create_element(win, TGS_WIDGET_TEXT);
    be->set_element_rect(w, 20, 160, 400, 30);
    be->set_element_content(w, "");
}

/* Mirrors examples/container_demo.c: CONTAINER > (LABEL, CONTAINER > (INPUT, BUTTON)) */
static void build_container_demo(tgs_backend *be, void *win)
{
    void *vlayout = be->create_element(win, TGS_WIDGET_BOX);
    be->set_element_rect(vlayout, 20, 20, 600, 200);
    be->set_element_content(vlayout, "");

    void *label = be->create_element(vlayout, TGS_WIDGET_TEXT);
    be->set_element_rect(label, 10, 10, 200, 30);
    be->set_element_content(label, "Name:");

    void *hlayout = be->create_element(vlayout, TGS_WIDGET_BOX);
    be->set_element_rect(hlayout, 10, 50, 560, 40);
    be->set_element_content(hlayout, "");

    void *input = be->create_element(hlayout, TGS_WIDGET_TEXT);
    be->set_element_rect(input, 0, 0, 300, 40);
    be->set_element_content(input, "");

    void *btn = be->create_element(hlayout, TGS_WIDGET_BOX);
    be->set_element_rect(btn, 320, 0, 80, 40);
    be->set_element_content(btn, "OK");

    void *result = be->create_element(vlayout, TGS_WIDGET_TEXT);
    be->set_element_rect(result, 10, 100, 400, 30);
    be->set_element_content(result, "");
}

/* One widget of every tgs_widget_type — smoke test for the backend mapping. */
static void build_library(tgs_backend *be, void *win)
{
    static const struct { tgs_widget_type type; const char *text; } items[] = {
        { TGS_WIDGET_TEXT,    "label" },
        { TGS_WIDGET_BOX,   "button" },
        { TGS_WIDGET_TEXT,    "input" },
        { TGS_WIDGET_BOX, "checkbox" },
        { TGS_WIDGET_BOX,   "" },
        { TGS_WIDGET_BOX, "" },
        { TGS_WIDGET_GRAPHIC,    "" },
        { TGS_WIDGET_BOX, "" },
        { TGS_WIDGET_BOX,   "" },
        { TGS_WIDGET_BOX,   "" },
    };
    size_t n = sizeof(items) / sizeof(items[0]);
    int x = 10, y = 10;
    size_t k;

    for (k = 0; k < n; k++) {
        void *w = be->create_element(win, items[k].type);
        be->set_element_rect(w, x, y, 180, 70);
        be->set_element_content(w, items[k].text);
        x += 195;
        if (x > 700) { x = 10; y += 95; }
    }
}

/* Write the current draw buffer to a PNG. */
static int dump_png(tgs_display *disp, tgs_backend *be, const char *path)
{
    (void)be;
    /* ARGB8888 is BGRA in memory on little-endian; PNG wants RGBA. */
    const size_t npx = (size_t)WIDTH * (size_t)HEIGHT;
    uint8_t *rgba = (uint8_t *)malloc(npx * 4);

    if (!rgba) {
        fprintf(stderr, "render_demo: out of memory\n");
        return -1;
    }
    for (size_t p = 0; p < npx; p++) {
        const uint8_t *src = disp->buffer + p * 4;
        uint8_t *dst = rgba + p * 4;
        dst[0] = src[2]; /* R */
        dst[1] = src[1]; /* G */
        dst[2] = src[0]; /* B */
        dst[3] = 0xFF;   /* opaque */
    }
    if (!stbi_write_png(path, WIDTH, HEIGHT, 4, rgba, WIDTH * 4)) {
        fprintf(stderr, "render_demo: failed to write %s\n", path);
        free(rgba);
        return -1;
    }
    free(rgba);
    fprintf(stderr, "render_demo: wrote %s (%dx%d)\n", path, WIDTH, HEIGHT);
    return 0;
}

/* states demo: one widget of each interactive type, plus a program-side hover
 * response (blue border on HOVER_ENTER, cleared on HOVER_LEAVE) — the same
 * policy the compositor's EVT_BIND subscription model enables. */
static void states_hover(void *handle, tgs_event_type type,
                         const char *data, void *ud)
{
    tgs_backend *be = (tgs_backend *)ud;

    (void)data;
    if (type == TGS_EVENT_HOVER_ENTER) {
        be->set_element_style(handle, TGS_STYLE_BORDER_WIDTH, 3);
        be->set_element_style(handle, TGS_STYLE_BORDER_COLOR, 0x4C9AFF);
    } else if (type == TGS_EVENT_HOVER_LEAVE) {
        be->set_element_style(handle, TGS_STYLE_BORDER_WIDTH, 0);
    }
}

static void build_states(tgs_backend *be, void *win)
{
    static const struct { tgs_widget_type type; int x, y, w, h; const char *t; } items[] = {
        { TGS_WIDGET_BOX,    20,  20, 140, 56, "Button" },
        { TGS_WIDGET_TEXT,     20,  96, 140, 56, "" },
        { TGS_WIDGET_BOX,  20, 172, 140, 40, "Check" },
        { TGS_WIDGET_BOX,  20, 232, 140, 40, "Radio" },
        { TGS_WIDGET_BOX,    20, 292, 140, 48, "" },
        { TGS_WIDGET_BOX,  20, 360, 140, 40, "" },
        { TGS_WIDGET_BOX,   180, 320, 160, 80, "" },
        { TGS_WIDGET_BOX,   360,  20, 160, 40, "" },
        { TGS_WIDGET_BOX,360,  80, 160, 120, "" },
    };
    size_t n = sizeof(items) / sizeof(items[0]);
    size_t k;

    be->set_event_callback(states_hover, be);
    for (k = 0; k < n; k++) {
        void *w = be->create_element(win, items[k].type);
        be->set_element_rect(w, items[k].x, items[k].y,
                            items[k].w, items[k].h);
        be->set_element_content(w, items[k].t);
    }
}

/* Run the interaction-state sequence and dump one PNG per state. */
static int run_states(tgs_backend *be, tgs_display *disp, const char *base)
{
    char path[256];
    int i;


    /* normal */
    snprintf(path, sizeof(path), "%s_normal.png", base);
    if (dump_png(disp, be, path) < 0) return -1;

    /* hover the Button (center 90,48) - the callback paints a blue border */
    be->inject_pointer(90, 48, 1);
    for (i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }
    snprintf(path, sizeof(path), "%s_hover.png", base);
    if (dump_png(disp, be, path) < 0) return -1;

    /* press then release: pressed look, then clicked + focused */
    be->inject_pointer(90, 48, 0);
    for (i = 0; i < 3; i++) {
        be->tick(16);
        be->render();
    }
    snprintf(path, sizeof(path), "%s_pressed.png", base);
    if (dump_png(disp, be, path) < 0) return -1;
    be->inject_pointer(90, 48, 2);
    for (i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }
    snprintf(path, sizeof(path), "%s_clicked.png", base);
    if (dump_png(disp, be, path) < 0) return -1;

    /* hover the Slider (center 90,316) */
    be->inject_pointer(90, 316, 1);
    for (i = 0; i < 5; i++) {
        be->tick(16);
        be->render();
    }
    snprintf(path, sizeof(path), "%s_slider_hover.png", base);
    if (dump_png(disp, be, path) < 0) return -1;

    return 0;
}

/* singles: one widget per screenshot, rendered alone and centered, in
 * normal / hover / pressed states. Layout containers get children so their
 * arrangement is visible. */
static const struct { tgs_widget_type type; const char *name; const char *t; } singles[] = {
    { TGS_WIDGET_TEXT,    "text",      "Label text" },
    { TGS_WIDGET_BOX,     "box",       "" },
    { TGS_WIDGET_GRAPHIC, "graphic",   "" },
};

/* Give containers children so their arrangement is visible. */
static void populate_single(tgs_backend *be, void *w, tgs_widget_type type)
{
    switch (type) {
    case TGS_WIDGET_BOX: {
        /* program-computed layout: label row, then two boxes side by side */
        void *a = be->create_element(w, TGS_WIDGET_TEXT);
        void *b = be->create_element(w, TGS_WIDGET_BOX);
        void *c = be->create_element(w, TGS_WIDGET_BOX);
        be->set_element_rect(a, 0, 0, 250, 24);
        be->set_element_content(a, "row one (program-computed rects)");
        be->set_element_rect(b, 0, 30, 120, 36);
        be->set_element_content(b, "left");
        be->set_element_rect(c, 130, 30, 120, 36);
        be->set_element_content(c, "right");
        break;
    }
    default:
        break;
    }
}

static int run_singles(tgs_backend *be, tgs_display *disp, const char *dir)
{
    size_t k;
    size_t n = sizeof(singles) / sizeof(singles[0]);

    be->set_event_callback(states_hover, be);
    for (k = 0; k < n; k++) {
        void *w;
        char path[300];
        int i;

        w = be->create_element(NULL, singles[k].type);
        if (!w) return -1;
        be->set_element_rect(w, 270, 250, 260, 90);
        be->set_element_content(w, singles[k].t);
        populate_single(be, w, singles[k].type);

        for (i = 0; i < 10; i++) {
            be->tick(16);
            be->render();
        }
        snprintf(path, sizeof(path), "%s/%s_normal.png", dir, singles[k].name);
        if (dump_png(disp, be, path) < 0) return -1;

        /* hover the widget's center (400,300) */
        be->inject_pointer(400, 300, 1);
        for (i = 0; i < 5; i++) {
            be->tick(16);
            be->render();
        }
        snprintf(path, sizeof(path), "%s/%s_hover.png", dir, singles[k].name);
        if (dump_png(disp, be, path) < 0) return -1;

        /* pressed */
        be->inject_pointer(400, 300, 0);
        for (i = 0; i < 3; i++) {
            be->tick(16);
            be->render();
        }
        snprintf(path, sizeof(path), "%s/%s_pressed.png", dir, singles[k].name);
        if (dump_png(disp, be, path) < 0) return -1;

        be->inject_pointer(400, 300, 2);
        for (i = 0; i < 5; i++) {
            be->tick(16);
            be->render();
        }
    }
    return 0;
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
    scene_backend_register();
    be = tgs_backend_get();
    if (!be) {
        fprintf(stderr, "render_demo: no backend registered\n");
        return 1;
    }

    scene_backend_set_display(&disp);
    if (be->init(WIDTH, HEIGHT) < 0) {
        fprintf(stderr, "render_demo: backend init failed\n");
        return 1;
    }

    /* --- singles mode: one widget per screenshot, its own windows --- */
    if (strcmp(demo, "singles") == 0) {
        int rc = run_singles(be, &disp, "docs/screenshots/singles");
        be->deinit();
        return rc < 0 ? 1 : 0;
    }

    /* --- Build the element tree of the selected demo on the canvas --- */
    void *win = NULL;

    if (strcmp(demo, "container") == 0) {
        build_container_demo(be, win);
    } else if (strcmp(demo, "library") == 0) {
        build_library(be, win);
    } else if (strcmp(demo, "simple_form") == 0) {
        build_simple_form(be, win);
    } else if (strcmp(demo, "states") == 0) {
        build_states(be, win);
    } else {
        fprintf(stderr,
                "render_demo: unknown demo '%s' "
                "(expected simple_form|container|library|states)\n",
                demo);
        be->deinit();
        return 1;
    }

    /* --- Pump the scene: paint into the shared draw buffer --- */
    for (i = 0; i < 10; i++) {
        be->tick(16);
        be->render();
    }

    if (!disp.buffer) {
        fprintf(stderr, "render_demo: backend did not expose a draw buffer\n");
        be->deinit();
        return 1;
    }

    if (strcmp(demo, "states") == 0) {
        char base[256];
        const char *slash = strrchr(out_path, '.');
        size_t blen = slash ? (size_t)(slash - out_path) : strlen(out_path);
        if (blen >= sizeof(base)) blen = sizeof(base) - 1;
        memcpy(base, out_path, blen);
        base[blen] = '\0';
        if (run_states(be, &disp, base) < 0) {
            be->deinit();
            return 1;
        }
    } else {
        if (dump_png(&disp, be, out_path) < 0) {
            be->deinit();
            return 1;
        }
    }

    be->deinit();
    return 0;
}
