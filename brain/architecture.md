---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-15T09:43:05"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md) · [docs/architecture-v2.md](../docs/architecture-v2.md) · [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md)

## Key decisions
- **Character base = libvterm (decided 2026-09-15).** The terminal emulator is not reimplemented: it is **libvterm** (vendored `deps/libvterm`, C99, the core existing terminals/editors use), so a program sees exactly what it would on an existing terminal — same sequences, same screen model, same key encoding (incl. cursor-key application mode). `src/compositor/term.c` adapts libvterm's screen to the renderer's cell grid and routes its output (DSR replies, encoded keys) back to the PTY. Box-drawing/blocks are synthesized as rects in the LVGL view.
- Protocol decoupled from rendering backend (`tgs_backend.h` abstraction)
- SDL2 (desktop) / direct `/dev/fb0` (embedded) display backends; FB present = copy the draw buffer to the mmap
- APC frame format, single-stream multiplexed
- Container-is-a-widget: `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply layout from type at creation; only containers may parent
- Navigation owned by the compositor, not LVGL: per-window rings/scopes, `NTF_FOCUS`+reason, key-routing precedence
- Edge-queued input: pointer/keypad enqueue transitions only
- **Character compatibility FIRST (decided 2026-09-14).** Ordinary character programs run unchanged (`bash`/`vim`/`htop`, zero TGS awareness). The base is a *working character terminal*, not a compat mode.
- **One program stream, char + graphics together (decided 2026-09-14).** One byte stream: ordinary bytes → the character region; TGS APC frames → directives. Every TGS program is a terminal program.
- **Self-consistency (decided 2026-09-14).** Everything above the server is a terminal program. The compositor is a display server with no WM policy; the window manager is itself a TGS program.
- **Nesting / recursive composition (decided 2026-09-14).** Complete when it nests inside itself.
- **Transport split (decided 2026-09-14).** Embedded images → base64 in APC (stream 2); framebuffer → binary DCS + length prefix (stream 3). Surfaces are transport-pluggable: remote = DCS/PTY, local = shm/dmabuf.

## Reversals
- **Product reframe (2026-09-14).** TGS is one toolkit with two render backends; the endpoint is a desktop graphics window system. Terminal text→graphics is a normal, precedented path; §5.4 framebuffer is NOT a contradiction.
- **IME (2026-09-14).** The five IME-specific protocol commands are cut. For the app, IME input is indistinguishable from stdin.
