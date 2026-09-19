# TGS Layer 0 Protocol Specification

**Version:** 1.0
**Status:** Draft

## 1. Overview

TGS Layer 0 is a single-stream multiplexed, APC-framed, text-based protocol between a terminal application (app) and a terminal compositor (compositor). All communication occurs over a single PTY using Application Program Command (APC) escape sequences.

The protocol carries window management, widget tree manipulation, input events, and IME state.

## 2. Wire Format

### 2.1 APC Frame

Every message is wrapped in an APC (Application Program Command) sequence:

```
ESC _ <payload> ESC \
```

- `ESC` = `0x1B`
- `_` = `0x5F` (APC introducer)
- `\` = `0x5C` (String Terminator, ST)

The payload is a UTF-8 string in the format described in §2.2.

### 2.2 Payload Format

```
TGS;<stream_id>;<frame_id>;<command>;<arg1>;<arg2>;...
```

| Field       | Type  | Description                                    |
|-------------|-------|------------------------------------------------|
| `TGS`       | fixed | Magic header, always literal `TGS`             |
| `stream_id` | int   | Logical stream (§3)                            |
| `frame_id`  | int   | Monotonic counter per stream, wrapping at 2^31 |
| `command`   | int   | Command ID (§4)                                |
| `arg1..N`   | str   | Semicolon-delimited arguments, command-specific|

Arguments are UTF-8 strings. Empty arguments are represented as empty strings between semicolons. Maximum 16 arguments per frame, each ≤ 128 bytes.

## 3. Streams

| ID | Name         | Direction        | Purpose                          |
|----|--------------|------------------|----------------------------------|
| 0  | HANDSHAKE    | bidirectional    | Connection establishment         |
| 1  | COMMAND      | app → compositor | Widget tree commands             |
| 2  | RESOURCE     | app → compositor | Image/font resource transfer     |
| 3  | FRAMEBUFFER  | app → compositor | Direct framebuffer updates       |
| 4  | EVENT        | compositor → app | Input and lifecycle events       |

Each stream has its own `frame_id` counter. Frame IDs start at 0 and increment per frame sent on that stream.

## 4. Commands

### 4.1 Handshake (stream 0)

| ID | Name  | Direction        | Args                          |
|----|-------|------------------|-------------------------------|
| 1  | HELLO | app → compositor | `version`, `caps`             |
| 2  | READY | compositor → app | `version`, `caps`             |
| 3  | REJECT | compositor → app | `rejected_cap`                |

- `version`: Protocol version string (e.g. `"1.0"`)
- `caps`: Comma-separated capability tokens (e.g. `"layout.tiled,widget.button"`)
- `rejected_cap`: Capability token the compositor cannot honour. Present only in `REJECT`.

### 4.2 Window (stream 1)

| ID | Name        | Direction     | Args                                  |
|----|-------------|---------------|---------------------------------------|
| 16 | WIN_CREATE  | app → comp    | `type`, `title`                       |
| 17 | WIN_DESTROY | app → comp    | `window_id`                           |

- `type`: Window type enum value (§5.1)
- `title`: Window title string

### 4.3 Widget (stream 1)

| ID | Name       | Direction  | Args                                          |
|----|------------|------------|-----------------------------------------------|
| 32 | WGT_CREATE | app → comp | `widget_id`, `parent_id`, `widget_type`, `x`, `y`, `w`, `h`, `content` |
| 33 | WGT_UPDATE | app → comp | `widget_id`, `value` (replaces the widget text content) |
| 34 | WGT_STYLE  | app → comp | `widget_id`, `prop`, `value`                  |
| 35 | WGT_DESTROY| app → comp | `widget_id`                                   |
| 36 | EVT_BIND   | app → comp | `widget_id`, `event_type`                     |
| 37 | WGT_LAYOUT | app → comp | `widget_id`, `layout_type` (optional)         |

- `widget_id`: Unique widget ID, assigned by the app and used by all later commands.
- `parent_id`: Explicit parent reference. `0` = direct child of the window root; otherwise the ID of an existing **container** widget.
- `widget_type`: Widget type enum value (§5.2)
- `prop`: Style property enum value (§5.4)
- `event_type`: Event type enum value (§5.3)
- `layout_type`: Layout type enum value (§5.5); used only by `WGT_LAYOUT`.

#### 4.3.1 Container Widgets

**布局是程序策略（SVG 场景语义）。** 渲染器不运行任何布局引擎：程序计算每个子控件的几何（自己的内容、自己的规则），用 `WGT_CREATE` 的 `x,y,w,h` 或 `WGT_LAYOUT` 摆放。容器（`CONTAINER` 8 / `SCROLL` 11）是纯盒子——唯一内建行为是 `SCROLL` 的视口状态（滚动偏移、边缘箭头消费）。

层级通过 `parent_id` 显式表达：

```
WGT_CREATE;<container_id>;<parent_id=0>;<type=CONTAINER>;...
WGT_CREATE;<button_id>;<parent_id=container_id>;<type=BUTTON>;...
```

**只有容器可以有子控件。** `WGT_CREATE` 的 `parent_id` 若指向叶子控件（BUTTON、LABEL、INPUT、CHECKBOX、RADIO、SLIDER、PROGRESS、SWITCH、IMAGE、DROPDOWN、TIMEPICK、DATEPICK）则非法，合成器 MUST 拒绝。

`WGT_LAYOUT` **保留但语义收窄**：它调整的是渲染器可选的布局提示（对无布局引擎的后端是 no-op），不再是程序布局的规范途径。规范的布局途径 = 程序计算 rect。

### 4.4 Notifications (stream 4)

| ID | Name        | Direction    | Args                                   |
|----|-------------|--------------|----------------------------------------|
| 64 | NTF_RESIZE  | comp → app   | `width`, `height`                      |
| 65 | NTF_FOCUS   | comp → app   | `widget_id`, `focused`                 |
| 66 | NTF_DESTROY | comp → app   | `widget_id`                            |
| 67 | NTF_STATE   | comp → app   | `win_id`, `state`                      |

### 4.5 Events (stream 4)

| ID | Name       | Direction    | Args                                         |
|----|------------|--------------|----------------------------------------------|
| 80 | EVT_CLICK  | comp → app   | `widget_id`, `x`, `y`, `button`              |
| 81 | EVT_KEY    | comp → app   | `keycode`, `modifiers`, `pressed`            |
| 82 | EVT_VALUE  | comp → app   | `widget_id`, `value`                         |
| 83 | EVT_FOCUS  | comp → app   | `widget_id`, `focused`                       |

- `button`: Mouse button number (1=left, 2=middle, 3=right)
- `modifiers`: Bitmask (1=shift, 2=ctrl, 4=alt)
- `pressed`: 1=pressed, 0=released

### 4.6 IME (stream 1)

| ID | Name         | Direction     | Args                          |
|----|--------------|---------------|-------------------------------|
| 96 | IME_PREEDIT  | app → comp    | `widget_id`, `text`           |
| 97 | IME_COMMIT   | app → comp    | `widget_id`, `text`           |
| 98 | IME_CANDIDATES | app → comp | `win_id`, `widget_id`, `count`, `c1`..`cN` |
| 99 | IME_SELECT     | app → comp | `win_id`, `widget_id`, `index`             |

The IME application sends `IME_PREEDIT` for in-progress composition text and `IME_COMMIT` for finalized text. The compositor routes these to the focused textarea widget.

`IME_CANDIDATES` carries the candidate list for the active composition (`count` entries, `c1`..`cN`); at most `TGS_MAX_ARGS - 3` (13) candidates fit in one frame. `IME_SELECT` reports the zero-based `index` of the candidate the user picked.

## 5. Enums

### 5.1 Window Types

| Value | Name       | Description                        |
|-------|------------|------------------------------------|
| 0     | NORMAL     | Standard application window        |
| 1     | DIALOG     | Modal or dialog window             |
| 2     | FULLSCREEN | Full-screen window                 |
| 3     | TOOL       | Toolbar or utility window          |

### 5.2 Widget Types

| Value | Name       | Description              |
|-------|------------|--------------------------|
| 0     | BUTTON     | Push button              |
| 1     | LABEL      | Text label               |
| 2     | INPUT      | Text input field         |
| 3     | CHECKBOX   | Checkbox toggle          |
| 4     | RADIO      | Radio button             |
| 5     | SLIDER     | Value slider             |
| 6     | PROGRESS   | Progress bar             |
| 7     | SWITCH     | Toggle switch            |
| 8     | CONTAINER  | Plain box; children at program-computed rects |
| 9     | *reserved* | (retired VLAYOUT)        |
| 10    | *reserved* | (retired HLAYOUT/GLAYOUT) |
| 11    | SCROLL     | Scrollable container     |
| 12    | LIST       | List view                |
| 13    | TABLE      | Table view               |
| 14    | MENU       | Menu container           |
| 15    | TAB        | Tab container            |
| 16    | DROPDOWN   | Dropdown select          |
| 17    | IMAGE      | Image display            |
| 18    | TIMEPICK   | Time picker              |
| 19    | DATEPICK   | Date picker              |

Total: 20 widget types.

Container semantics (§4.3.1): `CONTAINER` (8) and `SCROLL` (11) are the only types valid as a `parent_id`. Every other type is a leaf.

#### 5.2.1 Primitive vs derived (design criterion)

A widget type is a **primitive** iff the renderer must natively hold state or
capability that no composition of other protocol primitives can express
(edit text, toggle state, a value with range, pixel source, scroll offset,
page-switch state). Otherwise the type is **derived** — expressible as a
composition — and is admitted only as a convenience with a stated exit path.

Classification of the 20 types:

- **Primitive** (9): `LABEL`, `BUTTON`, `INPUT` (text-edit state, IME-eligible),
  `CHECKBOX` (toggle), `SLIDER` (value+range), `PROGRESS` (read-only value),
  `SWITCH` (toggle+animation), `IMAGE` (pixel source), `SCROLL` (scroll offset,
  arrow-consumption).
- **Derived convenience** (10): `RADIO` (checkbox + visual policy; exclusivity
  is app policy), `LIST` (scroll + appended rows), `MENU` (composition; stub —
  deprecation candidate), `TAB` (page-strip + page stack), `DROPDOWN`
  (collapsed list + popup), `TIMEPICK`/`DATEPICK` (structured value parsers).
- **Collapsed** (2026-09-18): `VLAYOUT`/`HLAYOUT`/`GLAYOUT` retired → plain
  `CONTAINER` (8). Layout is program policy (SVG-scene semantics): the program
  computes child rects; the renderer runs no layout engine. `WGT_LAYOUT` is
  demoted to an optional renderer hint, not the normative layout path.

Rules for new types (normative):

1. A new widget type MUST declare the primitive state the renderer natively
   holds, or MUST be rejected as mere composition (build it in the app from
   existing primitives).
2. A derived type MUST NOT accumulate behavior that a primitive cannot reach
   through `WGT_STYLE` / `WGT_ATTR` / `WGT_LAYOUT` — otherwise the protocol
   forks into per-widget special cases and stops being renderer-neutral.
3. Events stay interaction-shaped (`CLICK`, `VALUE_CHANGED`, `KEY`, `HOVER_*`,
   `FOCUS`), never widget-shaped; this is what keeps the protocol renderer-
   neutral (a widget-shaped event like "tab changed" would bind the wire to
   one toolkit's widget catalog).

### 5.3 Event Types

| Value | Name            | Description                  |
|-------|-----------------|------------------------------|
| 0     | CLICK           | Mouse click                  |
| 1     | KEY             | Keyboard input               |
| 2     | FOCUS           | Widget gained focus          |
| 3     | BLUR            | Widget lost focus            |
| 4     | VALUE_CHANGED   | Input value changed          |
| 5     | IME_PREEDIT     | IME composition text         |
| 6     | IME_COMMIT      | IME committed text           |

### 5.4 Style Properties

| Value | Name           | Description                  |
|-------|----------------|------------------------------|
| 0     | BG_COLOR       | Background color (RGB hex)   |
| 1     | FG_COLOR       | Foreground/text color        |
| 2     | RADIUS         | Border radius (pixels)       |
| 3     | BORDER_WIDTH   | Border width (pixels)        |
| 4     | BORDER_COLOR   | Border color (RGB hex)       |
| 5     | SHADOW_WIDTH   | Shadow width (pixels)        |
| 6     | SHADOW_COLOR   | Shadow color (RGB hex)       |
| 7     | FONT_SIZE      | Font size (pixels)           |
| 8     | OPACITY        | Opacity (0-255)              |

### 5.5 Layout Types

| Value | Name      | Description                    |
|-------|-----------|--------------------------------|
| 0     | NONE      | No layout (manual positioning) |
| 1     | FLEX_ROW  | Horizontal flex layout         |
| 2     | FLEX_COL  | Vertical flex layout           |
| 3     | GRID      | Grid layout                    |

### 5.6 Window States

| Value | Name       | Description                    |
|-------|------------|--------------------------------|
| 0     | NORMAL     | Restored, visible window       |
| 1     | MINIMIZED  | Minimized to taskbar           |
| 2     | MAXIMIZED  | Maximized to workspace bounds  |
| 3     | FULLSCREEN | Fullscreen, no window chrome   |
| 4     | HIDDEN     | Hidden but still alive         |

## 6. Handshake Sequence

```
App                              Compositor
 │                                    │
 │──── HELLO (version, caps) ────────>│
 │                                    │
 │<─── READY (version, caps) ─────────│
 │                                    │
 │         (connection ready)         │
```

Or, when the compositor cannot honour the requested capabilities:

```
App                              Compositor
 │                                    │
 │──── HELLO (version, caps) ────────>│
 │                                    │
 │<─── REJECT (rejected_cap) ─────────│
 │                                    │
 │         (app disconnects)          │
```

1. App sends `HELLO` on stream 0 with protocol version and supported capabilities.
2. If the request is supported, the compositor responds with `READY` on stream 0, echoing its version and capabilities.
3. Both sides may now use streams 1-4.

If the compositor does not support the requested version, it responds with `READY` containing its own version. The app must re-negotiate or disconnect.

If the compositor cannot support a capability the app requested, it responds with `REJECT` on stream 0 instead of `READY`, carrying the offending capability token in `rejected_cap`. The app must not send further commands and should disconnect or re-handshake without that capability.

## 7. Capability Negotiation

Capabilities are comma-separated tokens in the `HELLO`/`READY` args:

```
TGS;0;0;1;1.0;layout.tiled,widget.button,widget.label
```

### 7.1 Capability Tokens

| Token             | Description                    |
|-------------------|--------------------------------|
| `layout.tiled`    | Tiled window layout            |
| `layout.float`    | Floating window layout         |
| `widget.button`   | Button widget                  |
| `widget.label`    | Label widget                   |
| `widget.input`    | Text input widget              |
| `event.click`     | Click events                   |
| `event.key`       | Key events                     |
| `event.resize`    | Resize notifications           |
| `event.focus`     | Focus events                   |

Default Layer 0 capabilities:

```
layout.tiled,widget.button,widget.label,widget.input,
event.click,event.key,event.resize,event.focus
```

## 8. IME Protocol

Input Method Editor (IME) applications send preedit and commit text on stream 1:

```
TGS;1;<frame_id>;96;<widget_id>;<preedit_text>    (IME_PREEDIT)
TGS;1;<frame_id>;97;<widget_id>;<committed_text>  (IME_COMMIT)
```

The compositor routes these to the focused textarea/input widget. `IME_PREEDIT` updates the composition display; `IME_COMMIT` inserts the finalized text at the cursor position.

IME events are also reflected back on stream 4 as `TGS_EVENT_IME_PREEDIT` (5) and `TGS_EVENT_IME_COMMIT` (6) for app-side tracking.

Candidate lists travel on stream 1 as well:

```
TGS;1;<frame_id>;98;<win_id>;<widget_id>;<count>;<c1>;...;<cN>  (IME_CANDIDATES)
TGS;1;<frame_id>;99;<win_id>;<widget_id>;<index>               (IME_SELECT)
```

`IME_CANDIDATES` replaces the compositor's view of the candidate list for the focused input widget. `IME_SELECT` tells the compositor which zero-based candidate index the user chose; it is typically followed by an `IME_COMMIT`.

## 9. Frame ID Rules

- Each stream maintains an independent monotonic counter starting at 0.
- Frame IDs wrap at 2^31 to signed 32-bit range.
- Responses use the same frame ID as the request they answer.
- Unsolicited messages (events, notifications) use the next frame ID on their stream.

## 10. Error Handling

- Malformed frames (wrong prefix, missing fields) are silently dropped.
- Unknown command IDs are ignored.
- Arguments beyond `TGS_MAX_ARGS` (16) are truncated.
- Arguments exceeding `TGS_MAX_ARG_LEN` (128 bytes) are truncated.

## Appendix A: Reference Implementation

- Header: `src/common/tgs_protocol.h` — All type definitions and constants
- Frame codec: `src/common/tgs_frame.h` / `tgs_frame.c` — Encode/decode/write
- Backend interface: `src/common/tgs_backend.h` — Abstract rendering backend
