---
slug: roadmap
title: Roadmap
role: milestones
updated: "2026-09-15T02:14:18"
---

# Roadmap

See: [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md) · [docs/navigation.md](../docs/navigation.md)

## Endpoint
**A desktop graphics system** — reached rung by rung from the terminal toolkit. Earlier layers are the path, not a ceiling.

## Milestones
- L0: single window + size-notify + input/textarea/button/label + events + resize→relayout
- L1: styles + full 20-widget library + containers + focus/navigation — partial (landed: widgets/containers/nav; gap: preedit overlay)
- L2: multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal, no protocol commands)
- L3: client pixel surface (a widget kind) + binary DCS frame transport + animation
- L4: multi-surface scene compositor (z-order/alpha/transforms) + transport pluggability (local shm/dmabuf zero-copy; remote DCS)
- L5: desktop graphics system — full WM (decorations/layouts/workspaces), GPU path, launch/activate

## Revision
Requirements re-layered and re-scoped per `docs/spec-revision-proposal.md`. WM features (§5.1 decorations/layouts/workspaces) and the full resource subsystem (§5.3) are **deferred to L4–L5, not deleted** — they ARE the desktop endpoint.
