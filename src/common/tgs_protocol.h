/*
 * TGS Protocol Common Definitions
 * All type definitions: enums, stream IDs, command IDs, protocol constants.
 * C99, no external dependencies.
 */
#ifndef TGS_PROTOCOL_H
#define TGS_PROTOCOL_H

/* Protocol version and capabilities */
#define TGS_PROTOCOL_VERSION "1.0"
#define TGS_CAPS_LAYER0 \
    "layout.tiled,widget.button,widget.label,widget.input," \
    "event.click,event.key,event.resize,event.focus"

/* Stream IDs */
#define TGS_STREAM_HANDSHAKE  0
#define TGS_STREAM_COMMAND    1
#define TGS_STREAM_RESOURCE   2
#define TGS_STREAM_FRAMEBUFFER 3
#define TGS_STREAM_EVENT      4

/* Command IDs */
/* Handshake */
#define TGS_CMD_HELLO   1
#define TGS_CMD_READY   2

/* Window */
#define TGS_CMD_WIN_CREATE   16
#define TGS_CMD_WIN_DESTROY  17

/* Widget */
#define TGS_CMD_WGT_CREATE   32
#define TGS_CMD_WGT_UPDATE   33
#define TGS_CMD_WGT_STYLE    34
#define TGS_CMD_WGT_DESTROY  35
#define TGS_CMD_EVT_BIND     36

/* Notify */
#define TGS_CMD_NTF_RESIZE   64
#define TGS_CMD_NTF_FOCUS    65
#define TGS_CMD_NTF_DESTROY  66

/* Events */
#define TGS_CMD_EVT_CLICK    80
#define TGS_CMD_EVT_KEY      81
#define TGS_CMD_EVT_VALUE    82
#define TGS_CMD_EVT_FOCUS    83

/* IME */
#define TGS_CMD_IME_PREEDIT  96
#define TGS_CMD_IME_COMMIT   97

/* Window types */
typedef enum {
    TGS_WINDOW_NORMAL = 0,
    TGS_WINDOW_DIALOG,
    TGS_WINDOW_FULLSCREEN,
    TGS_WINDOW_TOOL,
} tgs_window_type;

/* Widget types (>= 20 per FR-5.2.1) */
typedef enum {
    TGS_WIDGET_BUTTON = 0,
    TGS_WIDGET_LABEL,
    TGS_WIDGET_INPUT,
    TGS_WIDGET_CHECKBOX,
    TGS_WIDGET_RADIO,
    TGS_WIDGET_SLIDER,
    TGS_WIDGET_PROGRESS,
    TGS_WIDGET_SWITCH,
    TGS_WIDGET_VLAYOUT,
    TGS_WIDGET_HLAYOUT,
    TGS_WIDGET_GLAYOUT,
    TGS_WIDGET_SCROLL,
    TGS_WIDGET_LIST,
    TGS_WIDGET_TABLE,
    TGS_WIDGET_MENU,
    TGS_WIDGET_TAB,
    TGS_WIDGET_DROPDOWN,
    TGS_WIDGET_IMAGE,
    TGS_WIDGET_TIMEPICK,
    TGS_WIDGET_DATEPICK,
    TGS_WIDGET_COUNT,
} tgs_widget_type;

/* Event types */
typedef enum {
    TGS_EVENT_CLICK = 0,
    TGS_EVENT_KEY,
    TGS_EVENT_FOCUS,
    TGS_EVENT_BLUR,
    TGS_EVENT_VALUE_CHANGED,
    TGS_EVENT_IME_PREEDIT,
    TGS_EVENT_IME_COMMIT,
} tgs_event_type;

/* Style properties */
typedef enum {
    TGS_STYLE_BG_COLOR = 0,
    TGS_STYLE_FG_COLOR,
    TGS_STYLE_RADIUS,
    TGS_STYLE_BORDER_WIDTH,
    TGS_STYLE_BORDER_COLOR,
    TGS_STYLE_SHADOW_WIDTH,
    TGS_STYLE_SHADOW_COLOR,
    TGS_STYLE_FONT_SIZE,
    TGS_STYLE_OPACITY,
} tgs_style_prop;

/* Layout types */
typedef enum {
    TGS_LAYOUT_NONE = 0,
    TGS_LAYOUT_FLEX_ROW,
    TGS_LAYOUT_FLEX_COL,
    TGS_LAYOUT_GRID,
} tgs_layout_type;

#endif /* TGS_PROTOCOL_H */
