---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-14"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md) · [docs/navigation.md](../docs/navigation.md)

## Key decisions
- Protocol decoupled from rendering backend (`tgs_backend.h` abstraction)
- IME is a separate TGS application, not built into compositor
- SDL for desktop, direct `/dev/fb0` for embedded
- APC frame format, single-stream multiplexed
- Container-is-a-widget: `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` apply layout from type at creation; only containers may parent (WM rejects leaf parents)
- Navigation owned by compositor, not LVGL: per-window rings/scopes, `NTF_FOCUS`+reason, key-routing precedence hook (nav owns Tab before LVGL)
- FB present contract: LVGL publishes to draw buffer; compositor copies to mmap — never shares a pointer
- Edge-queued input: pointer/keypad enqueue transitions only
