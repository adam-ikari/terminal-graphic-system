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
#define TGS_CMD_REJECT  3   /* [rejected_cap] — compositor → app, sent instead of READY */

/* Window */
#define TGS_CMD_WIN_CREATE   16
#define TGS_CMD_WIN_DESTROY  17

/* Widget */
#define TGS_CMD_WGT_CREATE   32
#define TGS_CMD_WGT_UPDATE   33
#define TGS_CMD_WGT_STYLE    34
#define TGS_CMD_WGT_DESTROY  35
#define TGS_CMD_EVT_BIND     36
#define TGS_CMD_WGT_LAYOUT   37  /* [widget_id, layout_type] — optional runtime relayout of a container */

/* Navigation */
#define TGS_CMD_SET_FOCUS    38  /* [window_id, widget_id] — widget_id 0 clears; app → compositor */
/* 39 reserved: GET_FOCUS (Layer 2 reply = NTF_FOCUS with reason NONE) */
#define TGS_CMD_WGT_ATTR     40  /* [widget_id, attr, value] — attr: tgs_widget_attr */
/* 41 reserved: NAV_BIND (Layer 2 per-app key binding override) */

/* Notify */
#define TGS_CMD_NTF_RESIZE   64
#define TGS_CMD_NTF_FOCUS    65  /* [win_id, widget_id, focused, reason] — reason: tgs_focus_reason */
#define TGS_CMD_NTF_DESTROY  66
#define TGS_CMD_NTF_STATE    67  /* [win_id, state] — state: tgs_window_state */
/* 68 reserved: NTF_FOCUS_PRE (Layer 2 bounded veto window) */
#define TGS_CMD_NTF_GEOMETRY 69  /* [widget_id, x, y, w, h] — widget's real
                                  * screen/absolute geometry after layout
                                  * (compositor → app) */

/* Events */
#define TGS_CMD_EVT_CLICK    80
#define TGS_CMD_EVT_KEY      81
#define TGS_CMD_EVT_VALUE    82
#define TGS_CMD_EVT_FOCUS    83  /* retired: MUST NOT be emitted; NTF_FOCUS (65) is the focus channel */

/* IME */
#define TGS_CMD_IME_PREEDIT  96
#define TGS_CMD_IME_COMMIT   97
#define TGS_CMD_IME_CANDIDATES 98 /* [win_id, widget_id, count, c1, c2, ...] */
#define TGS_CMD_IME_SELECT   99   /* [win_id, widget_id, index] */

#define TGS_CMD_IME_CANCEL   100 /* [win_id, widget_id] — compositor → IME app, drop active composition */

/* Focus change reason — `reason` argument of NTF_FOCUS (65) */
typedef enum {
    TGS_REASON_NONE = 0,        /* unspecified / cold query reply */
    TGS_REASON_TAB,             /* Tab traversal */
    TGS_REASON_SHIFT_TAB,       /* Shift+Tab traversal */
    TGS_REASON_ARROW,           /* spatial arrow navigation */
    TGS_REASON_POINTER,         /* pointer click on the widget */
    TGS_REASON_PROGRAMMATIC,    /* SET_FOCUS from the app */
    TGS_REASON_INIT,            /* initial focus of a window */
    TGS_REASON_WINDOW_ACTIVATE, /* window activated / raised / unhidden */
    TGS_REASON_WINDOW_RESTORE,  /* focus restored after a window was hidden or destroyed */
    TGS_REASON_SCOPE_RESTORE,   /* focus restored inside a scope (dialog/modal closed) */
    TGS_REASON_DESTROYED,       /* the focused widget was destroyed */
    TGS_REASON_HIDDEN,          /* focus lost because the window was hidden/minimized */
} tgs_focus_reason;

/* Behavioural widget attribute — `attr` argument of WGT_ATTR (40).
 * FOCUSABLE / NAV_ARROWS: -1 = type default, 0 = no, 1 = yes.
 * NAV_TAB: 0/1. FOCUS_INDEX: -1 = auto, >= 0 = ring position.
 * FOCUS_SCOPE (containers only): 0 = none, 1 = GROUP (single tab stop),
 * 2 = TRAP (modal ring). */
typedef enum {
    TGS_ATTR_FOCUSABLE = 0,
    TGS_ATTR_NAV_ARROWS,
    TGS_ATTR_NAV_TAB,
    TGS_ATTR_FOCUS_INDEX,
    TGS_ATTR_FOCUS_SCOPE,
} tgs_widget_attr;

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

/* Window activation state — `state` argument of NTF_STATE (67) */
typedef enum {
    TGS_WINDOW_STATE_INACTIVE = 0,
    TGS_WINDOW_STATE_ACTIVE = 1,
} tgs_window_state;

#endif /* TGS_PROTOCOL_H */
