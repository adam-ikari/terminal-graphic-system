---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-15T02:25:05"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md) · [docs/navigation.md](../docs/navigation.md) · [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md)

## Key decisions
- Protocol decoupled from rendering backend (`tgs_backend.h` abstraction)
- SDL2 (desktop) / direct `/dev/fb0` (embedded) display backends; FB present = copy the draw buffer to the mmap
- APC frame format, single-stream multiplexed
- Container-is-a-widget: `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply layout from type at creation; only containers may parent
- Navigation owned by the compositor, not LVGL: per-window rings/scopes, `NTF_FOCUS`+reason, key-routing precedence
- Edge-queued input: pointer/keypad enqueue transitions only
- **Self-consistency (decided 2026-09-14).** Everything above the server is a terminal program. The compositor is a **display server** — owns the display, renders, routes input, exposes surface/widget primitives, and holds **no WM policy**. The **window manager is itself a TGS program** (a client of the same API, X11-style); apps and the IME are TGS programs. One primitive set, many programs.
- **Transport split (decided 2026-09-14).** Two payload classes, two transports: **embedded images / resource bytes → base64 in the existing APC frame** (text-safe, no new parser, ~+33%); **framebuffer stream → binary DCS with an explicit byte-length prefix** (`ESC P TGSFB;<w>;<h>;<fmt>;<len> ST <len raw bytes> ESC \`) — the length prefix makes raw binary unambiguous, 0% overhead. Surfaces are transport-pluggable: remote = DCS/PTY, local = shm/dmabuf zero-copy.

## Reversals (2026-09-14)
- **Product reframe — REVERSAL.** TGS is **one toolkit with two render backends**, not two products, and the **endpoint is a desktop graphics system**. Default = declarative native widgets (server-side render, small text frames). Opt-in / late = a pixel surface modelled as a **widget** (a drawable region), fed by the binary DCS transport above. Terminal text→graphics is a normal, precedented path (Tektronix / ReGIS / Sixel / kitty); the §5.4 framebuffer is NOT a contradiction — its defects are transport + modelling + staging.
- **IME — REVERSAL.** The five IME-specific protocol commands are cut (`IME_PREEDIT`/`IME_COMMIT`/`IME_CANDIDATES`/`IME_SELECT`/`IME_CANCEL`). For the app, IME input is indistinguishable from stdin: the app sees only `EVT_VALUE_CHANGED`; IME is a compositor-internal input transformation.
