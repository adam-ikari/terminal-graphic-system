/*
 * scene_core.c — tree, geometry, hit-testing, event queue, painting.
 * Engine-independent (C89); all pixels flow through the tgs_paint port.
 */
#include "tgs_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scene_init(tgs_scene *s)
{
    memset(s, 0, sizeof(*s));
}

void scene_deinit(tgs_scene *s)
{
    int i;

    for (i = 0; i < SCENE_MAX_NODES; i++) {
        if (s->nodes[i].used) {
            free(s->nodes[i].text);
            free(s->nodes[i].paint_priv);
        }
    }
    memset(s, 0, sizeof(*s));
}

scene_node *scene_node_by_id(tgs_scene *s, int id)
{
    if (id < 0 || id >= SCENE_MAX_NODES) return NULL;
    return s->nodes[id].used ? &s->nodes[id] : NULL;
}

scene_node *scene_create_node(tgs_scene *s, tgs_widget_type type,
                              scene_node *parent)
{
    int i;
    scene_node *n;

    for (i = 0; i < SCENE_MAX_NODES; i++) {
        if (!s->nodes[i].used) break;
    }
    if (i >= SCENE_MAX_NODES) return NULL;
    n = &s->nodes[i];
    memset(n, 0, sizeof(*n));
    n->used = 1;
    n->id = i;
    n->type = type;
    n->parent = parent;
    n->visible = 1;
    /* Interactive kinds take focus by default; structure/paint kinds don't. */
    switch (type) {
    case TGS_WIDGET_BUTTON:
    case TGS_WIDGET_INPUT:
    case TGS_WIDGET_CHECKBOX:
    case TGS_WIDGET_SLIDER:
        n->focusable = 1;
        break;
    default:
        n->focusable = 0;
        break;
    }
    if (parent) {
        if (parent->child_count >= SCENE_MAX_CHILDREN) {
            n->used = 0;
            return NULL;
        }
        parent->children[parent->child_count++] = n;
    }
    return n;
}

static void detach(scene_node *n)
{
    scene_node *p = n->parent;
    int i, k = 0;

    if (!p) return;
    for (i = 0; i < p->child_count; i++) {
        if (p->children[i] != n) p->children[k++] = p->children[i];
    }
    p->child_count = k;
    n->parent = NULL;
}

void scene_destroy_node(tgs_scene *s, scene_node *n)
{
    int i;

    if (!n || !n->used) return;
    /* Depth-first child teardown; SCENE_MAX_NODES bounds recursion.
     * Each child detaches itself, compacting the array — walk backwards
     * so no child is skipped by the shifting. */
    for (i = n->child_count - 1; i >= 0; i--) {
        if (n->children[i]) scene_destroy_node(s, n->children[i]);
    }
    detach(n);
    free(n->text);
    free(n->paint_priv);
    memset(n, 0, sizeof(*n));
}

void scene_abs_rect(scene_node *n, int *x, int *y, int *w, int *h)
{
    scene_node *p;
    int ax = n->x, ay = n->y;

    for (p = n->parent; p; p = p->parent) {
        ax += p->x;
        ay += p->y;
    }
    if (x) *x = ax;
    if (y) *y = ay;
    if (w) *w = n->w;
    if (h) *h = n->h;
}

scene_node *scene_hit(tgs_scene *s, int ax, int ay)
{
    scene_node *best = NULL;
    int i;

    /* Linear scan in creation order; later siblings paint on top, so the
     * last hit wins (z-order = creation order within a parent). */
    for (i = 0; i < SCENE_MAX_NODES; i++) {
        scene_node *n = &s->nodes[i];
        int nx, ny;

        if (!n->used || !n->visible || n == best) continue;
        scene_abs_rect(n, &nx, &ny, NULL, NULL);
        if (ax >= nx && ax < nx + n->w && ay >= ny && ay < ny + n->h) {
            /* Window roots are structure, not widgets: a hit on one is
             * "no widget" (§A.1), so only parented nodes are targets. */
            if (n->parent != NULL)
                best = n;
        }
    }
    return best;
}

void scene_emit(tgs_scene *s, scene_node *n, tgs_event_type type,
                const char *data)
{
    scene_event *e;
    int next = (s->evq_tail + 1) % SCENE_EVENT_QUEUE;

    if (next == s->evq_head) return;  /* full: drop */
    e = &s->evq[s->evq_tail];
    e->node = n;
    e->type = type;
    e->data[0] = 0;
    if (data) {
        strncpy(e->data, data, sizeof(e->data) - 1);
        e->data[sizeof(e->data) - 1] = 0;
    }
    s->evq_tail = next;
}

void scene_pump_events(tgs_scene *s)
{
    while (s->evq_head != s->evq_tail) {
        scene_event *e = &s->evq[s->evq_head];
        s->evq_head = (s->evq_head + 1) % SCENE_EVENT_QUEUE;
        if (s->event_cb && e->node && e->node->used)
            s->event_cb(e->node, e->type, e->data[0] ? e->data : NULL,
                        s->event_ud);
    }
}

/* ---------- defaults ---------- */

uint32_t scene_default_bg(tgs_widget_type t)
{
    switch (t) {
    case TGS_WIDGET_BUTTON:   return 0xFF2D5AA8;
    case TGS_WIDGET_INPUT:    return 0xFF1E1E1E;
    case TGS_WIDGET_CHECKBOX: return 0x00000000;
    case TGS_WIDGET_SLIDER:   return 0xFF3A3A3A;
    case TGS_WIDGET_SCROLL:   return 0xFF242424;
    case TGS_WIDGET_IMAGE:    return 0xFF101010;
    case TGS_WIDGET_CONTAINER: return 0x00000000;
    default:                  return 0x00000000;
    }
}

uint32_t scene_default_fg(tgs_widget_type t)
{
    (void)t;
    return 0xFFEEEEEE;
}

static int slider_knob_rect(scene_node *n, int *kx)
{
    /* v in 0..100 maps to the knob center across the track. */
    if (n->w < 8) return 0;
    *kx = n->x + 4 + (n->w - 8) * n->slider_value / 100;
    return 1;
}

/* ---------- painting ---------- */

static void paint_node(tgs_scene *s, scene_node *n, int ox, int oy)
{
    tgs_paint *p = s->paint;
    int x = ox + n->x, y = oy + n->y;
    int i;
    uint32_t bg = n->has_bg ? n->bg_color : scene_default_bg(n->type);
    uint32_t fg = n->has_fg ? n->fg_color : scene_default_fg(n->type);
    int bw = n->has_border_w ? n->border_w : 0;
    uint32_t bc = n->has_border_color ? n->border_color : 0xFF666666;
    int radius = n->has_radius ? n->radius : 0;
    int fsize = n->has_font_size ? n->font_size : 14;

    if (!n->visible || !p) return;
    switch (n->type) {
    case TGS_WIDGET_BUTTON:
        p->box(p, x, y, n->w, n->h, radius ? radius : 4,
               bg, 1, bc, 0);
        p->text(p, x, y, n->w, n->h, n->text ? n->text : "", fsize, fg, 1);
        break;
    case TGS_WIDGET_LABEL:
        p->text(p, x, y, n->w, n->h, n->text ? n->text : "", fsize, fg, 0);
        break;
    case TGS_WIDGET_INPUT: {
        int cw = 8;
        int th = fsize + 4;
        int tx = x + 8, ty = y + (n->h - th) / 2;
        p->box(p, x, y, n->w, n->h, radius ? radius : 4, bg, 1,
               s->focused == n ? 0xFF88AAFF : bc,
               s->focused == n ? 2 : 1);
        if (n->text) {
            p->text(p, x, ty, n->w - 16, th, n->text, fsize, fg, 0);
            p->measure(p, n->text, fsize, &cw, NULL);
        }
        if (s->focused == n) {
            p->box(p, tx + cw + 1, ty, 2, th, 0, fg, 1, 0, 0);
        }
        if (n->paint_priv) {       /* preedit chip (§H.2) */
            int pw = 0, ph = 0;
            p->measure(p, (char *)n->paint_priv, fsize, &pw, &ph);
            p->box(p, x, y - ph - 4 > 0 ? y - ph - 4 : y, pw + 12, ph + 6,
                   3, 0xFF222222, 1, 0, 0);
            p->text(p, x + 6, y - ph - 4 > 0 ? y - ph - 4 : y, pw, ph,
                    (char *)n->paint_priv, fsize, fg, 0);
        }
        break;
    }
    case TGS_WIDGET_CHECKBOX: {
        int bs = n->h < 24 ? n->h - 8 : 16;
        int bx = x + 2, by = y + (n->h - bs) / 2;
        p->box(p, bx, by, bs, bs, radius ? radius : 3,
               n->checked ? 0xFF2D5AA8 : bg, 1, bc, 1);
        if (n->checked)
            p->box(p, bx + 4, by + 4, bs - 8, bs - 8, radius ? radius : 2,
                   0xFFEEEEEE, 1, 0, 0);
        p->text(p, bx + bs + 8, y, n->w - bs - 10, n->h,
                n->text ? n->text : "", fsize, fg, 0);
        break;
    }
    case TGS_WIDGET_SLIDER: {
        int ky = y + n->h / 2;
        int kx;
        p->box(p, x, ky - 3, n->w, 6, 3, bg, 1, 0, 0);
        if (slider_knob_rect(n, &kx)) {
            p->box(p, kx, ky - 8, 8, 16, 4, 0xFFDDDDDD, 1, 0, 0);
        }
        break;
    }
    case TGS_WIDGET_CONTAINER:
        if (bg) p->box(p, x, y, n->w, n->h, radius, bg, 1, bc, bw);
        break;
    case TGS_WIDGET_SCROLL:
        p->box(p, x, y, n->w, n->h, radius, bg, 1, bc, 1);
        break;
    case TGS_WIDGET_IMAGE:
        p->box(p, x, y, n->w, n->h, radius, bg, 1, bc, 1);
        /* pixel source arrives via resources (stream 2) — placeholder fill */
        break;
    default:
        break;
    }
    for (i = 0; i < n->child_count; i++) {
        paint_node(s, n->children[i], x, y);
    }
}

void scene_set_underlay(tgs_scene *s, const uint32_t *px, int w, int h)
{
    s->underlay_px = px;
    s->underlay_w = w;
    s->underlay_h = h;
}

void scene_draw(tgs_scene *s)
{
    tgs_paint *p = s->paint;
    int i;

    if (!p || !p->box) return;
    p->clip(p, 0, 0, 0, 0);           /* reset */
    /* Character base first: widgets composite above it. */
    if (p->underlay)
        p->underlay(p, s->underlay_px, s->underlay_w, s->underlay_h);
    /* Window backgrounds: normal windows paint dark; a TRANSPARENT window
     * (no bg override) skips its fill so the character base shows through. */
    for (i = 0; i < s->window_count; i++) {
        scene_node *w = s->windows[i];
        if (!w || !w->visible) continue;
        paint_node(s, w, 0, 0);
    }
    /* Focus ring, painted last so it overlays siblings (§I.3). */
    if (s->focused) {
        int fx, fy, fw, fh;
        scene_abs_rect(s->focused, &fx, &fy, &fw, &fh);
        p->box(p, fx - 2, fy - 2, fw + 4, fh + 4, 4, 0, 0, 0xFF88AAFF, 1);
    }
}
