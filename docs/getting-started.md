# Getting Started with TGS

## Prerequisites

- **C99 compiler** — gcc or clang
- **Linux** — macOS/Windows WSL later
- **SDL2 development libraries** — `libsdl2-dev` (for desktop SDL backend)
- **Sixel-capable terminal** — xterm (`-ti vt340`), mlterm, mintty, WezTerm, foot, etc.
- **CMake** — build system
- **Git** — for submodule dependencies

## Build

```bash
# Clone the repo
git clone <repo-url>
cd terminal-graphic-system

# Initialize dependencies
make init

# Build everything (SDL backend for desktop)
make build
```

This produces:
- `build/tgs-compositor` — the terminal compositor
- `build/simple_form` — demo application
- `build/ime_app` — reference IME application

## Run the Demo

```bash
# Option 1: use make
make run

# Option 2: run compositor directly
./build/tgs-compositor ./build/simple_form
```

You should see a form with a label, text input, and submit button rendered in an SDL window (or via Sixel in a terminal, depending on the build configuration).

## Xvfb (Headless)

The SDL backend needs an X display. With no `$DISPLAY` set, `SDL_Init` fails and the
compositor exits immediately. Xvfb provides a virtual X framebuffer, so the *real*
compositor — not a test harness — can run (and be screenshotted) on a headless machine.

```bash
make run-xvfb                    # writes xvfb-shot.png
./scripts/run-xvfb.sh out.png    # or choose the output path
```

The script claims a free display (`:99`–`:130`), runs `build/tgs-compositor` against
`build/simple_form`, waits for the window to render, captures it with `import` (falling
back to the root window, then `scrot`), and then tears everything down. Xvfb and the
compositor are always cleaned up, including on failure, and the script exits non-zero if
no screenshot was produced or the result is blank.

Requires `Xvfb` (package `xvfb`), `xdpyinfo` (`x11-utils`), `xdotool`, and a screenshot
tool (`import` from ImageMagick, or `scrot`).

## Create Your First TGS App

Copy the `examples/simple_form.c` pattern:

```c
#include "tgs_client.h"
#include <stdio.h>
#include <string.h>

/* Define widget IDs */
#define ID_LABEL  1
#define ID_INPUT  2
#define ID_BUTTON 3

int main(void)
{
    tgs_event ev;
    tgs_window_info win;

    /* 1. Initialize client — connects to compositor via stdin/stdout */
    if (tgs_client_init() != 0) return 1;

    /* 2. Create a window */
    tgs_client_create_window(TGS_WINDOW_NORMAL, "My App", &win);

    /* 3. Create widgets */
    tgs_client_create_widget(TGS_WIDGET_LABEL, ID_LABEL, win.window_id,
                             20, 20, 200, 30, "Hello:");
    tgs_client_create_widget(TGS_WIDGET_INPUT, ID_INPUT, win.window_id,
                             20, 60, 300, 40, "");
    tgs_client_create_widget(TGS_WIDGET_BUTTON, ID_BUTTON, win.window_id,
                             20, 120, 120, 40, "Click Me");

    /* 4. Bind events */
    tgs_client_bind_event(ID_BUTTON, TGS_EVENT_CLICK);

    /* 5. Event loop */
    while (tgs_client_poll_event(&ev, -1) == 0) {
        if (ev.type == TGS_EVENT_CLICK && ev.widget_id == ID_BUTTON) {
            tgs_client_update_widget(ID_LABEL, "Clicked!");
        }
    }

    tgs_client_shutdown();
    return 0;
}
```

Build with: `gcc -std=c99 -I src/client my_app.c -o my_app -L build -ltgs_client`

Run with: `./build/tgs-compositor ./my_app`

## API Overview

| Function | Purpose |
|----------|---------|
| `tgs_client_init()` | Connect to compositor, perform handshake |
| `tgs_client_create_window(type, title, &info)` | Create a window; returns ID and dimensions |
| `tgs_client_create_widget(type, id, parent_id, x, y, w, h, content)` | Add a widget to a window |
| `tgs_client_update_widget(id, text)` | Change widget text content |
| `tgs_client_set_widget_style(id, prop, value)` | Set a style property |
| `tgs_client_bind_event(widget_id, event_type)` | Subscribe to events on a widget |
| `tgs_client_poll_event(&ev, timeout_ms)` | Wait for next event; `-1` = block forever |
| `tgs_client_get_widget_text(widget_id)` | Read current widget text |
| `tgs_client_destroy_widget(id)` | Remove a widget |
| `tgs_client_destroy_window(win_id)` | Close a window |
| `tgs_client_shutdown()` | Disconnect and clean up |

### Key Types

```c
typedef struct {
    int window_id;
    int x, y, w, h;
} tgs_window_info;

typedef struct {
    int type;           /* tgs_event_type */
    int window_id;
    int widget_id;
    char text[256];     /* for VALUE_CHANGED events */
    int key;            /* for KEY events */
    int modifiers;      /* for KEY events */
    int focused;        /* for FOCUS events: 1=gained, 0=lost */
} tgs_event;
```

## Protocol Quick Reference

All communication uses APC frames over stdin/stdout:

```
ESC _ TGS;<stream>;<frame_id>;<command>;... ESC \
```

Full specification: [protocol/tgs-spec-layer0.md](../protocol/tgs-spec-layer0.md)
