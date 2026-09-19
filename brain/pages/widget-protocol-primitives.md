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

## Reversal (2026-09-18): layouts move to the program — collapse NOW

The "why not collapse now" rationale below is OVERTURNED for the layout trio.
First-principles: the compositor's own charter says "server holds mechanism,
program holds policy". Layout IS policy — the program knows its content and
should compute geometry itself. A renderer that runs flex/grid is a policy
leak: the compositor is doing the program's design work with its own
hard-coded rules.

DONE (2026-09-18): the user sharpened the direction — the widget protocol is
DRAWING semantics, like SVG. The renderer is a scene painter: primitives are
things you draw (box, text, image) and interact with (button, input, toggle,
value). VLAYOUT/HLAYOUT/GLAYOUT are RETIRED (values 9/10 reserved); a plain
TGS_WIDGET_CONTAINER (8) holds children at program-computed rects. SCROLL
survives (scroll offset is viewport mechanism). TGS_LAYOUT_* is demoted to an
optional renderer hint, not the normative layout path. Spec §4.3.1 and §5.2.1
rewritten; container_demo rewritten with program-computed rects; a new
test (ContainerHoldsProgramComputedRects) pins the no-layout contract:
rects round-trip exactly.


L0/L1 shipped the catalog; apps exist. The catalog is already semantic
(button/label/input are primitives by the criterion). The only true
composition-debt is MENU (stub, never shipped a semantic) and the layout trio
(attr-expressible but type-installed). Collapse is a breaking protocol change
deferred until a second backend or L4 forces the payoff.

## Stage 2 (2026-09-18): catalog collapsed to 8 primitive kinds

User directive escalated the primitivization: the widget protocol is DRAWING
semantics (like SVG). The enum now has 8 kinds — BUTTON(0), LABEL(1),
INPUT(2), CHECKBOX(3), SLIDER(5), CONTAINER(8), SCROLL(11), IMAGE(17) —
values 4,6,7,9,10,12-19 reserved. Retired kinds map to primitive
replacements in the compositor decode (wire compat): radio/switch→CHECKBOX,
progress→SLIDER, list/table/menu/tab/dropdown/pickers→CONTAINER.

Derived looks are style/attr compositions: radio = round CHECKBOX (RADIUS
style), "switch" = wide capsule CHECKBOX, "progress" = SLIDER whose drag the
app ignores. Compound widgets (list/table) = CONTAINER + child widgets at
program-computed rects — this also fixed the old TABLE cell(0,0)-only
limitation structurally (cells are real widgets with their own ids/events).

Blocked primitives for full derivation: POPUP (dropdown/menu/pickers) and
raw pointer coordinates for app-drawn surfaces (L4 pixel surface). Both are
L4-scoped; until then these compositions stay app-side approximations.

Events remain interaction-shaped. Spec §5.2 rewritten (drawing semantics +
retired-value remap table), §5.2.1 rules kept, §5.3 gains HOVER rows.
