# TGS Architecture

## System Overview

TGS (Terminal Graphic System) turns a terminal into a graphical platform. Applications build UIs through a byte-stream protocol — no GPU dependency, no desktop environment, works over SSH.

The system has four layers:

1. **Protocol** — Wire format (APC frames), stream multiplexing, capability negotiation
2. **Compositor** — Protocol parser, window manager, event engine, IME router, LVGL backend, output backends (SDL2 / FB)
3. **Client Library** — C API (`tgs_client`) for applications
4. **Applications** — User code, IME apps, tools

## Component Diagram

```
┌──────────────────────────────────────────────┐
│  TGS Compositor (host process)               │
│                                              │
│  ┌────────────────┐  ┌─────────────────────┐ │
│  │ Protocol Parser │  │ Window Manager      │ │
│  │ (APC frames)    │──│ (ID → LVGL handle)  │ │
│  └────────────────┘  │ Event routing       │ │
│                      └────────┬────────────┘ │
│  ┌────────────────┐          │              │
│  │ Event Engine   │──────────┘              │
│  │ (terminal →    │                         │
│  │  backend)      │  ┌─────────────────────┐│
│  └────────────────┘  │ LVGL Backend        ││
│                      │ (20+ widget types)  ││
│  ┌────────────────┐  └────────┬────────────┘│
│  │ IME Router     │           │             │
│  │ (keyboard →    │  ┌────────▼────────────┐│
│  │  IME app)      │  │ Output Backend      ││
│  └────────────────┘  │ (SDL2 / /dev/fb0)   ││
│                      └────────┬────────────┘│
│                               │             │
├───────────────────────────────┼─────────────┤
│                               │ (pixel data)│
└───────────────────────────────┼─────────────┘
                                │
┌───────────────────────────────▼─────────────┐
│  Display Output                             │
│  (SDL window / framebuffer)                 │
└─────────────────────────────────────────────┘
```

Each application (including IME) runs as a **separate process** connected to the compositor via its own PTY:

```
┌──────────┐  PTY  ┌──────────────┐  PTY  ┌──────────┐
│ App      │◄─────►│              │◄─────►│ IME App  │
│ (simple  │  stdin│  Compositor  │  stdin│ (ime_app)│
│  _form)  │◄─────►│              │◄─────►│          │
└──────────┘ stdout└──────────────┘ stdout└──────────┘
```

## Output Backends

TGS supports two output backends, selected at compile time:

### SDL Backend (Desktop Linux)
- SDL2 creates a window and simulates a framebuffer
- LVGL renders into a pixel buffer, displayed via SDL texture
- SDL events handle keyboard and mouse input
- Compile option: `cmake -DTGS_USE_SDL=ON`

### Framebuffer Backend (Embedded)
- Direct write to `/dev/fb0` (via mmap)
- evdev reads `/dev/input/event*` for input
- No desktop environment dependency
- Compile option: `cmake -DTGS_USE_SDL=OFF` (implementation pending)

## Data Flow: App → Compositor → Terminal

```
tgs_client_create_window()       → frame: TGS;1;0;WIN_CREATE;...
tgs_client_create_widget()       → frame: TGS;1;1;WGT_CREATE;...
tgs_client_set_widget_style()    → frame: TGS;1;2;WGT_STYLE;...
tgs_client_bind_event()          → frame: TGS;1;3;EVT_BIND;...
                                    │
                           ┌────────▼────────┐
                           │ Protocol Parser  │
                           │ (parse APC frame)│
                           └────────┬────────┘
                                    │
                           ┌────────▼────────┐
                           │ Window Manager   │
                           │ translate to     │
                           │ backend calls    │
                           └────────┬────────┘
                                    │
                           ┌────────▼────────┐
                           │ LVGL Backend     │
                           │ create widget,   │
                           │ set style, etc.  │
                           └────────┬────────┘
                                    │
                           ┌────────▼────────┐
                           │ Sixel Output     │
                           │ render to stdout │
                           └────────┬────────┘
                                    │
                                    ▼
                           Terminal renders Sixel
```

## Data Flow: Terminal Input → App

```
Terminal keypress / mouse
            │
    ┌───────▼───────┐
    │ Event Engine   │
    │ read PTY input │
    └───────┬───────┘
            │
    ┌───────▼───────┐
    │ LVGL Backend   │
    │ inject_mouse() │
    │ inject_key()   │
    └───────┬───────┘
            │
    ┌───────▼───────┐
    │ Window Manager │
    │ match widget   │
    │ fire callback  │
    └───────┬───────┘
            │
    ┌───────▼───────┐
    │ Protocol Encode│
    │ EVT_CLICK      │
    │ EVT_KEY, etc.  │
    └───────┬───────┘
            │
            ▼
    App receives tgs_event via tgs_client_poll_event()
```

## IME Flow

```
Terminal keypress
        │
        ▼
Event Engine → compositor detects focus on IME-aware widget
        │
        ▼
IME Router → forwards key to IME app PTY
        │
        ▼
IME App (ime_app.c) → processes keystroke
        │
        ├─ IME_COMMIT  → compositor inserts text into focused widget
        └─ IME_PREEDIT → compositor shows composition preview
```

The IME is a **separate TGS application**, not built into the compositor. It communicates via the same TGS protocol:

- Receives `EVT_KEY` events from the compositor
- Sends `IME_COMMIT` / `IME_PREEDIT` events back
- Compositor translates these into `insert_widget_text()` / preedit display on the target textarea

## Protocol Layers

### Wire Format

Every message is an APC frame:

```
ESC _ TGS;<stream_id>;<frame_id>;<command>;<arg1>;...;\<argN> ESC \
```

### Streams (multiplexed over single PTY)

| ID | Name         | Direction        | Purpose                        |
|----|--------------|------------------|--------------------------------|
| 0  | HANDSHAKE    | bidirectional    | Connection setup, capability negotiation |
| 1  | COMMAND      | app → compositor | Widget tree manipulation       |
| 2  | RESOURCE     | app → compositor | Binary resources (images)      |
| 3  | FRAMEBUFFER  | compositor → app | Sixel/pixel data               |
| 4  | EVENT        | compositor → app | User input events              |

### Handshake Sequence

```
App → Compositor:  HELLO;protocol_version;capabilities
Compositor → App:  READY;compositor_version;supported_capabilities
```

If the compositor does not support a requested capability, it returns `REJECT` instead of `READY`.

## Backend Abstraction

The `tgs_backend` struct defines a clean interface between the protocol layer and rendering:

```c
struct tgs_backend {
    void *user_data;

    /* Lifecycle */
    int  (*init)(int width, int height);
    void (*tick)(uint32_t ms);
    void (*deinit)(void);

    /* Rendering */
    void (*render)(void);
    void (*set_size)(int w, int h);

    /* Windows */
    void *(*create_window)(tgs_window_type type, const char *title);
    void  (*destroy_window)(void *handle);

    /* Widgets */
    void *(*create_widget)(void *parent, tgs_widget_type type);
    void  (*set_widget_rect)(void *handle, int x, int y, int w, int h);
    void  (*set_widget_content)(void *handle, const char *text);
    void  (*insert_widget_text)(void *handle, const char *text);
    void  (*set_widget_style)(void *handle, tgs_style_prop prop, int32_t value);
    void  (*set_widget_layout)(void *handle, tgs_layout_type layout);
    void  (*destroy_widget)(void *handle);

    /* Events */
    void (*set_event_callback)(tgs_event_cb cb, void *user_data);

    /* Input injection */
    void (*inject_mouse)(int x, int y, int button, int pressed);
    void (*inject_key)(int key, int mods, int pressed);
};
```

Register a backend with `tgs_backend_register()`. The compositor supports swapping backends at runtime — protocol never touches rendering directly.

## Window Management Model

The window manager (`window_manager`) maps widget/window IDs to backend handles:

- Each app gets one or more windows (normal, dialog, fullscreen, tool)
- Windows are root containers; widgets are children
- IDs are app-assigned integers — the compositor translates them to LVGL handles
- Events are routed by widget ID back to the originating app

Window types:

| Type         | Purpose                        |
|--------------|--------------------------------|
| `NORMAL`     | Standard application window    |
| `DIALOG`     | Modal dialog                   |
| `FULLSCREEN` | Takes over entire terminal     |
| `TOOL`       | Hidden/utility window (e.g. IME) |

## Widget Library (Layer 1)

All 20 `tgs_widget_type` values map to real LVGL 9.6 widgets (see [widgets.md](widgets.md)): `lv_button`, `lv_label`, `lv_textarea`, `lv_checkbox`, `lv_slider`, `lv_switch`, `lv_bar`, `lv_list`, `lv_table`, `lv_menu`, `lv_tabview`, `lv_dropdown`, `lv_image`, `lv_roller`, `lv_calendar`, plus the four container types below. `RADIO` is a round-styled checkbox (LVGL has no dedicated radio); `LIST`/`MENU` use LVGL's deprecated-but-present implementations behind a pragma to silence the deprecation warning.

### Explicit containers

`VLAYOUT`, `HLAYOUT`, `GLAYOUT`, `SCROLL` are first-class widget types, not a style flag on a generic box. Each applies its layout from its type at creation (`backend_create_widget` sets `flex_flow` / `grid_dsc_array` / scrollable). The window manager **rejects** `WGT_CREATE` whose `parent` is a window or a container widget only — a leaf widget may never parent another widget (`window_manager.c` WGT_CREATE path), enforcing a single, consistent container rule and keeping tree-walk based focus rings valid.

### Input pipeline

Both backends feed input through an **edge-queued** indev model:

- **Pointer** (`input_sdl.c`, `input_fb.c`): only state *transitions* are enqueued — fast clicks that previously landed between two `mouse_read_cb` polls now register. `mouse_qedged` holds the last queued state to suppress repeats.
- **Keypad**: only key *DOWN* edges are queued as presses; releasing on key-up made every typed character appear twice, collapsing a burst dropped keys.
- **Canonical key space**: every source normalises to one space before the backend — printable ASCII is itself, `1000..1005` = LEFT/RIGHT/UP/DOWN/HOME/END, modifiers `0x01` SHIFT / `0x02` CTRL / `0x04` ALT. Shift is applied to letters/digits. The LVGL backend translates this back to `LV_KEY_*`.

### FB present contract

The compositor never hands a pointer directly to `/dev/fb0`. LVGL publishes into `disp_drv->buffer`; `output_present()` (`output_fb.c`) **copies** that buffer to the mmap'd framebuffer. The copy is bpp-agnostic and verified at 16/24/32 bpp against a fake-fb with zero pixel mismatches, so LVGL may republish or reorganise its draw buffer freely.

## Navigation (Layer 1)

Keyboard focus is owned by the compositor, not LVGL. The design and full spec live in [navigation.md](navigation.md); summary:

- **Focus model** (`src/compositor/nav.c`, `nav.h`): a per-window registry of widgets + focus rings + scopes. Only focusable types are ring members; containers are transparent to traversal.
- **`NTF_FOCUS` (65)** with `[win_id, widget_id, focused, reason]` notifies the app of every focus change (gained/lost) with the reason (Tab, pointer, `SET_FOCUS`, programmatic). `EVT_FOCUS` (83) is retired.
- **`SET_FOCUS` (38)** / **`WGT_ATTR` (40)**: app requests focus and per-widget attributes (`TGS_ATTR_FOCUSABLE`, `TGS_ATTR_FOCUS_INDEX`, `NAV_ARROWS`, `nav.scope` = GROUP/TRAP).
- **Key-routing precedence**: the compositor consults a `nav_key_cb` hook *before* LVGL's indev; it consumes Tab/Shift-Tab/arrows only when the focused widget doesn't (e.g. a slider consumes Left/Right, an INPUT consumes arrows when `NAV_ARROWS` is set).
- **Window navigation**: per-window LVGL roots + groups; pointer click activates and restores last focus; z-order is read-only for restoration.
- **IME cancel**: focus leaving an IME-armed `INPUT` sends `IME_CANCEL` (100) to the IME process *before* clearing visuals and emitting `NTF_FOCUS` — never auto-commits. The preedit *overlay* itself is a documented Layer-1 gap (see [navigation.md §H.2](navigation.md#h2-preedit-is-an-overlay-never-widget-text-invariant)).

## Security Model

- **Process isolation**: Each app runs in its own process with its own PTY. Apps cannot read other apps' data.
- **Sandboxed protocol**: Apps communicate only via TGS protocol frames. No direct access to compositor internals, filesystem, or other apps.
- **Resource access**: Apps declare resource needs (images, fonts) in the RESOURCE stream. Compositor mediates access.
- **IME separation**: IME is an untrusted external app. Compositor validates IME output before insertion.
- **Capability negotiation**: Apps declare what they need at handshake. Compositor grants or rejects. No ambient authority.

## Constraints

- **C99** with minimal dependencies
- **Terminal compatibility**: ANSI/VT100 base, Sixel for rendering
- **Performance**: Single widget operations < 1ms, complex UIs < 50ms render
- **No GPU required**: CPU-only Sixel path for Layer 0; GPU path planned for Layer 2+
