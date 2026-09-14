# TGS IME Framework

## Overview

TGS IME (Input Method Engine) is implemented as a **separate TGS application**, not built into the compositor. This design keeps the compositor simple and allows pluggable IME engines.

```
┌──────────┐        ┌──────────────┐        ┌──────────┐
│ App with │  PTY   │              │  PTY   │          │
│ textarea │◄──────►│  Compositor  │◄──────►│ IME App  │
│          │        │              │        │          │
└──────────┘        │  IME Router  │        └──────────┘
                    └──────────────┘
```

## How It Works

1. App creates a widget with input capability (e.g., `TGS_WIDGET_INPUT`)
2. User focuses the widget and types
3. Compositor's **IME Router** detects the focus on an IME-aware widget
4. If an IME app is connected (has sent HELLO on the IME PTY), key events are routed to it
5. IME app processes the keystroke and sends results back:
   - `IME_COMMIT` — committed text to insert into the widget
   - `IME_PREEDIT` — composition preview text to display temporarily
6. Compositor calls `insert_widget_text()` on the backend to insert committed text

## Compositor-Side IME Routing

The window manager tracks IME state:

```c
typedef struct {
    tgs_backend *backend;
    int pty_fd;          /* Main app PTY fd */
    int ime_pty_fd;      /* IME app PTY fd (-1 if not running) */
    int frame_counter;
    int disp_w, disp_h;
    int hello_received;
    int ime_connected;   /* 1 if IME app has sent HELLO */
} window_manager;
```

- `ime_pty_fd` — PTY file descriptor for the IME app process
- `ime_connected` — flag set to `1` when IME app completes handshake
- When `ime_connected == 1` and a key event arrives, the compositor forwards it to the IME app instead of delivering it directly to the focused app

## Direct Input Engine (Reference Implementation)

The reference IME (`examples/ime_app.c`) implements a **Direct Input** engine — the simplest possible IME:

- Each printable ASCII keystroke commits immediately
- Non-printable keys (Enter, Backspace) commit their logical output
- Arrow keys, Tab, Escape pass through unchanged

```c
/* Direct Input: printable ASCII commits immediately */
if (key >= 0x20 && key <= 0x7e) {
    char committed[2] = { (char)key, '\0' };
    tgs_client_send_ime_commit(ev.window_id, ev.widget_id, committed);
}
```

This serves as a baseline and a starting point for custom IME engines.

## Creating Custom IME Engines

An IME engine is a standard TGS application that:

1. Calls `tgs_client_init()` to connect
2. Creates a **tool window** (`TGS_WINDOW_TOOL`) — hidden from view
3. Enters an event loop, listening for `TGS_EVENT_KEY` events
4. Processes keystrokes through its engine logic
5. Sends results back via:
   - `tgs_client_send_ime_commit(win_id, widget_id, text)` — final committed text
   - `tgs_client_send_ime_preedit(win_id, widget_id, text, cursor)` — composition preview

### Example: Pinyin-like IME skeleton

```c
while (tgs_client_poll_event(&ev, -1) == 0) {
    if (ev.type == TGS_EVENT_KEY) {
        if (is_composing) {
            if (ev.key == ENTER) {
                /* Pick first candidate, commit */
                tgs_client_send_ime_commit(ev.window_id, ev.widget_id,
                                           selected_candidate);
                reset_composition();
            } else if (ev.key == ESCAPE) {
                /* Cancel composition */
                tgs_client_send_ime_preedit(ev.window_id, ev.widget_id, "", 0);
                reset_composition();
            } else {
                /* Append to pinyin buffer, update preedit */
                append_to_buffer(ev.key);
                tgs_client_send_ime_preedit(ev.window_id, ev.widget_id,
                                            composition_text, cursor_pos);
            }
        } else {
            if (is_printable(ev.key)) {
                start_composing(ev.key);
            }
        }
    }
}
```

### Launching an IME App

The compositor starts the IME app as a child process on a separate PTY. The IME connects like any TGS client — it performs the HELLO/READY handshake and then receives key events.

## Protocol Events

### IME_COMMIT (command 97)

Sent by IME app → compositor. Inserts text into the focused widget.

```
ESC _ TGS;4;<frame_id>;97;<win_id>;<widget_id>;<text> ESC \
```

### IME_PREEDIT (command 96)

Sent by IME app → compositor. Shows composition preview (temporary, not yet committed).

```
ESC _ TGS;4;<frame_id>;96;<win_id>;<widget_id>;<text>;<cursor_pos> ESC \
```

### Client API

```c
/* Send committed text from IME app */
int tgs_client_send_ime_commit(int win_id, int widget_id, const char *text);

/* Send composition preview from IME app */
int tgs_client_send_ime_preedit(int win_id, int widget_id, const char *text, int cursor);
```

## Design Rationale

- **Separate process** — IME crash does not take down compositor or apps
- **Same protocol** — IME uses the same TGS client API as any app
- **Pluggable** — swap IME engines by running a different process
- **Sandboxed** — IME can only send commit/preedit events, not arbitrary widget commands
