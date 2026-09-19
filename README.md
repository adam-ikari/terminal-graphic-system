# TGS — Terminal Graphic System

A lightweight graphics system for terminals, built on ANSI/VT100 escape sequences.

## What is TGS?

TGS turns your terminal into a graphical platform. Applications build UIs through a byte-stream protocol — no GPU dependency, no desktop environment, works over SSH.

## Architecture

```
┌─────────────────────────────────────────┐
│  TGS Compositor (desktop/embedded)      │
│  ├── Protocol Parser (APC frames)       │
│  ├── Window Manager                     │
│  ├── Scene Backend (drawing prims)      │
│  ├── Output Backend (SDL2 or FB)        │
│  └── IME Router → IME App               │
├─────────────────────────────────────────┤
│  TGS Client Library (C API)             │
├─────────────────────────────────────────┤
│  Applications (your code)               │
└─────────────────────────────────────────┘
```

## Layer 1 Features

- **Drawing-semantics renderer** — 8 primitive kinds (button, label, input, checkbox, slider, container, scroll, image); a paint port with two reference backends (SDL2 today, Skia next); no layout engine in the renderer.
- **Explicit containers** — `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply their layout from their type at creation; only containers can parent children.
- **Keyboard navigation** — compositor-owned focus model with per-window rings/scopes, `Tab`/`Shift-Tab`/arrow traversal, `SET_FOCUS`/`NTF_FOCUS` with reason, `WGT_ATTR` (`FOCUSABLE`/`FOCUS_INDEX`/`NAV_ARROWS`/scope), and key-routing precedence. See [docs/navigation.md](docs/navigation.md).
- **Robust input pipeline** — edge-queued pointer/keypad input (fast clicks register, no double keys), canonical key space + modifiers.
- **FB present contract** — the scene backend publishes its framebuffer; the compositor copies to mmap'd `/dev/fb0` (16/24/32 bpp).

## Quick Start

```bash
# Build
git submodule update --init --recursive
mkdir -p build && cd build && cmake -DTGS_USE_SDL=ON .. && make -j$(nproc)

# Run demo
./build/tgs-compositor ./build/simple_form
```

## Requirements

- C99 compiler
- Linux (macOS/Windows WSL later)
- SDL2 development libraries (`libsdl2-dev`)

## Building

```bash
make init     # Clone dependencies
make build    # Compile
make test     # Run tests
make run      # Run compositor with demo app
make run-xvfb # Headless: run under Xvfb and capture a screenshot
```

## Project Structure

```
src/
├── common/          # Protocol definitions (backend-agnostic)
├── compositor/      # Terminal compositor core
├── backends/scene/  # scene core + SDL2 paint port + term view
└── client/          # C client library
examples/
├── simple_form.c    # Demo: buttons, labels, input
└── ime_app.c        # Reference IME implementation
protocol/
└── tgs-spec-layer0.md  # Protocol specification
```

## Protocol

TGS uses APC (Application Program Command) frames over stdin/stdout:

```
ESC _ TGS;<stream>;<frame_id>;<command>;<args>... ESC \
```

See [protocol/tgs-spec-layer0.md](protocol/tgs-spec-layer0.md) for the full specification.

## Documentation

- [Architecture](docs/architecture.md)
- [Getting Started](docs/getting-started.md)
- [Protocol Specification](protocol/tgs-spec-layer0.md)
- [IME Framework](docs/ime.md)
- [Navigation](docs/navigation.md)
- [Widget Reference](docs/widgets.md)

## License

MIT
