# TGS Protocol

## Overview

TGS Layer 0 is a single-stream multiplexed, APC-framed, text-based protocol between an app and a compositor. All communication occurs over a PTY using Application Program Command (APC) escape sequences.

Full specification: [protocol/tgs-spec-layer0.md](../protocol/tgs-spec-layer0.md)

## Wire Format

Every message is wrapped in an APC sequence:

```
ESC _ TGS;<stream_id>;<frame_id>;<command>;<arg1>;<arg2>;... ESC \
```

- `ESC` = `0x1B`
- `_` = `0x5F` (APC introducer)
- `ESC \` = `0x1B 0x5C` (String Terminator)

Payload fields:

| Field       | Description |
|-------------|-------------|
| `TGS`       | Magic header (always literal) |
| `stream_id` | Logical stream (0–4) |
| `frame_id`  | Monotonic counter per stream, wrapping at 2^31 |
| `command`   | Command ID |
| `arg1..N`   | Semicolon-delimited arguments (max 16, each ≤ 128 bytes) |

## Streams

| ID | Name         | Direction        | Purpose |
|----|--------------|------------------|---------|
| 0  | HANDSHAKE    | bidirectional    | Connection setup, capability negotiation |
| 1  | COMMAND      | app → compositor | Widget tree commands |
| 2  | RESOURCE     | app → compositor | Binary resources (images) |
| 3  | FRAMEBUFFER  | app → compositor | Client pixel-surface updates |
| 4  | EVENT        | compositor → app | User input events |

> Stream 3 carries client pixel-surface data (see [`architecture-v2.md`](architecture-v2.md) §4): the
> client uploads a buffer for a pixel surface. It uses the **binary DCS + length-prefix** framing,
> not the APC text frame the other streams use. Stream 2 (RESOURCE) stays APC text with base64.

## Handshake Sequence

```
App → Compositor:  TGS;0;1;HELLO;1.0;layout.tiled,widget.button,...
Compositor → App:  TGS;0;2;READY;1.0;layout.tiled,widget.button,...
```

- `HELLO` includes protocol version and requested capabilities
- `READY` confirms supported capabilities
- Unrecognized capabilities are rejected (no `READY` sent; connection closes)

## Key Commands

| Stream | Command | ID | Direction | Purpose |
|--------|---------|----|-----------|---------|
| HANDSHAKE | HELLO | 1 | app → compositor | Start handshake |
| HANDSHAKE | READY | 2 | compositor → app | Confirm handshake |
| COMMAND | WIN_CREATE | 16 | app → compositor | Create window |
| COMMAND | WIN_DESTROY | 17 | app → compositor | Destroy window |
| COMMAND | WGT_CREATE | 32 | app → compositor | Create widget |
| COMMAND | WGT_UPDATE | 33 | app → compositor | Update widget text |
| COMMAND | WGT_STYLE | 34 | app → compositor | Set widget style |
| COMMAND | WGT_DESTROY | 35 | app → compositor | Destroy widget |
| COMMAND | EVT_BIND | 36 | app → compositor | Subscribe to events |
| COMMAND | NTF_RESIZE | 64 | compositor → app | Window resized |
| COMMAND | NTF_FOCUS | 65 | compositor → app | Focus changed |
| EVENT | EVT_CLICK | 80 | compositor → app | Mouse click |
| EVENT | EVT_KEY | 81 | compositor → app | Key press/release |
| EVENT | EVT_VALUE | 82 | compositor → app | Widget value changed |
| EVENT | EVT_FOCUS | 83 | compositor → app | Focus gained/lost |
| EVENT | IME_PREEDIT | 96 | IME → compositor | Composition preview |
| EVENT | IME_COMMIT | 97 | IME → compositor | Committed text |

## See Also

- [Architecture](architecture.md) — System overview and data flow
- [Widgets](widgets.md) — All 20 widget types
- [IME Framework](ime.md) — Input method engine design
