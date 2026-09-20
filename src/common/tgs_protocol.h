/*
 * TGS Protocol Common Definitions — first-principles minimal form.
 *
 * TGS is a point-to-point drawing protocol between one program and a
 * renderer. A program owns one canvas; it paints primitives and receives
 * input events. There is NO window management, NO win_id, NO focus model,
 * NO layout engine, NO subscription gate, NO widget catalog: the renderer
 * paints the drawing primitives (TEXT / BOX / GRAPHIC), does hit-testing,
 * and forwards input. Everything interactive (buttons, toggles, sliders,
 * scroll, text buffers, focus visuals) is program-side composition of
 * primitives plus events. IME is a separate program; the renderer only
 * routes keys to it and delivers its committed text back as an event —
 * candidate-window presentation is the outer window manager's job, not
 * this protocol's.
 *
 * C99, no external dependencies.
 */
#ifndef TGS_PROTOCOL_H
#define TGS_PROTOCOL_H

#define TGS_PROTOCOL_VERSION "2.0"
#define TGS_CAPS_LAYER0 \
    "drawing.text,drawing.box,drawing.graphic," \
    "event.click,event.key,event.pointer,event.hover,event.resize"

/* Stream IDs */
#define TGS_STREAM_HANDSHAKE  0
#define TGS_STREAM_COMMAND    1
#define TGS_STREAM_RESOURCE   2
#define TGS_STREAM_FRAMEBUFFER 3
#define TGS_STREAM_EVENT      4

/* Command IDs (program → renderer, stream 1) */
/* Handshake */
#define TGS_CMD_HELLO   1
#define TGS_CMD_READY   2
#define TGS_CMD_REJECT  3   /* [rejected_cap] — renderer → program, instead of READY */

/* Elements — the whole drawing surface is one canvas; parent 0 is the
 * canvas root. No window concept exists. */
#define TGS_CMD_WGT_CREATE   32  /* [id, type, parent, x, y, w, h] — parent 0 = canvas */
#define TGS_CMD_WGT_UPDATE   33  /* [id, value] — replace the element's content */
#define TGS_CMD_WGT_STYLE    34  /* [id, style_prop, value] */
#define TGS_CMD_WGT_DESTROY  35  /* [id] */

/* Element kinds — drawing primitives only (values stable; retired kinds
 * keep their numbers reserved). */
typedef enum {
    /* 0 reserved: BUTTON retired — activation is program policy. A click
     * lands on any element (hit-testing is renderer mechanism); a "button"
     * is a BOX + TEXT child whose CLICK the program consumes. */
    TGS_WIDGET_TEXT = 1,        /* TEXT: static text render */
    /* 2, 3, 5 reserved: INPUT/CHECKBOX/SLIDER retired — text buffers,
     * toggle state, value+drag are program state driven by KEY/CLICK/
     * POINTER events, not renderer kinds */
    TGS_WIDGET_BOX = 8,         /* BOX/GROUP: rectangle; children at
                                 * program-computed rects */
    /* 4, 6, 7, 9, 10, 11, 12-19 reserved: retired kinds (RADIO/SWITCH/
     * PROGRESS/LIST/TABLE/MENU/TAB/DROPDOWN/TIMEPICK/DATEPICK = style or
     * box compositions; SCROLL = the program recomputes child geometry
     * from wheel/arrow events instead of a renderer viewport) */
    TGS_WIDGET_GRAPHIC = 16,    /* GRAPHIC: vector shape (circle, path,
                                 * polygon); shape chosen by STYLE. Pixel
                                 * images attach via RESOURCE (stream 2) */
    TGS_WIDGET_COUNT = 20,
} tgs_widget_type;

/* Style properties — the paint attributes of a drawing primitive. */
typedef enum {
    TGS_STYLE_BG_COLOR = 0,
    TGS_STYLE_FG_COLOR,
    TGS_STYLE_RADIUS,
    TGS_STYLE_BORDER_WIDTH,
    TGS_STYLE_BORDER_COLOR,
    TGS_STYLE_FONT_SIZE,
    TGS_STYLE_OPACITY,
    TGS_STYLE_SHAPE,            /* GRAPHIC: 0=circle 1=rect 2=path(shape data in UPDATE) */
} tgs_style_prop;

/* Notifications (renderer → program, stream 1) */
#define TGS_CMD_NTF_RESIZE   64  /* [w, h] — canvas size changed */
#define TGS_CMD_NTF_DESTROY  66  /* [id] — an element was torn down by the renderer */

/* Input events (renderer → program, stream 4) — delivered to the program
 * which routes them to whichever element it wants. CLICK/HOVER carry the
 * hit-tested element id (hit-testing is renderer mechanism). */
#define TGS_CMD_EVT_KEY      81  /* [key, mods] */
#define TGS_CMD_EVT_CLICK    80  /* [id] — press+release on the same element */
#define TGS_CMD_EVT_HOVER_ENTER 84 /* [id] — pointer entered the element (mouse only) */
#define TGS_CMD_EVT_HOVER_LEAVE 85 /* [id] — pointer left the element */
#define TGS_CMD_EVT_POINTER  86  /* [x, y, phase] — raw pointer coordinate:
                                  * phase 0=down 1=move 2=up. The primitive
                                  * for app-drawn interaction (drag, hover). */

/* IME — a separate program. The renderer routes keys to it and delivers its
 * committed text back as IME_COMMIT. No preedit/candidate traffic crosses
 * this protocol: candidate-window presentation is the outer WM's job. */
#define TGS_CMD_IME_COMMIT   97  /* [text] — IME program → renderer → program */

typedef enum {
    TGS_EVENT_KEY = 0,
    TGS_EVENT_CLICK,
    TGS_EVENT_HOVER_ENTER,
    TGS_EVENT_HOVER_LEAVE,
    TGS_EVENT_POINTER,
    TGS_EVENT_IME_COMMIT,
} tgs_event_type;

#endif /* TGS_PROTOCOL_H */
