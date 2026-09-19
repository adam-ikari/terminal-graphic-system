/*
 * scene_backend.c — the drawing-semantics backend (SDL2 paint port).
 *
 * Implements the compositor's tgs_backend vtable over the scene core:
 * window/widget registry, rect/content/style setters, hit-test events,
 * focus visuals, navigation, text editing for INPUT. Painting goes through
 * the tgs_paint port, so paint_skia.c can replace the engine without
 * touching this file.
 *
 * The renderer-side reference implementation: same vtable, same event
 * contract, same published-framebuffer contract (the buffer this module
 * allocates is what output_present() pushes).
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

/* Buffer contract: this module allocates the fb,
 * publishes it through g_display, and output_present() pushes it out. */
void scene_backend_set_display(tgs_display *display)
{
    g_display = display;
}

/* ---------- helpers ---------- */

static scene_node *node_of(void *handle)
{
    scene_node *n = (scene_node *)handle;

    if (!n || !n->used) return NULL;
    return n;
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

/* Character-base underlay: the term view's pixels, blitted under the
 * widget scene on every render (compositor calls this after rebuilds). */
void scene_backend_set_underlay(const uint32_t *px, int w, int h)
{
    scene_set_underlay(&g_scene, px, w, h);
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

/* ---------- windows ---------- */

static void *backend_create_window(tgs_window_type type, const char *title)
{
    scene_node *root;
    tgs_widget_type rtype = TGS_WIDGET_CONTAINER;

    (void)type; (void)title;
    if (g_scene.window_count >= SCENE_MAX_WINDOWS) return NULL;
    root = scene_create_node(&g_scene, rtype, NULL);
    if (!root) return NULL;
    root->x = 0;
    root->y = 0;
    root->w = g_scene.disp_w;
    root->h = g_scene.disp_h;
    root->focusable = 0;              /* structure, not a widget (§A.1) */
    /* NORMAL windows own an opaque background; TRANSPARENT windows keep the
     * character base visible through the root (L1 mixed mode). */
    if (type == TGS_WINDOW_NORMAL) {
        root->has_bg = 1;
        root->bg_color = 0xFF222222;
    }
    g_scene.windows[g_scene.window_count++] = root;
    return root;
}

/* Is `n` inside `root`'s subtree (inclusive)? Both live. */
static int in_subtree(scene_node *root, scene_node *n)
{
    int i;

    if (!root || !n) return 0;
    if (n == root) return 1;
    for (i = 0; i < root->child_count; i++)
        if (in_subtree(root->children[i], n)) return 1;
    return 0;
}

static void backend_destroy_window(void *handle)
{
    scene_node *root = node_of(handle);
    int i;

    if (!root) return;
    /* Pointer-state may reference the dying subtree; a stale pointer is
     * worse than a dangling one: node slots get reused, and pointer
     * equality would then alias the new widget (hover/focus ghosts). */
    if (in_subtree(root, g_scene.hovered)) g_scene.hovered = NULL;
    if (in_subtree(root, g_scene.focused)) g_scene.focused = NULL;
    for (i = 0; i < g_scene.window_count; i++) {
        if (g_scene.windows[i] == root) {
            g_scene.windows[i] = g_scene.windows[g_scene.window_count - 1];
            g_scene.window_count--;
            break;
        }
    }
    if (g_scene.focused && !g_scene.focused->used) g_scene.focused = NULL;
    scene_destroy_node(&g_scene, root);
}

/* ---------- widgets ---------- */

static void *backend_create_widget(void *parent, tgs_widget_type type)
{
    return scene_create_node(&g_scene, type, node_of(parent));
}

static void backend_destroy_widget(void *handle)
{
    scene_node *n = node_of(handle);

    if (!n) return;
    if (g_scene.focused == n) g_scene.focused = NULL;
    if (g_scene.hovered == n) g_scene.hovered = NULL;
    scene_destroy_node(&g_scene, n);
}

static void backend_set_widget_rect(void *handle, int x, int y, int w, int h)
{
    scene_node *n = node_of(handle);

    if (!n) return;
    n->x = x; n->y = y; n->w = w; n->h = h;
}

static void backend_set_widget_content(void *handle, const char *text)
{
    scene_node *n = node_of(handle);
    char *dup;

    if (!n) return;
    dup = text ? strdup(text) : NULL;
    free(n->text);
    n->text = dup;
    if (n->type == TGS_WIDGET_INPUT && n->cursor > (int)strlen(n->text ? n->text : ""))
        n->cursor = (int)strlen(n->text ? n->text : "");
}

static void backend_insert_widget_text(void *handle, const char *text)
{
    scene_node *n = node_of(handle);
    size_t oldlen, inslen;
    char *nb;

    if (!n || !text) return;
    if (n->type != TGS_WIDGET_INPUT) {
        backend_set_widget_content(handle, text);
        return;
    }
    oldlen = n->text ? strlen(n->text) : 0;
    inslen = strlen(text);
    if (n->cursor < 0 || n->cursor > (int)oldlen) n->cursor = (int)oldlen;
    nb = (char *)malloc(oldlen + inslen + 1);
    if (!nb) return;
    memcpy(nb, n->text ? n->text : "", (size_t)n->cursor);
    memcpy(nb + n->cursor, text, inslen);
    memcpy(nb + n->cursor + inslen, n->text ? n->text + n->cursor : "",
           oldlen - (size_t)n->cursor + 1);
    free(n->text);
    n->text = nb;
    n->cursor += (int)inslen;
}

static void backend_set_widget_preedit(void *handle, const char *text, int cursor)
{
    scene_node *n = node_of(handle);

    /* The preedit overlays the caret; the scene stores it as an attributed
     * suffix the painter draws after the text. Cursor mapping follows §H.2:
     * composition never changes committed text. */
    if (!n || n->type != TGS_WIDGET_INPUT) return;
    (void)cursor;
    free(n->paint_priv);
    n->paint_priv = (text && text[0]) ? strdup(text) : NULL;
}

static void backend_set_widget_style(void *handle, tgs_style_prop prop,
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
    default: break;               /* SHADOW_* and OPACITY: not painted yet */
    }
}

static void backend_set_widget_layout(void *handle, tgs_layout_type layout,
                                      int cols, int rows)
{
    /* WGT_LAYOUT is a retired renderer hint: the scene runs no layout
     * engine, so this is a documented no-op (spec §4.3.1). */
    (void)handle; (void)layout; (void)cols; (void)rows;
}

/* ---------- events ---------- */

static void backend_set_event_callback(tgs_event_cb cb, void *user_data)
{
    g_scene.event_cb = cb;
    g_scene.event_ud = user_data;
}

/* ---------- input ---------- */

/* Pointer edge contract: button 0 is
 * the primary button; pressed is 1 (press edge), 0 (release edge) or
 * -1 (motion only, no button state change). A CLICK completes on the
 * release edge, over the widget under the pointer. */
static void backend_inject_mouse(int x, int y, int button, int pressed)
{
    scene_node *hit;
    scene_node *n;

    if (button != 0) return;          /* only the primary button acts */
    if (pressed == 0) {
        /* Release: complete the click on the widget under the pointer. */
        hit = scene_hit(&g_scene, x, y);
        if (hit) {
            scene_emit(&g_scene, hit, TGS_EVENT_CLICK, NULL);
            if (hit->type == TGS_WIDGET_CHECKBOX)
                hit->checked = !hit->checked;
        }
        /* The pointer may also have moved between edges. */
        n = scene_hit(&g_scene, x, y);
        if (g_scene.hovered != n) {
            if (g_scene.hovered && g_scene.hovered->used)
                scene_emit(&g_scene, g_scene.hovered,
                           TGS_EVENT_HOVER_LEAVE, NULL);
            g_scene.hovered = n;
            if (n) scene_emit(&g_scene, n, TGS_EVENT_HOVER_ENTER, NULL);
        }
        return;
    }
    hit = scene_hit(&g_scene, x, y);
    if (pressed < 0) {
        /* Motion only: hover bookkeeping, no click/focus. */
        if (g_scene.hovered != hit) {
            if (g_scene.hovered && g_scene.hovered->used)
                scene_emit(&g_scene, g_scene.hovered,
                           TGS_EVENT_HOVER_LEAVE, NULL);
            g_scene.hovered = hit;
            if (hit) scene_emit(&g_scene, hit, TGS_EVENT_HOVER_ENTER, NULL);
        }
        return;
    }
    if (!hit) return;
    /* Focus follows the pointer press. */
    if (hit->focusable) {
        if (g_scene.focused != hit) {
            if (g_scene.focused)
                scene_emit(&g_scene, g_scene.focused, TGS_EVENT_BLUR, NULL);
            g_scene.focused = hit;
            scene_emit(&g_scene, hit, TGS_EVENT_FOCUS, NULL);
        }
    }
    if (hit->type == TGS_WIDGET_SLIDER && hit->w > 0) {
        int v = (x - hit->x) * 100 / hit->w;
        char buf[16];
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        hit->slider_value = v;
        snprintf(buf, sizeof buf, "%d", v);
        scene_emit(&g_scene, hit, TGS_EVENT_VALUE_CHANGED, buf);
    }
    /* Hover enter/leave bookkeeping for the whole tree. */
    n = scene_hit(&g_scene, x, y);
    if (g_scene.hovered != n) {
        if (g_scene.hovered && g_scene.hovered->used)
            scene_emit(&g_scene, g_scene.hovered, TGS_EVENT_HOVER_LEAVE, NULL);
        g_scene.hovered = n;
        if (n) scene_emit(&g_scene, n, TGS_EVENT_HOVER_ENTER, NULL);
    }
}

/* Key handling: navigation precedence first (§D.3), then text editing for
 * the focused INPUT, then raw key delivery. */
static void apply_key_to_input(scene_node *n, int key, int mods)
{
    (void)mods;
    if (key == 8) {                   /* backspace */
        size_t len;
        if (!n->text) return;
        len = strlen(n->text);
        if (n->cursor <= 0 || len == 0) return;
        memmove(n->text + n->cursor - 1, n->text + n->cursor,
                len - (size_t)n->cursor + 1);
        n->cursor--;
    } else if (key >= 32 && key < 127) {
        char b[2] = { (char)key, 0 };
        backend_insert_widget_text(n, b);
    } else {
        return;
    }
    scene_emit(&g_scene, n, TGS_EVENT_VALUE_CHANGED, n->text ? n->text : "");
}

static void backend_inject_key(int key, int mods, int pressed)
{
    scene_node *f;
    char buf[32];

    if (!pressed) return;
    if (g_scene.nav_key_cb) {
        tgs_nav_key_action action =
            g_scene.nav_key_cb(key, mods, pressed, g_scene.nav_key_ud);
        if (action == TGS_NAV_CONSUMED) return;
        if (action == TGS_NAV_WIDGET) goto deliver_key;
    }
    /* Text editing: the compositor did not claim the key, so a focused
     * INPUT consumes printable keys/backspace itself. */
    f = g_scene.focused;
    if (f && f->type == TGS_WIDGET_INPUT && !mods) {
        apply_key_to_input(f, key, mods);
        return;
    }
deliver_key:
    /* The compositor's rule-2 hand-off (or an unclaimed key): report the
     * edge as "key;mods" to the focused widget (window_manager.c parses
     * exactly that shape for IME/app routing). */
    f = g_scene.focused;
    if (f) {
        snprintf(buf, sizeof buf, "%d;%d", key, mods);
        scene_emit(&g_scene, f, TGS_EVENT_KEY, buf);
    }
}

/* ---------- focus & navigation ---------- */

static void backend_set_window_ring(void *window, void **widgets, int count)
{
    (void)window; (void)widgets; (void)count;
    /* Ring order lives in the compositor's nav model; the scene only draws
     * the focus visuals for whoever the compositor names. */
}

static void backend_set_active_window(void *window)
{
    g_scene.active_window = window;
}

static void backend_set_focus(void *handle)
{
    g_scene.focused = node_of(handle);
}

/* Nearest candidate in the direction `dir`: pick the candidate whose center
 * is ahead in that direction and closest to `from`'s center. NULL when none
 * qualifies (§J.6 lets the compositor fall back to ring order). */
static void *backend_focus_dir(void *from, void **candidates, int count,
                               tgs_nav_dir dir)
{
    scene_node *f = node_of(from);
    int fx, fy, fw, fh, best = -1;
    long long bestd = 0;
    int i;

    if (!f) return NULL;
    scene_abs_rect(f, &fx, &fy, &fw, &fh);
    fx += fw / 2; fy += fh / 2;
    for (i = 0; i < count; i++) {
        scene_node *c = node_of(candidates[i]);
        long long dx, dy, d;
        int cx, cy, cw, ch;

        if (!c || !c->focusable || c == f) continue;
        scene_abs_rect(c, &cx, &cy, &cw, &ch);
        cx += cw / 2; cy += ch / 2;
        dx = cx - fx; dy = cy - fy;
        if (dir == TGS_NAV_LEFT && dx >= 0) continue;
        if (dir == TGS_NAV_RIGHT && dx <= 0) continue;
        if (dir == TGS_NAV_UP && dy >= 0) continue;
        if (dir == TGS_NAV_DOWN && dy <= 0) continue;
        d = dx * dx + dy * dy;
        if (best < 0 || d < bestd) { best = i; bestd = d; }
    }
    return best >= 0 ? candidates[best] : NULL;
}

static void backend_set_nav_key_cb(tgs_nav_key_cb cb, void *user_data)
{
    g_scene.nav_key_cb = cb;
    g_scene.nav_key_ud = user_data;
}

static void backend_set_widget_focusable(void *handle, int focusable)
{
    scene_node *n = node_of(handle);

    if (n) n->focusable = focusable ? 1 : 0;
}

static const char *backend_clipboard_text(void)
{
#ifdef TGS_USE_SDL
    return SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
#else
    return NULL;
#endif
}

static int backend_widget_geometry(void *handle, int *x, int *y, int *w, int *h)
{
    scene_node *n = node_of(handle);

    if (!n) return -1;
    scene_abs_rect(n, x, y, w, h);
    return 0;
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
    scene_backend.create_window = backend_create_window;
    scene_backend.destroy_window = backend_destroy_window;
    scene_backend.create_widget = backend_create_widget;
    scene_backend.set_widget_rect = backend_set_widget_rect;
    scene_backend.set_widget_content = backend_set_widget_content;
    scene_backend.insert_widget_text = backend_insert_widget_text;
    scene_backend.set_widget_preedit = backend_set_widget_preedit;
    scene_backend.set_widget_style = backend_set_widget_style;
    scene_backend.set_widget_layout = backend_set_widget_layout;
    scene_backend.destroy_widget = backend_destroy_widget;
    scene_backend.set_event_callback = backend_set_event_callback;
    scene_backend.inject_mouse = backend_inject_mouse;
    scene_backend.inject_key = backend_inject_key;
    scene_backend.set_window_ring = backend_set_window_ring;
    scene_backend.set_active_window = backend_set_active_window;
    scene_backend.set_focus = backend_set_focus;
    scene_backend.focus_dir = backend_focus_dir;
    scene_backend.set_widget_focusable = backend_set_widget_focusable;
    scene_backend.set_nav_key_cb = backend_set_nav_key_cb;
    scene_backend.clipboard_text = backend_clipboard_text;
    scene_backend.widget_geometry = backend_widget_geometry;
    g_self = &scene_backend;
    tgs_backend_register(&scene_backend);
}
