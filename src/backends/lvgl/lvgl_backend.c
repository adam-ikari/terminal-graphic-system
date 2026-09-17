/*
 * LVGL Backend for TGS (Terminal Graphic System)
 * Implements the abstract tgs_backend interface using LVGL v9.
 * C99, no external dependencies beyond LVGL and project headers.
 */
#include "tgs_backend.h"
#include "lv_conf.h"
#include <lvgl/lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* The clipboard is the one thing this backend reaches SDL for directly; input
 * and display go through the input and output backends. */
#ifdef TGS_USE_SDL
#include <SDL2/SDL.h>
#endif

#include "output.h"

/* Display buffer pointer — set via lvgl_backend_set_display(). */
static tgs_display *g_display;

/* ---- Static State ---- */

static lv_display_t *disp;
static uint8_t      *draw_buf; /* LVGL's draw buffer: owned here, published as
                                * g_display->buffer. Output backends only read
                                * that pointer in output_present(). */
static lv_indev_t   *mouse_indev;
static lv_indev_t   *kb_indev;
static lv_group_t   *kb_group;  /* the ACTIVE window's group, on the indev */

/* One LVGL root object and keyboard group per window (§I.6). The compositor
 * owns ring ORDER; this registry only maps a window root to its group. */
#define BACKEND_MAX_WINDOWS 16
typedef struct {
    lv_obj_t   *root;
    lv_group_t *group;
} window_slot;

static window_slot g_windows[BACKEND_MAX_WINDOWS];

/* Focus outline (§I.3): added to focusable widgets only, drawn while LVGL has
 * LV_STATE_FOCUSED on them, so exactly the focused widget is outlined. */
static lv_style_t g_focus_style;
static int        g_focus_style_ready;
static lv_obj_t  *g_focus_vis;   /* widget currently drawn as focused */

/* Navigation precedence hook owned by the compositor (§D.3). */
static tgs_nav_key_cb g_nav_key_cb;
static void          *g_nav_key_ud;

/* Pointer state. Button transitions are queued instead of collapsing into one
 * slot: a down+up landing in the same poll batch (fast or programmatic click)
 * would otherwise be invisible to LVGL, which only ever observes the last
 * state written. */
#define INDEV_QUEUE_LEN 32
static int     mouse_x, mouse_y;
static int     mouse_pressed;                /* steady state of the button */
static uint8_t mouse_queue[INDEV_QUEUE_LEN]; /* 1 = press, 0 = release */
static int     mouse_qhead, mouse_qtail;
static int     mouse_qedged;                 /* last state queued — edges only */

/* Keypad state: every press and release is queued and LVGL reads one per
 * indev cycle, exactly like the pointer transitions above. Only a key DOWN
 * means PRESSED — queueing a press on key up as well made every typed
 * character appear twice, and collapsing a burst into one slot dropped keys. */
typedef struct {
    int     code;    /* TGS key code */
    int     mods;    /* TGS modifier mask */
    uint8_t pressed;
} key_event;

static key_event key_queue[INDEV_QUEUE_LEN];
static int       key_qhead, key_qtail;
static int       key_ev_code;  /* TGS code LVGL is delivering right now */
static int       key_ev_mods;  /* its modifier mask, forwarded in TGS_EVENT_KEY */

static tgs_event_cb g_event_cb;
static void        *g_event_user_data;

/* ---- Forward Declarations ---- */

static void lvgl_event_handler(lv_event_t *e);
static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void kb_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

/* ---- Internal Helpers ---- */

/* Canonical TGS key codes — the ONE key space every input source
 * (input_sdl.c, input_fb.c/evdev) normalises into and this backend translates
 * to LVGL. Printable ASCII maps to itself; 1000+ is reserved for keys with no
 * ASCII value. Mirrors docs/navigation.md §D.4.
 *
 *   1000 LEFT   1001 RIGHT   1002 UP   1003 DOWN   1004 HOME   1005 END
 *   1006 PAGEUP 1007 PAGEDOWN — terminal bindings; the widget path has no page
 *                               concept, so these pass through unmapped
 *
 * Modifier mask carried by inject_key(key, mods, pressed) — same section:
 *   TGS_MOD_SHIFT 0x01   TGS_MOD_CTRL 0x02   TGS_MOD_ALT 0x04
 * Left/right variants collapse into the class bit. LVGL has no modifier
 * concept, so Shift is resolved here: Tab+SHIFT becomes LV_KEY_PREV (Tab
 * alone is LV_KEY_NEXT), which is what group focus-prev needs. */
#define TGS_KEY_LEFT   1000
#define TGS_KEY_RIGHT  1001
#define TGS_KEY_UP     1002
#define TGS_KEY_DOWN   1003
#define TGS_KEY_HOME   1004
#define TGS_KEY_END    1005
#define TGS_KEY_PAGEUP   1006
#define TGS_KEY_PAGEDOWN 1007

#define TGS_MOD_SHIFT  0x01
#define TGS_MOD_CTRL   0x02
#define TGS_MOD_ALT    0x04

static uint32_t map_tgs_key(int key, int mods)
{
    switch (key) {
    case TGS_KEY_LEFT:  return LV_KEY_LEFT;
    case TGS_KEY_RIGHT: return LV_KEY_RIGHT;
    case TGS_KEY_UP:    return LV_KEY_UP;
    case TGS_KEY_DOWN:  return LV_KEY_DOWN;
    case TGS_KEY_HOME:  return LV_KEY_HOME;
    case TGS_KEY_END:   return LV_KEY_END;
    case 8:   return LV_KEY_BACKSPACE;
    case 9:   return (mods & TGS_MOD_SHIFT) ? LV_KEY_PREV : LV_KEY_NEXT; /* TAB */
    case 13:  return LV_KEY_ENTER;
    case 27:  return LV_KEY_ESC;
    case 127: return LV_KEY_DEL;
    default:  return (uint32_t)key;
    }
}

/* ---- Widget Creation ---- */

/* A container is a widget too: its type *is* its layout. Containers are bare
 * layout areas — the theme's card look (opaque fill, border, padding and an
 * explicit dark text color that children would inherit) is dropped, so a
 * container never hides or recolors the widgets it places. */
static lv_obj_t *create_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

/* Default grid container: 2 equal columns, up to 4 content-sized rows. LVGL
 * only stores the descriptor pointers, so they must be static; unused CONTENT
 * rows collapse to zero height. */
#define GRID_COLS 2
#define GRID_ROWS 4

static int32_t grid_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static int32_t grid_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT,
                                 LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

/* Drop a new child into the next free cell of its grid container. */
static void grid_place_child(lv_obj_t *grid, lv_obj_t *child)
{
    uint32_t cell = lv_obj_get_child_count(grid) - 1;
    if (cell >= (uint32_t)(GRID_COLS * GRID_ROWS)) cell = GRID_COLS * GRID_ROWS - 1;
    lv_obj_set_grid_cell(child,
                         LV_GRID_ALIGN_START, (int32_t)(cell % GRID_COLS), 1,
                         LV_GRID_ALIGN_START, (int32_t)(cell / GRID_COLS), 1);
}

static lv_obj_t *create_lvgl_widget(tgs_widget_type type, lv_obj_t *parent)
{
    lv_obj_t *obj;

    switch (type) {
    case TGS_WIDGET_BUTTON:   return lv_button_create(parent);
    case TGS_WIDGET_LABEL:    return lv_label_create(parent);
    case TGS_WIDGET_INPUT:    return lv_textarea_create(parent);
    case TGS_WIDGET_CHECKBOX: return lv_checkbox_create(parent);
    case TGS_WIDGET_SLIDER:   return lv_slider_create(parent);
    case TGS_WIDGET_SWITCH:   return lv_switch_create(parent);
    case TGS_WIDGET_RADIO:
        /* LVGL has no dedicated radio: a checkbox with a round indicator. */
        obj = lv_checkbox_create(parent);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        return obj;
    case TGS_WIDGET_PROGRESS:
        obj = lv_bar_create(parent);
        lv_bar_set_range(obj, 0, 100);
        lv_bar_set_value(obj, 0, LV_ANIM_OFF);
        return obj;
    case TGS_WIDGET_LIST:
        /* lv_list is deprecated in the vendored LVGL 9.6 but still the LIST mapping. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        return lv_list_create(parent);
#pragma GCC diagnostic pop
    case TGS_WIDGET_TABLE:    return lv_table_create(parent);
    case TGS_WIDGET_MENU:
        /* lv_menu is deprecated in the vendored LVGL 9.6 but still the MENU mapping. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        return lv_menu_create(parent);
#pragma GCC diagnostic pop
    case TGS_WIDGET_TAB:      return lv_tabview_create(parent);
    case TGS_WIDGET_DROPDOWN: return lv_dropdown_create(parent);
    case TGS_WIDGET_IMAGE:    return lv_image_create(parent);
    case TGS_WIDGET_TIMEPICK: return lv_roller_create(parent);
    case TGS_WIDGET_DATEPICK: return lv_calendar_create(parent);
    case TGS_WIDGET_VLAYOUT:
        obj = create_container(parent);
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        return obj;
    case TGS_WIDGET_HLAYOUT:
        obj = create_container(parent);
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        return obj;
    case TGS_WIDGET_GLAYOUT:
        obj = create_container(parent);
        lv_obj_set_grid_dsc_array(obj, grid_col_dsc, grid_row_dsc);
        return obj;
    case TGS_WIDGET_SCROLL:
        obj = create_container(parent);
        lv_obj_set_scrollable(obj, true);
        return obj;
    default:
        return lv_obj_create(parent);
    }
}
/* ---- Display Buffer ----
 *
 * Buffer contract (shared with output.h / output_present()):
 *   THIS backend owns the single LVGL draw buffer and publishes it through
 *   g_display->buffer (with width/height/bpp/stride describing its ARGB8888
 *   layout). The output backend never supplies or draws into a buffer of its
 *   own — output_present() is the only place that pushes g_display->buffer to
 *   the real surface (SDL texture, mmap'd /dev/fb0, ...). */

static void display_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    (void)area; (void)px;
    /* The draw buffer IS the published buffer — output_present() reads it
     * directly, so there is nothing to copy here. Must still report
     * completion, otherwise LVGL stalls the refresh. */
    lv_display_flush_ready(d);
}

void lvgl_backend_set_display(tgs_display *display)
{
    g_display = display;
}

/* ---- Backend Interface Implementation ---- */

static int backend_init(int width, int height)
{
    lv_init();

    /* Create display */
    disp = lv_display_create(width, height);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);

    if (!g_display) return -1;

    /* Single allocation, owned here (see the buffer contract above). */
    size_t buf_size = (size_t)width * (size_t)height * 4;
    draw_buf = (uint8_t *)malloc(buf_size);
    if (!draw_buf) return -1;
    g_display->width  = width;
    g_display->height = height;
    g_display->bpp    = 32;
    g_display->stride = width * 4; /* ARGB8888, tightly packed */
    g_display->buffer = draw_buf;

    lv_display_set_buffers(disp, draw_buf, NULL, (uint32_t)buf_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, display_flush_cb);

    /* Mouse input device */
    mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    /* Keyboard input device. Until a window's ring is installed the group is
     * an empty placeholder: LVGL only delivers keys to the focused object of
     * the group the indev points at, which is exactly the active window. */
    kb_group = lv_group_create();
    kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb_indev, kb_read_cb);
    lv_indev_set_group(kb_indev, kb_group);

    lv_style_init(&g_focus_style);
    lv_style_set_outline_width(&g_focus_style, 2);
    lv_style_set_outline_color(&g_focus_style, lv_color_hex(0x4C9AFF));
    lv_style_set_outline_pad(&g_focus_style, 1);
    g_focus_style_ready = 1;

    memset(g_windows, 0, sizeof(g_windows));
    g_focus_vis = NULL;
    g_nav_key_cb = NULL;
    g_nav_key_ud = NULL;

    mouse_x = mouse_y = mouse_pressed = 0;
    mouse_qhead = mouse_qtail = mouse_qedged = 0;
    key_qhead = key_qtail = 0;
    key_ev_code = key_ev_mods = 0;
    g_event_cb = NULL;
    g_event_user_data = NULL;

    return 0;
}

static void backend_tick(uint32_t ms)
{
    lv_tick_inc(ms);
    lv_timer_handler();
}

static void backend_deinit(void)
{
    /* Frees the buffer that output backends may still point at, so clear the
     * published pointer too. */
    if (draw_buf) { free(draw_buf); draw_buf = NULL; }
    if (g_display) g_display->buffer = NULL;
}

static void backend_render(void)
{
    lv_timer_handler();
}

static void backend_set_size(int w, int h)
{
    uint8_t *nb;

    if (!disp || w < 1 || h < 1) return;
    if (g_display && w == g_display->width && h == g_display->height) return;

    /* The draw buffer is published as the framebuffer, so it must be replaced
     * together with the resolution — LVGL would otherwise paint a w x h screen
     * through a buffer sized for the old one. */
    nb = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!nb) return;

    lv_display_set_buffers(disp, nb, NULL, (uint32_t)((size_t)w * (size_t)h * 4u),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_resolution(disp, w, h);

    free(draw_buf);
    draw_buf = nb;

    if (g_display) {
        g_display->width  = w;
        g_display->height = h;
        g_display->stride = w * 4;
        g_display->buffer = nb;
    }
}

/* ---- Windows ---- */

static window_slot *window_slot_find(lv_obj_t *root)
{
    int i;

    for (i = 0; i < BACKEND_MAX_WINDOWS; i++) {
        if (g_windows[i].root == root) return &g_windows[i];
    }
    return NULL;
}

static void *backend_create_window(tgs_window_type type, const char *title)
{
    int i;

    (void)type;
    (void)title;

    for (i = 0; i < BACKEND_MAX_WINDOWS; i++) {
        lv_obj_t *root;

        if (g_windows[i].root) continue;

        /* §I.6: every window owns a distinct root on the screen, which is
         * what makes per-window rings and focus observable. */
        root = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(root, 0, 0);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        /* The root is the coordinate origin the compositor's pixel space maps
         * 1:1 onto: children's rects (set_widget_rect) are absolute screen
         * pixels. The default theme's card style insets the content area by
         * PAD_DEF plus a border, which would shift every child by that inset
         * and skew both rendering and hit-testing — zero both (D4). */
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x222222), 0);
        /* Default theme text is near-black, which is invisible on the dark
         * window background used above — force a light default so labels
         * inherit it. */
        lv_obj_set_style_text_color(root, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_scrollable(root, false);
        /* A root is structure, not a widget: clicking the background must not
         * move focus off the widget the user last had (§A.1). */
        lv_obj_set_click_focusable(root, false);

        g_windows[i].root = root;
        g_windows[i].group = lv_group_create();
        lv_group_set_wrap(g_windows[i].group, true); /* §C.1: every ring wraps */
        return (void *)root;
    }
    return NULL;
}

static void backend_destroy_window(void *handle)
{
    lv_obj_t *root = (lv_obj_t *)handle;
    window_slot *slot = root ? window_slot_find(root) : NULL;

    if (!slot) return;
    if (kb_group == slot->group) {
        kb_group = NULL;
        lv_indev_set_group(kb_indev, NULL);
    }
    lv_obj_delete(root);          /* detaches every widget from the group */
    lv_group_delete(slot->group); /* also unplugs the group from any indev */
    slot->root = NULL;
    slot->group = NULL;
}

/* ---- Widgets ---- */

static void *backend_create_widget(void *parent, tgs_widget_type type)
{
    lv_obj_t *p = parent ? (lv_obj_t *)parent : lv_screen_active();
    lv_obj_t *obj = create_lvgl_widget(type, p);
    if (!obj) return NULL;

    /* Store widget type in user_data for event mapping */
    lv_obj_set_user_data(obj, (void *)(intptr_t)type);

    /* Grid containers place each new child into the next free cell. */
    if (lv_obj_get_style_layout(p, LV_PART_MAIN) == LV_LAYOUT_GRID)
        grid_place_child(p, obj);

    /* Ring membership is installed by the compositor (set_window_ring), which
     * owns focusability and order — a widget is never silently enrolled. */

    /* Register generic event handler */
    lv_obj_add_event_cb(obj, lvgl_event_handler, LV_EVENT_ALL, NULL);

    return (void *)obj;
}

static void backend_set_widget_rect(void *handle, int x, int y, int w, int h)
{
    if (!handle) return;
    lv_obj_t *obj = (lv_obj_t *)handle;
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
}

static void backend_set_widget_content(void *handle, const char *text)
{
    if (!handle || !text) return;
    lv_obj_t *obj = (lv_obj_t *)handle;
    intptr_t type = (intptr_t)lv_obj_get_user_data(obj);

    switch (type) {
    case TGS_WIDGET_BUTTON: {
        /* LVGL buttons need a child label for text */
        lv_obj_t *label = lv_obj_get_child(obj, 0);
        if (!label) {
            label = lv_label_create(obj);
            lv_obj_center(label);
        }
        lv_label_set_text(label, text);
        break;
    }
    case TGS_WIDGET_LABEL:
        lv_label_set_text(obj, text);
        break;
    case TGS_WIDGET_INPUT:
        lv_textarea_set_text(obj, text);
        break;
    case TGS_WIDGET_RADIO:
        lv_checkbox_set_text(obj, text);
        break;
    case TGS_WIDGET_PROGRESS:
        lv_bar_set_value(obj, atoi(text), LV_ANIM_OFF);
        break;
    case TGS_WIDGET_LIST:
        /* Each call appends one row; empty content means "no row". */
        if (text[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            lv_list_add_button(obj, NULL, text);
#pragma GCC diagnostic pop
        }
        break;
    case TGS_WIDGET_TABLE:
        lv_table_set_cell_value(obj, 0, 0, text);
        break;
    case TGS_WIDGET_TAB:
        /* Each call adds one tab. */
        if (text[0]) lv_tabview_add_tab(obj, text);
        break;
    case TGS_WIDGET_DROPDOWN:
        /* Options are newline separated. */
        lv_dropdown_set_options(obj, text);
        break;
    case TGS_WIDGET_TIMEPICK:
        lv_roller_set_options(obj, text, LV_ROLLER_MODE_NORMAL);
        break;
    case TGS_WIDGET_DATEPICK: {
        int y, m, d;
        if (sscanf(text, "%d-%d-%d", &y, &m, &d) == 3) {
            lv_calendar_set_today_date(obj, (uint32_t)y, (uint32_t)m, (uint32_t)d);
            lv_calendar_set_month_shown(obj, (uint32_t)y, (uint32_t)m);
        }
        break;
    }
    default:
        /* Containers carry no content; MENU and IMAGE have no string mapping. */
        break;
    }
}
static void backend_insert_widget_text(void *handle, const char *text)
{
    if (!handle || !text) return;
    lv_obj_t *obj = (lv_obj_t *)handle;
    intptr_t type = (intptr_t)lv_obj_get_user_data(obj);

    if (type == TGS_WIDGET_INPUT) {
        lv_textarea_add_text(obj, text);
    } else {
        /* Fallback: replace content */
        backend_set_widget_content(handle, text);
    }
}

static void backend_set_widget_style(void *handle, tgs_style_prop prop, int32_t value)
{
    if (!handle) return;
    lv_obj_t *obj = (lv_obj_t *)handle;

    switch (prop) {
    case TGS_STYLE_BG_COLOR:
        lv_obj_set_style_bg_color(obj, lv_color_hex((uint32_t)value), 0);
        break;
    case TGS_STYLE_FG_COLOR:
        lv_obj_set_style_text_color(obj, lv_color_hex((uint32_t)value), 0);
        break;
    case TGS_STYLE_RADIUS:
        lv_obj_set_style_radius(obj, value, 0);
        break;
    case TGS_STYLE_BORDER_WIDTH:
        lv_obj_set_style_border_width(obj, value, 0);
        break;
    case TGS_STYLE_BORDER_COLOR:
        lv_obj_set_style_border_color(obj, lv_color_hex((uint32_t)value), 0);
        break;
    case TGS_STYLE_SHADOW_WIDTH:
        lv_obj_set_style_shadow_width(obj, value, 0);
        break;
    case TGS_STYLE_SHADOW_COLOR:
        lv_obj_set_style_shadow_color(obj, lv_color_hex((uint32_t)value), 0);
        break;
    case TGS_STYLE_OPACITY:
        lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
        break;
    case TGS_STYLE_FONT_SIZE:
        /* LVGL font size is fixed at compile time; skip */
        break;
    }
}

static void backend_set_widget_layout(void *handle, tgs_layout_type layout)
{
    if (!handle) return;
    lv_obj_t *obj = (lv_obj_t *)handle;

    switch (layout) {
    case TGS_LAYOUT_FLEX_ROW:
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        break;
    case TGS_LAYOUT_FLEX_COL:
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
        break;
    case TGS_LAYOUT_GRID:
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        break;
    case TGS_LAYOUT_NONE:
    default:
        break;
    }
}

static void backend_destroy_widget(void *handle)
{
    if (!handle) return;
    lv_obj_delete((lv_obj_t *)handle);
}

/* ---- Focus & navigation (§I.1, §D.3) ---- */

/* Install the compositor-computed ring: group membership in its order. */
static void backend_set_window_ring(void *window, void **widgets, int count)
{
    window_slot *slot = window ? window_slot_find((lv_obj_t *)window) : NULL;
    lv_obj_t *prev;
    int i;

    if (!slot || !slot->group) return;

    /* Keep the composed focus across the rebuild: the compositor's registry
     * does not change here, so losing it would drop the outline and let LVGL
     * re-report a focus gain the app already knows about. */
    prev = lv_group_get_focused(slot->group);

    lv_group_remove_all_objs(slot->group);
    for (i = 0; i < count; i++) {
        if (widgets[i]) lv_group_add_obj(slot->group, (lv_obj_t *)widgets[i]);
    }
    if (prev && lv_obj_get_group(prev) == slot->group)
        lv_group_focus_obj(prev);
}

/* Hand the keyboard to a window: LVGL only routes keys to the focused object
 * of the group the keypad indev points at (§I.2). Background windows need no
 * focus freeze — the indev cannot reach their groups at all. */
static void backend_set_active_window(void *window)
{
    window_slot *slot = window ? window_slot_find((lv_obj_t *)window) : NULL;

    kb_group = slot ? slot->group : NULL;
    lv_indev_set_group(kb_indev, kb_group);
}

static void backend_set_focus(void *handle)
{
    lv_obj_t *obj = (lv_obj_t *)handle;

    if (!obj) {
        /* "No focus": drop the visuals. LVGL keeps its group cursor, which
         * the compositor no longer consults. */
        if (g_focus_vis) {
            lv_obj_remove_state(g_focus_vis, LV_STATE_FOCUSED);
            lv_obj_invalidate(g_focus_vis);
            g_focus_vis = NULL;
        }
        return;
    }

    g_focus_vis = obj;
    lv_group_focus_obj(obj);
    lv_obj_scroll_to_view(obj, LV_ANIM_OFF);
}

static void obj_center(lv_obj_t *obj, int *cx, int *cy)
{
    lv_area_t a;

    lv_obj_get_coords(obj, &a);
    *cx = (a.x1 + a.x2) / 2;
    *cy = (a.y1 + a.y2) / 2;
}

/* Geometric spatial search among the candidates of the current ring (§D.3
 * rule 3). Returns the best candidate in `dir`, NULL when there is none — the
 * compositor-visible signal for rule 4 (residual forward). */
static void *backend_focus_dir(void *from, void **candidates, int count,
                              tgs_nav_dir dir)
{
    lv_obj_t *src = (lv_obj_t *)from;
    int sx, sy, best = -1, best_score = 0, i;

    if (!src) return NULL;
    obj_center(src, &sx, &sy);

    for (i = 0; i < count; i++) {
        lv_obj_t *c = (lv_obj_t *)candidates[i];
        int cx, cy, dx, dy, primary, perp, score;

        if (!c || c == src) continue;
        obj_center(c, &cx, &cy);
        dx = cx - sx;
        dy = cy - sy;
        if (dir == TGS_NAV_LEFT  && dx >= 0) continue;
        if (dir == TGS_NAV_RIGHT && dx <= 0) continue;
        if (dir == TGS_NAV_UP    && dy >= 0) continue;
        if (dir == TGS_NAV_DOWN  && dy <= 0) continue;

        if (dir == TGS_NAV_LEFT || dir == TGS_NAV_RIGHT) {
            primary = dx < 0 ? -dx : dx;
            perp    = dy < 0 ? -dy : dy;
        } else {
            primary = dy < 0 ? -dy : dy;
            perp    = dx < 0 ? -dx : dx;
        }
        score = primary + 2 * perp; /* straight-ahead neighbours win */
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best < 0 ? NULL : candidates[best];
}

static void backend_set_widget_focusable(void *handle, int focusable)
{
    lv_obj_t *obj = (lv_obj_t *)handle;

    if (!obj) return;
    /* LVGL focuses any object that is click-focusable, so a label or progress
     * bar would steal focus on click (§A.3, criterion L15). */
    lv_obj_set_click_focusable(obj, focusable ? true : false);
    if (focusable)
        lv_obj_add_style(obj, &g_focus_style, LV_STATE_FOCUSED);
    else
        lv_obj_remove_style(obj, &g_focus_style, LV_STATE_FOCUSED);
}

static void backend_set_nav_key_cb(tgs_nav_key_cb cb, void *user_data)
{
    g_nav_key_cb = cb;
    g_nav_key_ud = user_data;
}

/* ---- Events ---- */

static void backend_set_event_callback(tgs_event_cb cb, void *user_data)
{
    g_event_cb = cb;
    g_event_user_data = user_data;
}

static void lvgl_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    if (!obj) return;

    intptr_t type = (intptr_t)lv_obj_get_user_data(obj);
    tgs_event_type etype;
    const char *event_data = NULL;
    static char value_buf[256];
    static char key_buf[32];
    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        etype = TGS_EVENT_CLICK;
        break;
    case LV_EVENT_VALUE_CHANGED:
        etype = TGS_EVENT_VALUE_CHANGED;
        if (type == TGS_WIDGET_INPUT) {
            const char *t = lv_textarea_get_text(obj);
            if (t) {
                strncpy(value_buf, t, sizeof(value_buf) - 1);
                value_buf[sizeof(value_buf) - 1] = '\0';
                event_data = value_buf;
            }
        }
        break;
    case LV_EVENT_FOCUSED:
        /* Focus must be visible even inside a scroll viewport (§I.3). */
        lv_obj_scroll_to_view(obj, LV_ANIM_OFF);
        etype = TGS_EVENT_FOCUS;
        break;
    case LV_EVENT_DEFOCUSED:
        etype = TGS_EVENT_BLUR;
        break;
    case LV_EVENT_KEY:
        /* Forward the key to the app as "key;mods" (window_manager.c parses
         * exactly that and routes it to the IME or the app). The code reported
         * is the TGS code, not LVGL's — key_ev_code holds it because LVGL
         * rewrites the key on the way through (e.g. Tab+SHIFT -> LV_KEY_PREV);
         * key_ev_mods keeps the modifier mask LVGL drops. */
        etype = TGS_EVENT_KEY;
        snprintf(key_buf, sizeof(key_buf), "%d;%d", key_ev_code, key_ev_mods);
        event_data = key_buf;
        break;
    default:
        return; /* ignore unhandled events */
    }

    if (g_event_cb)
        g_event_cb((void *)obj, etype, event_data, g_event_user_data);
}

/* ---- Input Injection ---- */

static void backend_inject_mouse(int x, int y, int button, int pressed)
{
    (void)button;
    mouse_x = x;
    mouse_y = y;
    mouse_pressed = pressed ? 1 : 0;

    /* Queue edges only (a drag's repeated pressed=1 motion is not an edge), so
     * LVGL reads the press before the release that follows in the same batch. */
    if (mouse_pressed != mouse_qedged) {
        int next = (mouse_qtail + 1) % INDEV_QUEUE_LEN;
        if (next != mouse_qhead) { /* queue full: drop, retry on the next event */
            mouse_queue[mouse_qtail] = (uint8_t)mouse_pressed;
            mouse_qtail = next;
            mouse_qedged = mouse_pressed;
        }
    }
}

static void backend_inject_key(int key, int mods, int pressed)
{
    int next;

    /* Navigation precedence (§D.3): the compositor decides what this key edge
     * means for the focused widget before LVGL can interpret it. */
    if (g_nav_key_cb) {
        tgs_nav_key_action action = g_nav_key_cb(key, mods, pressed, g_nav_key_ud);

        if (action == TGS_NAV_CONSUMED) return;
        if (action == TGS_NAV_WIDGET) {
            /* Rule 2 hand-off: straight to the focused widget, bypassing the
             * indev's own Tab/Enter/ESC group handling. The queue path sets
             * key_ev_code/_mods in kb_read_cb; this synchronous delivery skips
             * it, so record the edge here too — LV_EVENT_KEY reads these. */
            if (pressed && kb_group) {
                key_ev_code = key;
                key_ev_mods = mods;
                lv_group_send_data(kb_group, map_tgs_key(key, mods));
            }
            return;
        }
    }

    next = (key_qtail + 1) % INDEV_QUEUE_LEN;
    if (next == key_qhead) return; /* full: a burst longer than any indev reads */

    key_queue[key_qtail].code    = key;
    key_queue[key_qtail].mods    = mods;
    key_queue[key_qtail].pressed = pressed ? 1 : 0;
    key_qtail = next;
}

/* ---- Input Device Read Callbacks ---- */

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int state;

    /* One queued transition per read; the steady state once the queue drains,
     * so a held button still reports PRESSED on every later read. */
    if (mouse_qhead != mouse_qtail) {
        state = mouse_queue[mouse_qhead];
        mouse_qhead = (mouse_qhead + 1) % INDEV_QUEUE_LEN;
    } else {
        state = mouse_pressed;
    }

    data->point.x = (lv_coord_t)mouse_x;
    data->point.y = (lv_coord_t)mouse_y;
    data->state = state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void kb_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (key_qhead != key_qtail) {
        const key_event *ev = &key_queue[key_qhead];
        key_qhead = (key_qhead + 1) % INDEV_QUEUE_LEN;

        data->key = map_tgs_key(ev->code, ev->mods);
        data->state = ev->pressed ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
        if (ev->pressed) {
            key_ev_code = ev->code;
            key_ev_mods = ev->mods;
        }
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* The clipboard, where the platform has one. SDL owns the string it hands back,
 * so this keeps it alive until the next call rather than making every caller
 * free it. */
static const char *backend_clipboard_text(void)
{
#ifdef TGS_USE_SDL
    static char *held;

    if (held) {
        SDL_free(held);
        held = NULL;
    }
    if (!SDL_HasClipboardText()) return NULL;

    held = SDL_GetClipboardText();
    return (held && held[0]) ? held : NULL;
#else
    return NULL;
#endif
}

/* Absolute (screen-space) geometry of a widget: walk the parent chain summing
 * each ancestor's position (a window root sits at 0,0), then read the leaf's
 * resolved size. Flex/grid positions are applied by lv_timer_handler, so this
 * is only accurate after a tick — the compositor flushes geometry then. */
static int backend_widget_geometry(void *handle, int *x, int *y, int *w, int *h)
{
    lv_obj_t *obj;
    lv_obj_t *p;
    int ax = 0, ay = 0;

    if (!handle) return -1;
    obj = (lv_obj_t *)handle;
    p = obj;
    while (p) {
        ax += lv_obj_get_x(p);
        ay += lv_obj_get_y(p);
        p = lv_obj_get_parent(p);
    }
    if (x) *x = ax;
    if (y) *y = ay;
    if (w) *w = lv_obj_get_width(obj);
    if (h) *h = lv_obj_get_height(obj);
    return 0;
}


static tgs_backend lvgl_backend = {
    .user_data            = NULL,
    .init                 = backend_init,
    .tick                 = backend_tick,
    .deinit               = backend_deinit,
    .render               = backend_render,
    .set_size             = backend_set_size,
    .create_window        = backend_create_window,
    .destroy_window       = backend_destroy_window,
    .create_widget        = backend_create_widget,
    .set_widget_rect      = backend_set_widget_rect,
    .set_widget_content   = backend_set_widget_content,
    .insert_widget_text   = backend_insert_widget_text,
    .set_widget_style     = backend_set_widget_style,
    .set_widget_layout    = backend_set_widget_layout,
    .destroy_widget       = backend_destroy_widget,
    .set_event_callback   = backend_set_event_callback,
    .inject_mouse         = backend_inject_mouse,
    .inject_key           = backend_inject_key,
    .set_window_ring      = backend_set_window_ring,
    .set_active_window    = backend_set_active_window,
    .set_focus            = backend_set_focus,
    .focus_dir            = backend_focus_dir,
    .set_widget_focusable = backend_set_widget_focusable,
    .set_nav_key_cb       = backend_set_nav_key_cb,
    .clipboard_text       = backend_clipboard_text,
    .widget_geometry      = backend_widget_geometry,
};

void lvgl_backend_register(void)
{
    tgs_backend_register(&lvgl_backend);
}
