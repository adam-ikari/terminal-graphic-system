---
id: backend-sdl2-skia
title: "Renderer backends: SDL2 + Skia, graphics semantics"
category: decision
status: active
tags: [backends, renderer, skia, sdl2]
created: "2026-09-19T07:36:33"
updated: "2026-09-19T13:03:56"
---

<!-- compiled_truth -->
# Backend re-platform: SDL2 + Skia

## Decision (2026-09-19, user directive)

- The widget protocol is **drawing semantics** (SVG-like scene painting) — final.
- Renderer backends: **SDL2 and Skia are the two selectable graphics backends**.
- LVGL retires from the backend role; its flex/grid engines, widget catalog
  mapping, and deprecated-widget workarounds go with it.
- The presentation layer (`src/compositor/output.h`, output_sdl/output_fb)
  survives: SDL2 backend presents through SDL textures; the Skia backend
  rasterizes into the same `tgs_display` framebuffer and presents identically.

## Facts

- Backend contract is the vtable in `src/common/tgs_backend.h` (42-94):
  init/tick/render/create_window/create_widget/set_widget_rect/content/style/
  events/inject/focus. Renderer-neutral by design — protocol never names a
  toolkit.
- SDL2 2.0.20 present on this box. Skia: no system package (no .pc); needs
  vendored build or fetched artifacts.
- Embedded constraint (200 MHz, no GPU) drives default backend choice;
  SDL2 window is the desktop path, /dev/fb the embedded path.

## Why (compressed)

- Rendering semantics live in the protocol (already settled: 8 primitive
  kinds). Backend choice is invisible to the wire — swapping engines is
  implementation, not protocol work.
- LVGL was the wrong tool for a "scene painter": retained-mode widget tree
  duplicates protocol-side state, and its layout engines are retired policy
  leaks. Skia/SDL are immediate-mode paint engines matching drawing semantics.


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
  summary: "Cutover DONE (2026-09-19): LVGL fully removed (deps/lvgl + src/backends/lvgl deleted, letter/font probes retired). Scene backend (scene_core + scene_backend + paint_sdl2 + term_view) implements the full vtable; 74/74 tests green; d2_repro PASS; screenshots regenerated and vision-verified. Fixes during cutover: inject_mouse contract (button 0 primary, pressed -1 motion-only), window roots excluded from hit-testing, reverse-order child teardown (skip bug), NAV_WIDGET key delivery parity, 4bpp blended glyph compositing, NORMAL-window opaque bg policy."
  source: session
  affects: [backend-sdl2-skia]
