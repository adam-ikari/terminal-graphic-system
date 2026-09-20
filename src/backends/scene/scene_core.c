/*
 * scene_core.c — tree, geometry, hit-testing, event queue, painting.
 * Engine-independent; all pixels flow through the tgs_paint port.
 */
#include "tgs_scene.h"

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
        if (s->nodes[i].used) free(s->nodes[i].text);
    }
    memset(s, 0, sizeof(*s));
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
    /* Depth-first child teardown; walk backwards so detach's compaction of
     * the sibling array never skips a child. */
    for (i = n->child_count - 1; i >= 0; i--) {
        if (n->children[i]) scene_destroy_node(s, n->children[i]);
    }
    detach(n);
    if (s->hovered == n) s->hovered = NULL;
    free(n->text);
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

    /* Linear scan; later creation order paints on top, so the last hit wins
     * (z-order = creation order within a parent). */
    for (i = 0; i < SCENE_MAX_NODES; i++) {
        scene_node *n = &s->nodes[i];
        int nx, ny;

        if (!n->used || !n->visible) continue;
        scene_abs_rect(n, &nx, &ny, NULL, NULL);
        if (ax >= nx && ax < nx + n->w && ay >= ny && ay < ny + n->h)
            best = n;
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
        if (s->event_cb && (!e->node || e->node->used))
            s->event_cb(e->node, e->type, e->data[0] ? e->data : NULL,
                        s->event_ud);
    }
}

void scene_set_underlay(tgs_scene *s, const uint32_t *px, int w, int h)
{
    s->underlay_px = px;
    s->underlay_w = w;
    s->underlay_h = h;
}

/* ---------- defaults ---------- */

uint32_t scene_default_bg(tgs_widget_type t)
{
    switch (t) {
    case TGS_WIDGET_BOX:      return 0xFF222222;
    case TGS_WIDGET_GRAPHIC:  return 0xFF101010;
    default:                  return 0x00000000;   /* TEXT: transparent */
    }
}

uint32_t scene_default_fg(tgs_widget_type t)
{
    (void)t;
    return 0xFFEEEEEE;
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
    case TGS_WIDGET_TEXT:
        p->text(p, x, y, n->w, n->h, n->text ? n->text : "", fsize, fg, 0);
        break;
    case TGS_WIDGET_BOX:
        if (bg) p->box(p, x, y, n->w, n->h, radius, bg, 1, bc, bw);
        break;
    case TGS_WIDGET_GRAPHIC:
        /* vector shape; default circle (radius = half-min-edge). */
        p->box(p, x, y, n->w, n->h, radius ? radius : (n->w < n->h ? n->w : n->h) / 2,
               bg, 1, bc, bw ? bw : 1);
        break;
    default:
        break;
    }
    for (i = 0; i < n->child_count; i++) {
        paint_node(s, n->children[i], x, y);
    }
}

void scene_draw(tgs_scene *s)
{
    tgs_paint *p = s->paint;

    if (!p || !p->box) return;
    p->clip(p, 0, 0, 0, 0);           /* reset */
    /* Character base first: elements composite above it. */
    int i;
    if (p->underlay)
        p->underlay(p, s->underlay_px, s->underlay_w, s->underlay_h);
    for (i = 0; i < SCENE_MAX_NODES; i++) {
        if (s->nodes[i].used && !s->nodes[i].parent)
            paint_node(s, &s->nodes[i], 0, 0);
    }
}
