---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-15T13:11:38"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md) · [docs/architecture-v2.md](../docs/architecture-v2.md) · [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md)

## Key decisions
- **Character base = libvterm (2026-09-15).** The emulator is not reimplemented: it is **libvterm** (vendored `deps/libvterm`, C99), so a program sees what it would on an existing terminal — same sequences, screen model, and key encoding (incl. cursor-key application mode). `src/compositor/term.c` adapts libvterm's screen to the renderer's cell grid and routes its output (device replies, encoded keys) back to the PTY. Cell size is read from the font, never assumed. **lv_draw_letter anchors a glyph on a pivot at (adv_w/2, line_height - base_line) and rasterises it shifted by -pivot**, so the point must be pre-compensated or every glyph lands half a cell left and a full line up.
- **The program's tty is a terminal's, not a raw pipe (2026-09-15).** Only ICANON and ECHO are cleared (protocol frames arrive byte-for-byte; the compositor never reads its own writes back). ICRNL stays so Enter is a newline, ISIG so Ctrl-C signals, OPOST|ONLCR so a program's "\n" reaches the screen as CR LF. cfmakeraw() cleared those too, which made plain `echo` output march across the screen and left Ctrl-C dead.
- **L0 is complete (2026-09-15).** The compositor is a working character terminal: PTY → stream demultiplexer (text vs TGS APC frames) → libvterm → LVGL canvas. Keys, mouse and focus reach the program; the window resizes the display, the grid and the program (TIOCSWINSZ → SIGWINCH); Shift+PageUp/PageDown scroll a 1000-line scrollback; the cursor honours DECSCUSR (block by default). BEL is deliberately silent.
- Protocol decoupled from rendering backend (`tgs_backend.h` abstraction)
- SDL2 (desktop) / direct `/dev/fb0` (embedded) display backends; FB present = copy the draw buffer to the mmap
- APC frame format, single-stream multiplexed
- Container-is-a-widget: `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply layout from type at creation; only containers may parent
- Navigation owned by the compositor, not LVGL: per-window rings/scopes, `NTF_FOCUS`+reason, key-routing precedence
- **Character compatibility FIRST (2026-09-14).** Ordinary character programs run unchanged (`bash`/`vim`/`htop`, zero TGS awareness). The base is a *working character terminal*, not a compat mode.
- **One program stream, char + graphics together (2026-09-14).** One byte stream: ordinary bytes → the character region; TGS APC frames → directives. Every TGS program is a terminal program.
- **Self-consistency (2026-09-14).** Everything above the server is a terminal program. The compositor is a display server with no WM policy; the window manager is itself a TGS program.
- **Nesting / recursive composition (2026-09-14).** Complete when it nests inside itself.
- **Transport split (2026-09-14).** Embedded images → base64 in APC (stream 2); framebuffer → binary DCS + length prefix (stream 3). Surfaces are transport-pluggable: remote = DCS/PTY, local = shm/dmabuf.

## Reversals
- **Product reframe (2026-09-14).** TGS is one toolkit with two render backends; the endpoint is a desktop graphics window system. Terminal text→graphics is a normal, precedented path; §5.4 framebuffer is NOT a contradiction.
- **IME (2026-09-14).** The five IME-specific protocol commands are cut. For the app, IME input is indistinguishable from stdin.
