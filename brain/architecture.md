---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-16T00:52:25"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md) · [docs/architecture-v2.md](../docs/architecture-v2.md) · [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md)

## Key decisions
- **Character base = libvterm (2026-09-15).** The emulator is not reimplemented: it is **libvterm** (vendored `deps/libvterm`, C99). `src/compositor/term.c` adapts libvterm's screen to the renderer's cell grid and routes its output back to the PTY. Cell size is read from the font, never assumed. **lv_draw_letter anchors a glyph on a pivot at (adv_w/2, line_height - base_line) and rasterises it shifted by -pivot**, so the point must be pre-compensated or every glyph lands half a cell left and a full line up.
- **The program's tty is a terminal's, not a raw pipe (2026-09-15).** Only ICANON and ECHO are cleared. ICRNL stays so Enter is a newline, ISIG so Ctrl-C signals, OPOST|ONLCR so a program's "\n" reaches the screen as CR LF. cfmakeraw() cleared those too, which made plain `echo` output march across the screen and left Ctrl-C dead.
- **Terminal bindings belong to the terminal (2026-09-15).** Shift+PageUp/PageDown scrolls the scrollback; Ctrl+Shift+V pastes. Neither ever reaches the program as keys. Paste goes as text with libvterm's bracketed-paste markers, emitted only when the program enabled CSI ?2004h.
- **Attribute bridge is deliberately partial (2026-09-15).** libvterm has **no faint bit at all**; a 1-bit bitmap font has no italic or blink. The cell carries bold/underline/reverse (plus conceal). Everything else renders at normal intensity — a stated contract, not an oversight.
- **tgs_client_poll_event return contract (2026-09-16).** 0 = an event arrived (*ev filled); 1 = the timeout expired with no event; -1 = the compositor connection ended or failed. A finite-timeout poll MUST treat a quiet timeout as "no event", not an error — the old code folded HUP into timeout and reported both as -1, so any client polling with a finite timeout exited the instant the compositor had nothing to send. Blocking callers (timeout -1) never see 1.
- **L0 is complete and tested in four layers (2026-09-15).** PTY → stream demultiplexer → libvterm → LVGL canvas. Tests follow the tui-testing-debugging skill: (1) state, (2) fixed-size render snapshots in `tests/snapshots/` that state their own frozen contract, (3) virtual-terminal invariants, (4) PTY integration with no display, polling for markers and leaving artifacts on failure. **A wait for a child to exit must be deadline-based**: once the pty is at EOF poll() returns POLLHUP immediately, so counting iterations collapses the window.
- **L1 verified (2026-09-16).** One program prints characters and opens TGS widgets over the same stdout; the stream demultiplexer splits them and the character path stays alive across the HELLO/READY handshake. Probe: `examples/char_and_graphics.c`. The poll_event contract above was the blocker found while verifying it.
- Protocol decoupled from rendering backend (`tgs_backend.h` abstraction)
- SDL2 (desktop) / direct `/dev/fb0` (embedded) display backends; FB present = copy the draw buffer to the mmap
- APC frame format, single-stream multiplexed
- Container-is-a-widget: `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply layout from type at creation; only containers may parent
- Navigation owned by the compositor, not LVGL: per-window rings/scopes, `NTF_FOCUS`+reason, key-routing precedence
- **Character compatibility FIRST (2026-09-14).** Ordinary character programs run unchanged. The base is a *working character terminal*, not a compat mode.
- **One program stream, char + graphics together (2026-09-14).** One byte stream: ordinary bytes → the character region; TGS APC frames → directives.
- **Self-consistency (2026-09-14).** Everything above the server is a terminal program. The window manager is itself a TGS program.
- **Nesting / recursive composition (2026-09-14).** Complete when it nests inside itself.
- **Transport split (2026-09-14).** Embedded images → base64 in APC; framebuffer → binary DCS + length prefix.

## Reversals
- **Product reframe (2026-09-14).** TGS is one toolkit with two render backends; the endpoint is a desktop graphics window system.
- **IME (2026-09-14).** The five IME-specific protocol commands are cut.
