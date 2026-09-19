---
id: backend-sdl2-skia
title: "Renderer backends: SDL2 + Skia, graphics semantics"
category: decision
status: active
tags: [backends, renderer, skia, sdl2]
created: "2026-09-19T07:36:33"
updated: "2026-09-19T15:22:43"
---

<!-- compiled_truth -->
# Backend re-platform: SDL2 + Skia

## Decision (2026-09-19, user directive)

- **控件语义放弃，追求图形原语** — the widget protocol is DRAWING semantics
  (SVG-like scene painting), final. The protocol names only drawing
  primitives (8 kinds); there is no widget catalog on the wire.
- Renderer backends: **SDL2 and Skia are the two selectable graphics
  backends**, both reference implementations behind the same paint port.
- The old toolkit-based backend (widget tree, layout engines) was removed
  entirely — the renderer holds no control semantics.
- The presentation layer (`src/compositor/output.h`, output_sdl/output_fb)
  survives: the backend publishes its framebuffer; output_present() pushes
  it to SDL textures or mmap'd /dev/fb0.

## Facts

- Backend contract is the vtable in `src/common/tgs_backend.h` (42-94):
  init/tick/render/create_window/create_widget/set_widget_rect/content/style/
  events/inject/focus. Renderer-neutral by design — protocol never names a
  toolkit.
- Skia is vendored at /home/gem/deps/skia (minimal libskia.a, no GPU);
  SDL2 + SDL2_ttf are system packages.
- Embedded constraint (200 MHz, no GPU) drives default backend choice;
  SDL2 window is the desktop path, /dev/fb the embedded path.

## Why (compressed)

- Rendering semantics live in the protocol (8 primitive kinds). Backend
  choice is invisible to the wire — swapping engines is implementation,
  not protocol work. A retained widget toolkit duplicates protocol-side
  state and drags layout policy into the renderer; both violate the
  "server holds mechanism, program holds policy" charter.


## Timeline

- time: 2026-09-19T07:36:33
  kind: decision
  summary: "Created this page: Renderer backends: SDL2 + Skia, graphics semantics"
  source: created via brain create-page
  affects: [backend-sdl2-skia]

- time: 2026-09-19T07:36:33
  kind: decision
  summary: Backend re-platform
  source: brain update-truth
  affects: [backend-sdl2-skia]

- time: 2026-09-19T13:03:56
  kind: decision
  summary: "Cutover DONE (2026-09-19): the old toolkit backend fully removed (the vendored toolkit and its backend module deleted, letter/font probes retired). Scene backend (scene_core + scene_backend + paint_sdl2 + term_view) implements the full vtable; 74/74 tests green; d2_repro PASS; screenshots regenerated and vision-verified. Fixes during cutover: inject_mouse contract (button 0 primary, pressed -1 motion-only), window roots excluded from hit-testing, reverse-order child teardown (skip bug), NAV_WIDGET key delivery parity, 4bpp blended glyph compositing, NORMAL-window opaque bg policy."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-19T14:19:16
  kind: note
  summary: "Skia paint port DONE (2026-09-19): paint_skia.cpp implements the paint port over SkCanvas (anti-aliased rrects, SkFont via SkFontMgr_New_Custom_Directory + FreeType, save/clipRect clip pairs, direct underlay memcpy). Built behind -DTGS_USE_SKIA against a locally built minimal libskia.a (no GPU/extras) at /home/gem/deps/skia. 74/74 tests green on BOTH backends; Skia library render vision-verified. API notes: this Skia milestone has no SkTypeface::MakeFromFile — use the font manager; no SkColor4f on SkPaint — setColor(SkColor)."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-19T14:38:56
  kind: decision
  summary: "Decision (2026-09-19): control semantics is abandoned; graphics primitives are the protocol and the renderer. No widget catalog on the wire (8 drawing-primitive kinds), the renderer is a scene painter with two reference paint ports (SDL2, Skia), and all engine history is purged from docs/brain — the record keeps only the principle."
  source: user directive
  affects: [backend-sdl2-skia]

- time: 2026-09-19T15:22:43
  kind: decision
  summary: "BUTTON retired (2026-09-19): activation is program policy, not a renderer primitive. A button = CONTAINER + LABEL child + CLICK the program consumes. Value 0 reserved; wire-compat decode maps button/0 to CONTAINER. 7 primitive kinds remain: LABEL, INPUT, CHECKBOX, SLIDER, CONTAINER, SCROLL, IMAGE. 74/74 green on both backends; vision-verified renders."
  source: user directive
  affects: [backend-sdl2-skia]
