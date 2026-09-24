---
id: backend-sdl2-skia
title: "Renderer backends: SDL2 + Skia, graphics semantics"
category: decision
status: active
tags: [backends, renderer, skia, sdl2]
created: "2026-09-19T07:36:33"
updated: "2026-09-24T04:36:25"
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

## Pacing & redraw (2026-09-24)

- **output_present SLEEPS to its anchor instead of dropping early calls**:
  `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, last+interval)`; an
  early wake (EINTR) clamps `now` to `due` so the schedule never pulls
  forward. Dropping quantized presents onto compositor iteration
  boundaries (interval = ceil(gate/T)*T; measured 17.8/9.38/6.27ms at
  gate 60/120/240 ⇒ T≈3ms, 106.6fps under a 125Hz ceiling). Anchor sleep
  is hrtimer-accurate and independent of T: measured gate120 → 123.7fps,
  gate240 → 245.3 (cap 250), gate60 → 62.2 (cap 62.5). Trade-off: input
  is serviced only between anchors, latency ≤ one interval (8ms@120,
  16ms@60) — frame-buffering semantics, the price of a punctual present
  in a single loop.
- **term_view redraw cuts** (all pixel-identical, cross-build composite
  480000/480000 maxdiff=0): (cp,fg) → SDL_Surface glyph cache via 1024-
  slot open addressing (render is deterministic ⇒ cached bytes equal a
  fresh rasterisation; full-grid redraw went from 3700
  TTF_RenderUTF8_Blended calls to a few dozen); DEF_BG prefill as a
  bounds-check-free store loop (was 473k guarded px_set calls/frame);
  blend's /255 as a 65536-entry table filled with the same C division —
  rounding is part of the pixel contract (a reciprocal approximation can
  move an antialiased edge by 1), numerators bounded ≤ 255*255=65025.
- **Honest residual**: full-screen churn = 109.9fps — present work ≈9.1ms
  > 8ms gate (parse+full redraw ≈5.6ms + ~50 dirty tiles encoded
  ≈3.5ms). Real per-frame work now, not cadence quantization.

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

- time: 2026-09-23T04:00:00
  kind: decision
  summary: "Dirty-region presentation design (2026-09-23): the canvas is a fixed grid of pixel tiles (64x64), each tile a persistent kitty image id (base 0x74670000); a present re-transmits ONLY tiles whose bytes differ from the prev-copy (per-tile memcmp), an idle frame writes 0 bytes. Why tiles, not a single dirty rect: kitty deletes an image's data together with its placements, so an update-by-replace of one rect reverts whatever that id previously covered — only a non-overlapping partition is both bounded (memory = one canvas) and self-consistent (tiles never overlap, so z-order never matters). Placement is cell-based in kitty: CUP to (x/cell_w, y/ch) + X/Y pixel offsets (must be < cell size), a=T,i=<id>,C=1. Host cell size from TIOCGWINSZ (ws_xpixel/ws_col, ws_ypixel/ws_row), TGS_CELL_W/TGS_CELL_H env override for pty-less capture; no cell size → full-frame fallback (which now also skips unchanged frames). Resize/cleanup deletes the id range with a=d,d=R,x=,y=."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-24T01:47:54
  kind: evidence
  summary: "Dirty-tile presentation MEASURED (2026-09-24): 64x64 tile grid / persistent kitty ids (base 0x74670000) shipped; idle = 0 B/s (tile and full-frame fallback both, 0 stray bytes); localized dirty @gate120 = 106.6fps / 117 KB/s vs full-frame 83.7fps / 1.24 MB/s (fps +27%, bandwidth 1/10.6); unpaced flood = 99.5fps / 116 KB/s vs 83.3 / 1.25 MB/s; full-screen churn (worst case, all 130 tiles dirty) = 94.3fps / 1.72 MB/s vs 56.2 / 3.06 MB/s; gate240 = 160.0fps / 172 KB/s vs 88.3 / 1.28 MB/s — breaks the old full-frame 102fps ceiling at ANY gate, proving encode left the critical path; gate60 sanity = 56.2fps. yes flood = 0 B/s correctly (screen pixel-identical, no dirty tile — not a worst case; churn scenario replaced it). Pixel (PIL authoritative): idle vs ground truth rect(20,20,600,200) = 120000/120000 100% maxdiff=0; cross-mode char scene tile-composite vs full-composite = 480000/480000 100% identical diff bbox=None; glyphs antialiased (198 colors, 53k non-bg px). ctest 51/51 (9 suites; +test_kitty_dirty). Three pixel-only bugs fixed: (1) presenter encoded priv->fb not published d->buffer (scene republishes in backend_init) -> all-zero first frame; (2) output_resize compared d->width already moved by set_size -> always early-return stale grid, now compares priv->w/h; (3) blit_cp only handled BytesPerPixel==1 but TTF_RenderUTF8_Blended returns 32bpp -> glyphs rendered then dropped, fixed with blend_cp fg-over-cell-bg (text-grid snapshot tests are blind to this — grid content is correct, only pixel diff catches it). pty_capture teardown deadlock fixed (child blocked mid-write in full pty, SIGTERM+SA_RESTART never lands -> WNOHANG drain + 2s SIGKILL). Honest residual: at gate 120 (integer ms -> 8ms -> 125 cap) measured 106.6fps — limiter is now compositor per-iteration full-canvas redraw (view redraw + scene_draw memcpy per dirty tick), not the encoder."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-24T03:30:35
  kind: decision
  summary: "Pacing + redraw truth: present gate sleeps to its anchor; glyph-cache/defill/255-table redraw cuts are pixel-identical"
  source: brain update-truth
  affects: [backend-sdl2-skia]

- time: 2026-09-24T03:30:51
  kind: evidence
  summary: "Anchor-scheduled present + redraw cuts MEASURED (2026-09-24): replaced output_present's skip-when-early gate with clock_nanosleep(TIMER_ABSTIME, last+interval), so presents land on schedule anchors instead of compositor iteration boundaries (old interval = ceil(gate/T)*T, T≈3ms). Same-scenario serial A/B on this machine (old gate build vs patched, 10s captures): counter@g120 101.8→123.7fps (109→133 KB/s), unpaced flood@g120 98.1→123.8 (112 KB/s→140), full-frame fallback@g120 84.2→122.3fps (1.22→1.77 MB/s), full-screen churn@g120 71.7→109.9fps (1.56→2.37 MB/s), gate240 159.3→245.3 (cap 250), gate60 56.4→62.2 (cap 62.5) — gate probes now hit their caps, proving cadence comes from the anchor. Work-bound residual attacks in term_view.c: (cp,fg) glyph-surface cache (3700 TTF renders/redraw → dozens), bounds-check-free DEF_BG fill, /255 lookup table pre-filled with the same C division (exact quotient; reciprocal approx would move AA edges). Pixel (PIL authoritative): idle vs ground rect(20,20,600,200) 120000/120000 100% maxdiff=0, 0 stray, 0 B/s steady; cross-build composites old vs new (idle + static char scene, tile and fallback each) all 480000/480000 maxdiff=0 — anchor sleep, glyph cache and /255 table moved zero pixels; cross-mode tile vs fallback 480000/480000; 231 distinct colors (AA alive). ctest 51/51 (snapshot suites pixel-unchanged). Replay script hardened: APC truncated at EOF (capture deadline cut mid-frame) reported as truncated tail instead of raising; a later ESC still raises (mid-stream desync = real fault). Honest residual: churn 109.9fps stays <120 — present work ≈9.1ms > 8ms gate (parse+full redraw ≈5.6ms + ~50 dirty-tile encode ≈3.5ms); that is real per-frame work, not quantization. Input latency bound is now one interval (8ms@120), the cost of a punctual present in a single loop."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-24T03:54:55
  kind: decision
  summary: "kitty native G-frame landing — design settled (2026-09-24): parser routes 'G1;' to TGS, every other APC 'G…' payload to a new kitty_native module (buf 4096→8192 so one 4KB-binary kitty chunk, ~5.5KB base64, fits). Semantics (spec §8 superset face, reference implementation): a=t/T/p/d, f=100 (stb_image, IHDR precheck before decode: ≤8192/axis, ≤4M px, payload ≤8MB), f=32/24 raw exact-size; chunked m=1/0 assembly (single stream); 64 image slots keyed by i=, auto-id from 0x40000000 when i absent; placement = cursor-cell snapshot (tgs_term_cx/cy) + X/Y in-cell pixel offset, c/r = display SIZE in cells (nearest-neighbour scale, aspect-derived when only one given), x/y/w/h on a=T/p = display crop (raw partial transmit rejected EINVAL — documented), z + insertion order draw sequence. Z-ORDER (load-bearing): images blit into the term-view px AFTER glyphs and BEFORE scene render — over the character base, under widget/scene elements, matching the fullscreen-window-z-order decision; drawn whenever base repaints (term dirty OR image dirty). ACKs: responses written to the app pty (poll POLLOUT=0 + write, drop when full — never blocks the loop) exactly when i= is present, q=0 → OK, q=1 → errors only, q=2 → silent; codes OK/ENOENT/EINVAL/ENOTSUPPORTED; a=q answers validity without PNG decode (kitty behavior). OUT OF SCOPE (documented known gaps): U=1 unicode-placeholder virtual placements, animation (a=a/f=), zlib compression, multi-placement per image (p=), cursor movement policy C=0 (never moves the vterm cursor — divergence), placement scroll-coupling (fixed at snapshot cell, text scrolls under), BMP/GIF formats. Element-attached pixel sources (GRAPHIC fill by image id) remain L4 per spec §8.3."
  source: session
  affects: [backend-sdl2-skia]

- time: 2026-09-24T04:36:25
  kind: evidence
  summary: "kitty-native G-frame landing DONE (2026-09-24): commit 8681252 — bare kitty frames (a=t/T/p/d + a=q answer, f=100 PNG via stb_image with IHDR precheck ≤8192/axis ≤4M px ≤8MB payload, f=32/24 raw exact-size, m=1..0 single-stream base64 assembly incl. non-quantum splits, i= 64 slots auto-id ≥0x40000000 coldest-placement eviction) decode and place pixels on the canvas: blit into term-view px after glyph draw before scene render — over char base, under scene elements (z-order load-bearing), re-blitted on any base repaint. Placement = cursor-cell snapshot + X/Y in-cell offset; c/r = display size in cells (NN scale, aspect-derived when one given); x/y/w/h crop only on placement-only frames (data-frame w/h are raw dims; x/y there = partial transmit → ENOTSUPPORTED); order (z, seq); re-transmit same id resets placement. ACKs to app pty only when i= present, q=0/1/2 quiet table, OK/ENOENT/EINVAL/ENOTSUPPORTED, poll(POLLOUT,0)+write drop-when-full. Parser buf 4096→8192 (4KB-binary chunk b64 ≈5.5KB must fit). LIVE-CAUGHT BUG (TGS_RAW_DUMP): ack queued BEFORE the handshake made tgs_client treat any non-G1 APC payload as a stream error → client init failed, widgets absent, composite DEF_BG-only — dual identification had been done only compositor-side. Fixed: client-side same split (consume+skip non-G1 G-frames; G1 that fails decode stays fatal; event path already tolerant), pinned by the ClientInput test (ack-before-READY ordering); spec §8.1 amended to normative BOTH-ENDS rule. Also fixed latent term_resize_to: recreated view never repointed scene underlay (stale freed pointer) + image dirty mark. Verified PIL-authoritative: ctest 67/67 (+16: 15 kitty_native + 1 ClientInput); e2e char scene image 16px + 7 base probes exact, idle 0 B/s (no repaint storm); widget occlusion — native frame sent before container_demo start, image inside root BOX → composite vs render_snapshot ground 120000/120000 = 100% maxdiff=0 (z-order pixel-pinned, and live regression for the client fix). Known gaps recorded spec §8.3: U=1, animation, zlib, p>0, C=0 cursor policy (never moves vterm cursor), scroll coupling (snapshot cell fixed), BMP/GIF, partial raw, t=/o= targets; element-fill GRAPHIC source stays L4; image layer re-blits with full base repaint (no image-level dirty yet); single-stream assembly."
  source: session
  affects: [backend-sdl2-skia]
