---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-14"
---

# System architecture

See: [docs/architecture.md](../docs/architecture.md)

## Key decisions
- Protocol decoupled from rendering backend (tgs_backend.h abstraction)
- IME is a separate TGS application, not built into compositor
- SDL for desktop, direct /dev/fb0 for embedded
- APC frame format, single-stream multiplexed
