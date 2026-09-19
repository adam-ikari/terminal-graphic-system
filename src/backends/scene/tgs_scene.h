/*
 * tgs_scene.h — drawing-semantics renderer core.
 *
 * The renderer is a scene painter: it holds the widget tree the compositor
 * builds through the backend vtable, runs the interaction state machines
 * (hit-testing, focus visuals, text editing), and paints through a paint
 * port. Two paint ports exist: SDL2 (paint_sdl2.c) and Skia (paint_skia.c).
 *
 * The paint port is the ONLY engine-specific seam — everything above it
 * (tree, state, events, focus) is engine-independent C89.
 */
#ifndef TGS_SCENE_H
#define TGS_SCENE_H

#include "tgs_protocol.h"
#include "tgs_backend.h"
#include <stdint.h>

#define SCENE_MAX_NODES   256
#define SCENE_MAX_WINDOWS 8
#define SCENE_MAX_CHILDREN 64
#define SCENE_EVENT_QUEUE 64

/* ------------------------------------------------------------------ */
/* Paint port: what a scene painter needs to put pixels down.          */
/* Coordinate space = the published framebuffer (absolute pixels).      */
/* ------------------------------------------------------------------ */

typedef struct tgs_paint tgs_paint;

struct tgs_paint {
    void *priv;

    /* Paint a rounded box. has_fill==0 skips the fill; border_w==0 skips
     * the border. Clip is whatever set_clip last installed. */
    void (*box)(tgs_paint *p, int x, int y, int w, int h, int radius,
                uint32_t fill, int has_fill,
                uint32_t border, int border_w);

    /* Draw UTF-8 text inside the rect: align 0=left, 1=center (horizontal),
     * always vertically centered. Missing glyphs are skipped. */
    void (*text)(tgs_paint *p, int x, int y, int w, int h,
                 const char *s, int font_size, uint32_t color, int align);

    /* Pixel size of the text at the given font size. 0 on success. */
    int (*measure)(tgs_paint *p, const char *s, int font_size,
                   int *w, int *h);

    /* Raw ARGB8888 copy of the character base, full-canvas, under
     * everything. NULL pixels clears the base. */
    void (*underlay)(tgs_paint *p, const uint32_t *px, int w, int h);

    /* Install/reset the clip rect (subsequent box/text calls clip to it). */
    void (*clip)(tgs_paint *p, int x, int y, int w, int h); /* w|h<=0 = reset */

    /* Release engine resources (fonts, surfaces). */
    void (*deinit)(tgs_paint *p);
};

/* SDL2 + SDL2_ttf paint port. Fonts: DejaVu Sans at fixed pixel sizes;
 * every render sizes map to one of the cached faces. */
enum { SCENE_FONT_SIZES = 4 };

void paint_sdl2_init(tgs_paint *p, uint32_t *fb, int w, int h);

/* ------------------------------------------------------------------ */
/* Scene model                                                         */
/* ------------------------------------------------------------------ */

typedef struct scene_node scene_node;

struct scene_node {
    int used;
    int id;                          /* stable scene id (also the handle) */
    tgs_widget_type type;
    scene_node *parent;
    scene_node *children[SCENE_MAX_CHILDREN];
    int child_count;

    /* Geometry, relative to the parent (absolute = accumulated). */
    int x, y, w, h;

    /* Content. */
    char *text;                      /* malloc'd, NULL when none */
    int cursor;                      /* INPUT: byte offset of the caret */

    /* Style overrides (only set_widget_style writes these; painting falls
     * back to per-kind defaults otherwise). */
    uint32_t bg_color, fg_color, border_color;
    int radius, border_w, font_size;
    unsigned char has_bg, has_fg, has_border_color;
    unsigned char has_radius, has_border_w, has_font_size;

    /* Kind state. */
    int checked;                     /* CHECKBOX */
    int slider_value;                /* SLIDER */
    int visible;                     /* 0 hides the subtree */
    int focusable;                   /* 0 = never a focus target */

    void *paint_priv;                /* paint-side cache (unused for now) */
};

/* Events travel scene → compositor through this queue entry. */
typedef struct {
    scene_node *node;
    tgs_event_type type;
    char data[256];                  /* VALUE_CHANGED payload / "code;mods" */
} scene_event;

typedef struct {
    scene_node nodes[SCENE_MAX_NODES];
    scene_node *windows[SCENE_MAX_WINDOWS];
    int window_count;
    int next_id;

    scene_node *focused;             /* currently focused widget or NULL */
    scene_node *hovered;             /* last widget the pointer entered */
    void *active_window;             /* window receiving keys (set by comp) */

    scene_event evq[SCENE_EVENT_QUEUE];
    int evq_head, evq_tail;

    /* navigation precedence hook (§D.3) — compositor decides first */
    tgs_nav_key_cb nav_key_cb;
    void *nav_key_ud;

    tgs_event_cb event_cb;           /* compositor sink */
    void *event_ud;

    tgs_paint *paint;
    int disp_w, disp_h;

    /* character-base underlay (term_view pixels, ARGB8888) */
    const uint32_t *underlay_px;
    int underlay_w, underlay_h;
} tgs_scene;

/* ---- scene lifecycle (scene_backend.c) ---- */
void scene_init(tgs_scene *s);
void scene_deinit(tgs_scene *s);
scene_node *scene_create_node(tgs_scene *s, tgs_widget_type type,
                              scene_node *parent);
void scene_destroy_node(tgs_scene *s, scene_node *n);
scene_node *scene_node_by_id(tgs_scene *s, int id);

/* Paint the whole scene into the paint port's framebuffer. */
void scene_draw(tgs_scene *s);

/* Character-base underlay: ARGB8888 pixels of the terminal grid
 * (w*h), blitted under the widget scene on every draw. NULL clears. */
void scene_set_underlay(tgs_scene *s, const uint32_t *px, int w, int h);

/* Absolute geometry of a node. */
void scene_abs_rect(scene_node *n, int *x, int *y, int *w, int *h);

/* Hit test: topmost visible node of kind != CONTAINER containing (ax, ay)
 * in absolute coordinates; NULL when none. */
scene_node *scene_hit(tgs_scene *s, int ax, int ay);

/* Fire an event into the queue (truncated payload). */
void scene_emit(tgs_scene *s, scene_node *n, tgs_event_type type,
                const char *data);

/* Drain one event into the compositor callback (called from tick). */
void scene_pump_events(tgs_scene *s);

/* Per-kind visual defaults used when no style override is set. */
uint32_t scene_default_bg(tgs_widget_type t);
uint32_t scene_default_fg(tgs_widget_type t);

#endif /* TGS_SCENE_H */
