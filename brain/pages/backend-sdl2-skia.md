---
id: backend-sdl2-skia
title: "Renderer backends: SDL2 + Skia, graphics semantics"
category: decision
status: active
tags: [backends, renderer, skia, sdl2]
created: "2026-09-19T07:36:33"
updated: "2026-09-21T01:05:17"
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

- time: 2026-09-20T01:48:32
  kind: decision
  summary: "kitty presentation DONE (2026-09-20): TGS is a superset of kitty — frames ride the kitty graphics APC channel with the G1 sub-namespace marker (ESC _ G1;stream;frame;cmd;args ESC \\), kitty-native G frames share the channel (parser dual-identification). Presentation channel: output_kitty.c encodes the canvas as PNG (f=100, stb_image_write) → base64 → chunked APC (m-chained, 4KB), paced at 30fps (raw RGBA measured 77MB/s — PNG brings it to ~200KB/s). kitty is mandatory, not a compile option (TGS_OUTPUT selects kitty|sdl|fb for the debug/embedded alternates; input layer always built). stb_image_write decoupled from libsixel into deps/ top-level. Verified live: compositor stdout carries valid kitty frames."
  source: user directive
  affects: [backend-sdl2-skia]

- time: 2026-09-20T01:53:00
  kind: decision
  summary: "Spec v2.0 DONE (2026-09-20): protocol/tgs-spec-layer0.md rewritten — drawing semantics, G1 APC dual-identification, no windows/win_id/focus/layout/subscription/geometry. Normative rules: kind admission (renderer-native state or reject), interaction-shaped events only, no subscription gating, kitty presentation pacing (raw RGBA banned). v1.0 archived (tgs-spec-layer0-v1.md.bak). IME spec: separate program, window-level key routing, candidate window is outer WM's job — no PREEDIT/CANDIDATES/SELECT/CANCEL commands."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-20T02:27:46
  kind: evidence
  summary: "Live verification (2026-09-20): kitty round-trip is PIXEL-EXACT — ground truth (render_snapshot direct fb dump) vs kitty presentation capture (compositor stdout APC decode) compare 2242/2242 sampled pixels inside the box, maxdiff=0, antialiased text glyph edges identical (249,249,249). Found+fixed: WGT_CREATE content arg dropped in WM rewrite. Methodology: capture compositor stdout, decode kitty APC per-transmission (a=T boundary groups), reconstruct PNG, pixel compare. 49/49 tests green."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-21T01:05:17
  kind: decision
  summary: "60/120Hz presentation DONE (2026-09-21): stb deflate was 25fps (40ms/frame) — replaced with system zlib level 1, hand-built PNG (IHDR/IDAT/IEND+CRC32): 136fps (7.3ms). Poll loop: 5ms fixed serialized poll+encode to 12ms at 120Hz (74fps); poll now 1ms, presenter owns cadence. TGS_FPS env (default 60, cap 240). Measured: 60→59.8fps, 120→101-102fps, 240→102 (encoder ceiling); all pixel-exact (PIL) 2242/2242 maxdiff=0. Honest ceiling on Ryzen 7 5800H: encode 86% of 120Hz budget — dirty-region is the path to true 120. ALSO: verification script bug found — custom PNG decoder conflated filter 3 (Average) with 4 (Paeth); 88% mismatch was decoder, not protocol; PIL is authoritative."
  source: session
  affects: [backend-sdl2-skia]
