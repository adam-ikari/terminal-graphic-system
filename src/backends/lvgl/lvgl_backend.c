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

#include "output.h"

/* Display buffer pointer — set via lvgl_backend_set_display() */
static tgs_display *g_display;

/* ---- Static State ---- */

static lv_display_t *disp;
static uint8_t      *draw_buf;
static lv_indev_t   *mouse_indev;
static lv_indev_t   *kb_indev;
static lv_group_t   *kb_group;

static int mouse_x, mouse_y, mouse_pressed;
static int last_key, key_pressed, key_queued;

static tgs_event_cb g_event_cb;
static void        *g_event_user_data;

/* ---- Forward Declarations ---- */

static void lvgl_event_handler(lv_event_t *e);
static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void kb_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

/* ---- Internal Helpers ---- */

/* Map raw TGS key code → LVGL key */
static uint32_t map_tgs_key(int key)
{
    switch (key) {
    case 1:  return LV_KEY_UP;
    case 2:  return LV_KEY_DOWN;
    case 3:  return LV_KEY_RIGHT;
    case 4:  return LV_KEY_LEFT;
    case 8:  return LV_KEY_BACKSPACE;
    case 9:  return LV_KEY_NEXT; /* TAB */
    case 13: return LV_KEY_ENTER;
    case 27: return LV_KEY_ESC;
    case 127: return LV_KEY_DEL;
    default: return (uint32_t)key;
    }
}

/* ---- Widget Creation ---- */

static lv_obj_t *create_lvgl_widget(tgs_widget_type type, lv_obj_t *parent)
{
    switch (type) {
    case TGS_WIDGET_BUTTON:   return lv_button_create(parent);
    case TGS_WIDGET_LABEL:    return lv_label_create(parent);
    case TGS_WIDGET_INPUT:    return lv_textarea_create(parent);
    case TGS_WIDGET_CHECKBOX: return lv_checkbox_create(parent);
    case TGS_WIDGET_SLIDER:   return lv_slider_create(parent);
    case TGS_WIDGET_SWITCH:   return lv_switch_create(parent);
    /* Layout containers and unknown types → generic object */
    case TGS_WIDGET_VLAYOUT:
    case TGS_WIDGET_HLAYOUT:
    case TGS_WIDGET_GLAYOUT:
    case TGS_WIDGET_SCROLL:
    default:                  return lv_obj_create(parent);
    }
}
/* ---- Display Buffer ---- */

static void display_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    (void)d; (void)area; (void)px;
    /* Buffer is shared with display — output_present() reads it directly */
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

    /* Use display buffer (set via lvgl_backend_set_display) */
    if (!g_display) return -1;
    size_t buf_size = (size_t)width * (size_t)height * 4;
    draw_buf = (uint8_t *)malloc(buf_size);
    if (!draw_buf) return -1;
    g_display->buffer = draw_buf;

    lv_display_set_buffers(disp, draw_buf, NULL, (uint32_t)buf_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, display_flush_cb);

    /* Mouse input device */
    mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    /* Keyboard input device + group */
    kb_group = lv_group_create();
    kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb_indev, kb_read_cb);
    lv_indev_set_group(kb_indev, kb_group);

    mouse_x = mouse_y = mouse_pressed = 0;
    last_key = 0;
    key_pressed = key_queued = 0;
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
    if (draw_buf) { free(draw_buf); draw_buf = NULL; }
}

static void backend_render(void)
{
    lv_timer_handler();
}

static void backend_set_size(int w, int h)
{
    if (disp)
        lv_display_set_resolution(disp, w, h);
}

/* ---- Windows ---- */

static void *backend_create_window(tgs_window_type type, const char *title)
{
    (void)type;
    (void)title;
    /* The root LVGL screen is the window */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x222222), 0);
    return (void *)scr;
}

static void backend_destroy_window(void *handle)
{
    (void)handle;
    /* Cannot delete the active screen */
}

/* ---- Widgets ---- */

static void *backend_create_widget(void *parent, tgs_widget_type type)
{
    lv_obj_t *p = parent ? (lv_obj_t *)parent : lv_screen_active();
    lv_obj_t *obj = create_lvgl_widget(type, p);
    if (!obj) return NULL;

    /* Store widget type in user_data for event mapping */
    lv_obj_set_user_data(obj, (void *)(intptr_t)type);

    /* Add to keyboard group for navigation */
    if (kb_group) lv_group_add_obj(kb_group, obj);

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
    case TGS_WIDGET_CHECKBOX:
        lv_checkbox_set_text(obj, text);
        break;
    default:
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
        etype = TGS_EVENT_FOCUS;
        break;
    case LV_EVENT_DEFOCUSED:
        etype = TGS_EVENT_BLUR;
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
    mouse_x = x;
    mouse_y = y;
    mouse_pressed = pressed;
    (void)button;
}

static void backend_inject_key(int key, int mods, int pressed)
{
    (void)mods;
    last_key = key;
    key_queued = 1;
    key_pressed = pressed;
}

/* ---- Input Device Read Callbacks ---- */

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = (lv_coord_t)mouse_x;
    data->point.y = (lv_coord_t)mouse_y;
    data->state = mouse_pressed ? LV_INDEV_STATE_PRESSED
                                : LV_INDEV_STATE_RELEASED;
}

static void kb_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (key_queued) {
        data->key = map_tgs_key(last_key);
        data->state = LV_INDEV_STATE_PRESSED;
        key_queued = 0;
    } else if (key_pressed) {
        data->key = map_tgs_key(last_key);
        data->state = LV_INDEV_STATE_RELEASED;
        key_pressed = 0;
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ---- Registration ---- */

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
};

void lvgl_backend_register(void)
{
    tgs_backend_register(&lvgl_backend);
}
