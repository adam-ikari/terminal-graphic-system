/*
 * scene_backend.c — the drawing-semantics backend.
 *
 * Implements the compositor's tgs_backend vtable over the scene core:
 * one canvas tree, element create/rect/content/style/destroy, pointer and
 * key injection, hit-testing. Painting goes through the tgs_paint port,
 * so paint_skia.c can replace the engine without touching this file.
 *
 * There is no window, focus, navigation, layout, or text-editing state:
 * the program owns those through events and repaints.
 */
#include "tgs_scene.h"
#include "tgs_backend.h"
#include "../compositor/output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tgs_scene g_scene;
static tgs_paint g_paint;
static uint8_t *g_fb;
static tgs_display *g_display;              /* published-buffer consumer */
static tgs_backend *g_self;                 /* registered backend */

/* Buffer contract: this module allocates the fb, publishes it through
 * g_display, and output_present() pushes it out. */
void scene_backend_set_display(tgs_display *display)
{
    g_display = display;
}
static scene_node *g_press_target;           /* element under a press edge */

/* Character-base underlay: the term view's pixels, blitted under the scene
 * on every render (compositor calls this after rebuilds). */
void scene_backend_set_underlay(const uint32_t *px, int w, int h)
{
    scene_set_underlay(&g_scene, px, w, h);
}

/* ---------- lifecycle ---------- */

static int backend_init(int width, int height)
{
    size_t sz = (size_t)width * (size_t)height * 4u;

    scene_init(&g_scene);
    g_fb = (uint8_t *)malloc(sz);
    if (!g_fb) return -1;
    memset(g_fb, 0, sz);

    paint_sdl2_init(&g_paint, (uint32_t *)g_fb, width, height);
    g_scene.paint = &g_paint;
    g_scene.disp_w = width;
    g_scene.disp_h = height;

    if (g_display) {
        g_display->width  = width;
        g_display->height = height;
        g_display->bpp    = 32;
        g_display->stride = width * 4;
        g_display->buffer = g_fb;
    }
    return 0;
}

static void backend_deinit(void)
{
    if (g_paint.deinit) g_paint.deinit(&g_paint);
    scene_deinit(&g_scene);
    free(g_fb);
    g_fb = NULL;
    if (g_display) g_display->buffer = NULL;
}

static void backend_tick(uint32_t ms)
{
    (void)ms;
    scene_pump_events(&g_scene);
}

static void backend_render(void)
{
    scene_draw(&g_scene);
}

static void backend_set_size(int w, int h)
{
    uint8_t *nb;
    size_t sz;

    if (w < 1 || h < 1) return;
    if (w == g_scene.disp_w && h == g_scene.disp_h) return;
    sz = (size_t)w * (size_t)h * 4u;
    nb = (uint8_t *)malloc(sz);
    if (!nb) return;
    memset(nb, 0, sz);
    free(g_fb);
    g_fb = nb;
    if (g_paint.deinit) g_paint.deinit(&g_paint);
    paint_sdl2_init(&g_paint, (uint32_t *)g_fb, w, h);
    g_scene.paint = &g_paint;
    g_scene.disp_w = w;
    g_scene.disp_h = h;
    if (g_display) {
        g_display->width  = w;
        g_display->height = h;
        g_display->stride = w * 4;
        g_display->buffer = g_fb;
    }
}

/* ---------- elements ---------- */

static scene_node *node_of(void *handle)
{
    scene_node *n = (scene_node *)handle;

    if (!n || !n->used) return NULL;
    return n;
}

static void *backend_create_element(void *parent, tgs_widget_type type)
{
    return scene_create_node(&g_scene, type, node_of(parent));
}

static void backend_destroy_element(void *handle)
{
    scene_node *n = node_of(handle);

    if (!n) return;
    scene_destroy_node(&g_scene, n);
}

static void backend_set_element_rect(void *handle, int x, int y, int w, int h)
{
    scene_node *n = node_of(handle);

    if (!n) return;
    n->x = x; n->y = y; n->w = w; n->h = h;
}

static void backend_set_element_content(void *handle, const char *text)
{
    scene_node *n = node_of(handle);
    char *dup;

    if (!n) return;
    dup = text ? strdup(text) : NULL;
    free(n->text);
    n->text = dup;
}

static void backend_set_element_style(void *handle, tgs_style_prop prop,
                                      int32_t value)
{
    scene_node *n = node_of(handle);

    if (!n) return;
    switch (prop) {
    case TGS_STYLE_BG_COLOR:     n->bg_color = (uint32_t)value; n->has_bg = 1; break;
    case TGS_STYLE_FG_COLOR:     n->fg_color = (uint32_t)value; n->has_fg = 1; break;
    case TGS_STYLE_RADIUS:       n->radius = value; n->has_radius = 1; break;
    case TGS_STYLE_BORDER_WIDTH: n->border_w = value; n->has_border_w = 1; break;
    case TGS_STYLE_BORDER_COLOR: n->border_color = (uint32_t)value; n->has_border_color = 1; break;
    case TGS_STYLE_FONT_SIZE:    n->font_size = value; n->has_font_size = 1; break;
    default: break;               /* SHAPE/OPACITY: not painted yet */
    }
}

/* ---------- events ---------- */

static void backend_set_event_callback(tgs_event_cb cb, void *user_data)
{
    g_scene.event_cb = cb;
    g_scene.event_ud = user_data;
}

/* ---------- input ---------- */

/* Pointer edge contract: phase 0=press, 1=move, 2=release. The renderer
 * does hit-testing, emits HOVER enter/leave on movement across elements,
 * emits CLICK on a press+release over the same element, and reports every
 * pointer coordinate as POINTER so the program can do its own drag state. */
static void backend_inject_pointer(int x, int y, int phase)
{
    scene_node *hit;
    char buf[32];

    hit = scene_hit(&g_scene, x, y);

    /* Hover bookkeeping: emit enter/leave on element change (moves only). */
    if (phase == 1 && hit != g_scene.hovered) {
        if (g_scene.hovered && g_scene.hovered->used)
            scene_emit(&g_scene, g_scene.hovered, TGS_EVENT_HOVER_LEAVE, NULL);
        g_scene.hovered = hit;
        if (hit) scene_emit(&g_scene, hit, TGS_EVENT_HOVER_ENTER, NULL);
    }

    /* CLICK: press then release on the same element. */
    if (phase == 0) {
        g_press_target = hit;
    } else if (phase == 2) {
        if (hit && hit == g_press_target)
            scene_emit(&g_scene, hit, TGS_EVENT_CLICK, NULL);
        g_press_target = NULL;
    }

    /* POINTER coordinates always reported (the drag primitive). node may
     * be NULL (bare canvas) — the pump delivers those too. */
    snprintf(buf, sizeof buf, "%d;%d;%d", x, y, phase);
    scene_emit(&g_scene, hit, TGS_EVENT_POINTER, buf);
}

static void backend_inject_key(int key, int mods, int pressed)
{
    char buf[32];

    if (!pressed) return;
    /* Keys go to the program (node NULL — no element anchor); the WM
     * routes them to the IME program or the app. */
    snprintf(buf, sizeof buf, "%d;%d", key, mods);
    scene_emit(&g_scene, NULL, TGS_EVENT_KEY, buf);
}

/* ---------- hit-test ---------- */

static void *backend_hit_test(int x, int y)
{
    return scene_hit(&g_scene, x, y);
}

/* ---------- clipboard ---------- */

static const char *backend_clipboard_text(void)
{
#ifdef TGS_USE_SDL
    return SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
#else
    return NULL;
#endif
}

/* ---------- registration ---------- */

static tgs_backend scene_backend;

void scene_backend_register(void)
{
    memset(&scene_backend, 0, sizeof(scene_backend));
    scene_backend.user_data = &g_scene;
    scene_backend.init = backend_init;
    scene_backend.tick = backend_tick;
    scene_backend.deinit = backend_deinit;
    scene_backend.render = backend_render;
    scene_backend.set_size = backend_set_size;
    scene_backend.create_element = backend_create_element;
    scene_backend.set_element_rect = backend_set_element_rect;
    scene_backend.set_element_content = backend_set_element_content;
    scene_backend.set_element_style = backend_set_element_style;
    scene_backend.destroy_element = backend_destroy_element;
    scene_backend.set_event_callback = backend_set_event_callback;
    scene_backend.inject_pointer = backend_inject_pointer;
    scene_backend.inject_key = backend_inject_key;
    scene_backend.hit_test = backend_hit_test;
    scene_backend.clipboard_text = backend_clipboard_text;
    g_self = &scene_backend;
    tgs_backend_register(&scene_backend);
}
