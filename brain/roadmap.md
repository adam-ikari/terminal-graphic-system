---
slug: roadmap
title: Roadmap
role: milestones
updated: "2026-09-15T01:53:46"
---

# Roadmap

See: [docs/spec-revision-proposal.md](../docs/spec-revision-proposal.md) · [docs/navigation.md](../docs/navigation.md)

## Milestones (revised 2026-09-14 — prove the toolkit before the WM)
- Layer 0: single window + size-notify + input/textarea/button/label + events + resize→relayout
- Layer 1: styles + full 20-widget library + containers + focus/navigation — partial (landed: widgets/containers/nav; gap: preedit overlay)
- Layer 2: multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal, no protocol commands)
- Layer 3: pixel surface (a widget kind) + binary frame transport + animation

## Revision
Requirements re-layered and re-scoped per `docs/spec-revision-proposal.md` (§5.1 shrink, §5.3 shrink, §4.2 soften, §5.5 IME cut, §6.1 end-to-end latency, §6.2 app-side fallback).
