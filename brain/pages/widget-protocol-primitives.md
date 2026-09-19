---
slug: widget-protocol-primitives
title: Widget protocol: primitive-first design
role: decision
category: decision
tags: [protocol, widgets, primitives, design]
updated: "2026-09-18"
---

# Widget protocol: primitive-first design

First-principles review of the widget-type protocol. Criterion: a widget type is a
PRIMITIVE iff the renderer must natively hold state or capability that no
composition of other protocol primitives can express. Otherwise it is DERIVED and
must eventually be expressible as composition, so the protocol stays small and
neutral.

## Verdict per type (20 existing)

PRIMITIVE (keep as type — native state/capability):
- LABEL, BUTTON (activation), INPUT (text edit state + IME eligibility),
  CHECKBOX (toggle state), SLIDER (draggable value + range state),
  PROGRESS (read-only value display), SWITCH (toggle with animation),
  IMAGE (pixel buffer source — gated on L3 P5 resources),
  SCROLL (viewport state: scroll offset, arrow-consumption semantics)

DERIVED-BUT-KEPT (composition of primitives, kept as convenience types with an
exit criterion — see rule below):
- VLAYOUT / HLAYOUT / GLAYOUT — pure arrangement, no leaf state; could be
  container + layout attr (FLEX_COL/FLEX_ROW/GRID already exist as TGS_LAYOUT_*).
  Kept because type-driven creation is the L0 contract (layout at WGT_CREATE).
- RADIO — CHECKBOX + visual policy (exclusivity is app policy per §5.2).
- LIST — SCROLL + repeated rows; kept for row-append semantics.
- MENU — composition; known stub, no native state. Candidate for deprecation
  into composition once P2 builds it.
- TAB — pager container: header strip + page stack. Native page-switch state.
- DROPDOWN — collapsed list + popup surface (needs popup/compositor support).
- TIMEPICK / DATEPICK — structured value widgets: native parser/validator state.

## The rule going forward

1. New widget types MUST state their primitive state, or be rejected as
   composition (build them in the app from existing primitives).
2. Derived convenience types must not grow behavior a primitive cannot reach
   via style/attr/layout — otherwise the protocol forks into per-widget
   special cases (the toolkit trap).
3. Event model stays interaction-shaped (CLICK/VALUE/KEY/HOVER/FOCUS), never
   widget-shaped — this is what keeps the protocol renderer-neutral.

## Why not collapse now

L0/L1 shipped the catalog; apps exist. The catalog is already semantic
(button/label/input are primitives by the criterion). The only true
composition-debt is MENU (stub, never shipped a semantic) and the layout trio
(attr-expressible but type-installed). Collapse is a breaking protocol change
deferred until a second backend or L4 forces the payoff.
