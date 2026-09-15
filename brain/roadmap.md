---
slug: roadmap
title: Roadmap
role: milestones
updated: "2026-09-15T05:21:35"
---

# Roadmap

See: [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md) · [docs/architecture-v2.md](../docs/architecture-v2.md)

## Endpoint
**A desktop graphics window system** — reached rung by rung from the character terminal. Earlier layers are the path, not a ceiling.

## Milestones
- **L0 — DONE (2026-09-15): a working character terminal.** The compositor *is* an xterm-class terminal: PTY (grid size set via forkpty's winsize before exec) → stream demultiplexer (text vs TGS APC frames) → VT emulator (`src/compositor/term.c`) → LVGL canvas (`src/backends/lvgl/lvgl_term.c`). Keys return PTY bytes until a program speaks TGS. Verified: `htop` and `ls` run with zero TGS code; `tests/test_term.cpp` covers the emulator + demux; `term_probe` replays captured bytes.
- L1: char + graphics in one program — text and TGS frames interleave on one stream
- L2: single window + size-notify + widget set + events + resize→relayout
- L3: styles + full widget library + containers + focus/navigation + multi-window + resources + IME (internal)
- L4: client pixel surface + binary DCS transport + scene compositor + transport pluggability
- L5: desktop graphics system — a **WM program** + GPU path
- L6: nesting — a TGS server runs as a TGS program inside a TGS surface

## Revision
Requirements re-layered and re-scoped per `docs/spec-revision-proposal.md`.
