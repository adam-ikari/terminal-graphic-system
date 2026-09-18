# L3 Survey: Styles + Widget Library + Containers + Multi-window + Resources + IME (READ-ONLY)

**Headline:** L3 is **mostly-implementation, with one fully-greenfield area**. Five of the six
sub-areas already have real, wired code whose gaps are specific and localized: every one of the 20
widget types maps to a live LVGL object, 8 of 9 style props render, all four container types run the
native LVGL layout engines, per-window LVGL roots/groups + pointer activation work, and IME
arm/disarm/cancel/commit works end-to-end with a Direct-Input reference engine. The two real
construction sites are **resources — `TGS_STREAM_RESOURCE` (2) is declared and used by nothing**
(a full greenfield subsystem), and the **IME candidate list** (`TGS_CMD_IME_CANDIDATES`/`IME_SELECT`
have no compositor dispatch at all). The well-documented §H.2 preedit-overlay gap is small and
exactly specified. The largest *silent* defects are the `TGS_LAYOUT_GRID → flex-row` stub in runtime
`WGT_LAYOUT`, `TGS_STYLE_FONT_SIZE` as a no-op, and `TGS_CMD_IME_PREEDIT` as a no-op stub — all
three are in `lvgl_backend.c` / `window_manager.c` today.

---

## A. Styles

| | Evidence |
|---|---|
| **Exists** | `TGS_STYLE_*` enum (9 props) `src/common/tgs_protocol.h:143-154`; `TGS_CMD_WGT_STYLE` (34) dispatched `window_manager.c:641-650` → `be->set_widget_style`; `backend_set_widget_style` `lvgl_backend.c:548-582`; client `tgs_client_set_widget_style` `tgs_client.c:325-340` |
| **Implemented** | `BG_COLOR`, `FG_COLOR`, `RADIUS`, `BORDER_WIDTH`, `BORDER_COLOR`, `SHADOW_WIDTH`, `SHADOW_COLOR`, `OPACITY` — all map 1:1 to LVGL style calls on the object's MAIN part (`lvgl_backend.c:553-577`) |
| **Stubbed** | `TGS_STYLE_FONT_SIZE` is a **silent no-op** (`lvgl_backend.c:578-580`, comment "LVGL font size is fixed at compile time; skip"). Only `LV_FONT_MONTSERRAT_14` is compiled in (`lv_conf.h:85-103`); the char base uses `lv_font_unscii_8` (`lvgl_term.c:14`). There is no runtime font switching, no way to name a font, and no scalable-font path |
| **Target (L3 "styles")** | All declared props visibly render; styles are queryable/reportable; font size actually changes text size |
| **Gap** | (1) `FONT_SIZE` no-op — needs ≥2 built-in fonts + a per-widget font selection path (ties into Resources, §E). (2) **No style query/report channel exists in the protocol at all** — styles are write-only fire-and-forget; an app cannot read back what it set, and there is no `GET_STYLE`/`NTF_STYLE`. (3) Styles apply only to the LVGL MAIN part; e.g. `FG_COLOR` set on a button does not restyle its child label (`set_widget_style` targets the object itself, not children). (4) No style *reset/theme* command. (5) No test asserts any style renders (`tests/` never mentions `TGS_STYLE_`) |
| **Effort** | **Small–medium, mostly implementation.** 8/9 props done; the real work is a font mechanism (ties to Resources) + a query/report command + one visual test |

## B. Full widget library

All 20 `tgs_widget_type` values (`tgs_protocol.h:108-130`) are accepted by `str_to_widget_type`
(`window_manager.c:446-476`) and get a live LVGL object in `create_lvgl_widget`
(`lvgl_backend.c:173-235`). Per-widget status:

| Widget | LVGL mapping | Functional? | Notes |
|---|---|---|---|
| BUTTON | `lv_button_create` (`:178`) | **Yes** | label child auto-created on content (`:476-485`); CLICK via `lvgl_event_handler` |
| LABEL | `lv_label_create` (`:179`) | **Yes** | |
| INPUT | `lv_textarea_create` (`:180`) | **Yes** | VALUE_CHANGED carries text (`:760-767`); KEY transport + IME target |
| CHECKBOX | `lv_checkbox_create` (`:181`) | **Yes** | |
| RADIO | `lv_checkbox` + round radius (`:184-188`) | **Partial** | renders/toggles, but **no mutual-exclusion group** — LVGL has no radio group, and nothing else implements exclusivity |
| SLIDER | `lv_slider_create` (`:182`) | **Yes** | VALUE_CHANGED; arrow-consumer (`nav.c:43-53`) |
| PROGRESS | `lv_bar_create` 0–100 (`:189-193`) | **Yes** | content string = numeric value (`:495-497`) |
| SWITCH | `lv_switch_create` (`:183`) | **Yes** | |
| VLAYOUT | flex column (`:212-217`) | **Yes** | see §C |
| HLAYOUT | flex row (`:218-223`) | **Yes** | |
| GLAYOUT | grid, fixed 2×4 template (`:224-227`, `:156-171`) | **Partial** | real grid, but template is hardcoded (`GRID_COLS 2`, `GRID_ROWS 4`); no protocol to set columns/rows |
| SCROLL | scrollable container (`:228-231`) | **Yes** | no scroll-position control, no scroll events |
| LIST | `lv_list_add_button` per content call (`:498-506`) | **Yes** | deprecated LVGL API, but works; appends rows |
| TABLE | `lv_table_create` (`:200`) | **Stub** | content mapping writes **only cell (0,0)** (`:507-509`) — no row/col addressing in the protocol |
| MENU | `lv_menu_create` (`:201-206`) | **Stub** | created, but content hits the default no-mapping branch (`:529-531`) — cannot populate items |
| TAB | `lv_tabview`; content adds a tab (`:510-513`) | **Yes** | |
| DROPDOWN | `lv_dropdown`; newline options (`:514-517`) | **Yes** | |
| IMAGE | `lv_image_create` (`:209`) | **Stub** | created, but **nothing can load pixels**: all LVGL image decoders disabled (`lv_conf.h:66-73`), no resource stream (§E), no content mapping (`:529-531`) — renders empty |
| TIMEPICK | `lv_roller` options (`:518-520`) | **Yes** | |
| DATEPICK | `lv_calendar`, content "y,m,d" (`:521-526`) | **Yes** | |

Container policy is enforced: a widget may only be parented to a window or an explicit container
(`window_manager.c:583-600`), matching the spec's "only containers may have children"
(`protocol/tgs-spec-layer0.md:118`).

**Gap:** library is ~85% wired. The stubs are IMAGE (needs Resource path), TABLE (needs row/col
addressing — new protocol args), MENU (needs an item API), RADIO (needs exclusive-group semantics).
No automated test creates widgets through the real backend and asserts rendering/events (only
`test_lvgl_click.cpp` and `nav_probe.c` touch widgets; `nav_probe.c` is **not** in ctest).

**Effort:** **Small.** Per-stub work is localized and each has an LVGL-native completion path.

## C. Containers

| | Evidence |
|---|---|
| **Exists** | Type-driven layout at creation: VLAYOUT/HLAYOUT set flex flow, GLAYOUT sets a grid descriptor array, SCROLL sets scrollable (`lvgl_backend.c:212-231`). New children of a grid parent are placed into the next free cell (`:448-450`, `grid_place_child` `:163-171`). Parent-of-container nesting works via `parent_id` (demo: `examples/container_demo.c:40-54`). After the LVGL tick the compositor reports real absolute geometry via `NTF_GEOMETRY` (69) for every container and its directly-laid-out children (`wm_flush_geometry` `window_manager.c:409-444`, gated on `geom_pending`; called from `main.c:437`); client caches it (`tgs_client.c:445-459`, `tgs_client_get_widget_geometry`) |
| **Runtime relayout** | `TGS_CMD_WGT_LAYOUT` (37) → `backend_set_widget_layout` (`lvgl_backend.c:584-603`): `FLEX_ROW`/`FLEX_COL` real, **`TGS_LAYOUT_GRID` is stubbed to `LV_FLEX_FLOW_ROW`** (`:596-598`) — a runtime switch to GRID silently becomes a row flex. Sets `geom_pending` so geometry is re-reported (`window_manager.c:662-663`) |
| **Target (L3 "containers")** | Containers actually arrange children (they already do via LVGL's engines); GRID at runtime must be real; NTF_GEOMETRY reports each child's settled rect |
| **Gap** | (1) Runtime `TGS_LAYOUT_GRID` stub. (2) Grid template fixed 2-col/4-row — no protocol to size columns/rows/span. (3) No per-child flex grow/order, no gap/margin/align control (LVGL supports these; nothing exposes them). (4) SCROLL: no way to set/view scroll offset, no scroll-value event. (5) Container test coverage is thin (`test_geometry.cpp` drives the *client* side of NTF_GEOMETRY over pipes; no test asserts LVGL actually stacks/grids children) |
| **Effort** | **Small.** The layout engines are LVGL-native and already wired; the work is exposing grid config + fixing the runtime GRID stub |

## D. Multi-window

| | Evidence |
|---|---|
| **Exists** | Per-window LVGL root (100%×100% of the screen) + dedicated group (`backend_create_window` `lvgl_backend.c:379-419`); ring membership per window (`backend_set_window_ring` `:614-633`); keyboard hand-off to one active window (`backend_set_active_window` `:638-644`); compositor `activate_window` (`window_manager.c:128-154`) emits the lost/gained `NTF_FOCUS` pair and switches the LVGL keyboard. Pointer-click activation routes through `TGS_EVENT_FOCUS` → `activate_window` when the click lands on a widget of a background window (`:868-871`). WIN_CREATE activates the newest window (`:537`). Per-window focus restore: `nav_restore` (`:148`), successor via `nav_successor`/`nav_other_window` (`nav.h:129,138`). Focus reasons incl. `WINDOW_ACTIVATE`/`WINDOW_RESTORE`/`HIDDEN` (`tgs_protocol.h:79-80`) |
| **Not there** | **No keyboard window switch**: `wm_nav_key` (`window_manager.c:252-322`) handles Tab/Ctrl+Tab (widget nav hatch), arrows, Home/End — there is **no Alt+Tab / Alt+`n` branch** and no window-ring cycling key, even though `TGS_MOD_ALT` (0x04) exists and `nav_other_window` is available. §G.1 of `docs/navigation.md` explicitly defers "keyboard window switching". **No window state**: `TGS_CMD_NTF_STATE` (67) is declared (`tgs_protocol.h:50`) but **never emitted**; there is no minimize/maximize/hide *command* at all and no `WIN_STATE` setter, so the `HIDDEN` reason is dead. No z-order control beyond "newest on top". No resize notification to existing windows: `send_resize` fires only at WIN_CREATE (`window_manager.c:535`); a terminal resize updates the compositor's cached size (`wm_set_display_size`, `main.c:120`) but no `NTF_RESIZE` is broadcast to already-created windows |
| **Target (L3 "multi-window")** | Two+ windows coexist with independent focus rings; switching works by pointer *and* keyboard; each window restores its focus; per-window state is observable |
| **Gap** | Alt+Tab (or equivalent) in `wm_nav_key` + a window-ring cycle; a window-state command/notify pair; per-window resize broadcast. The per-window plumbing (roots, groups, activation, restore) is all present and working |
| **Effort** | **Small–medium.** Keyboard switch ≈ one `wm_nav_key` branch using `nav`'s window list; state command ≈ a small protocol addition + `NTF_STATE` emitter. No architecture change |

## E. Resources

| | Evidence |
|---|---|
| **Exists** | `TGS_STREAM_RESOURCE (2)` is declared (`tgs_protocol.h:18`) and described in the spec as "Image/font resource transfer" (`protocol/tgs-spec-layer0.md:50`) and in `docs/architecture-v2.md:149` as **base64-in-APC**. **Nothing else.** `grep TGS_STREAM_RESOURCE` over `src/` → zero hits. No resource command IDs, no client API, no compositor dispatch, no backend load call, no cache |
| **Target (L3 "resources")** | At minimum: an app uploads a named asset (image; later fonts/themes), the compositor caches it, and a widget (IMAGE; later FONT_SIZE) references it by id |
| **Gap** | **Fully greenfield.** A minimal L3 needs: (1) resource commands (e.g. `RES_LOAD`/`RES_FREE` with an id + type + base64 payload on stream 2; base64 decoder — none exists in `src/`); (2) a compositor-side resource table + lifetime tied to the client; (3) backend entry points `set_widget_image(handle, resource)` and a font loader — which requires enabling an LVGL image decoder (all decoders are OFF: `lv_conf.h:66-73`) and either compiling a second font or a raster image path; (4) the IMAGE widget content path (§B) and `TGS_STYLE_FONT_SIZE` (§A) both hang off this. No tests, docs of the mechanism, or examples exist |
| **Effort** | **Medium–large; the biggest single L3 workstream.** Most of it is new protocol + new compositor subsystem + LVGL image/font enablement. A *bounded* slice (one image type, e.g. raw RGBA or a small embedded PNG via LVGL's PNG decoder, + a fixed builtin-font id) keeps it realistic for L3 |

## F. IME (internal)

| | Evidence |
|---|---|
| **Implemented** | Full route: `main.c:318-330` forks the IME app (`ime_app`, a separate TGS process, `examples/ime_app.c`); HELLO with `ime=true` marks `ime_connected` (`window_manager.c:506-513`); the IME PTY feeds the same parser (shared widget namespace) (`main.c:404-410`). **Arm predicate** — keys reach the IME only when `ime_connected && ime_pty_fd >= 0 && focused type == INPUT` (`wm_backend_event` KEY `:880-883`, `forward_key` `:228-231`). **Disarm** — `ime_cancel` sends `TGS_CMD_IME_CANCEL` (100) before any focus move off an INPUT (`focus_commit` `:105`, `ime_cancel` `:71-88`); IME PTY death clears the flags (`main.c:413-418`). **Commit** — `IME_COMMIT` (97) guarded by "widget still has focus" (`:782-794`) → `be->insert_widget_text` (`lvgl_backend.c:534-546`, cursor insert on textarea, content-replace fallback). Client side: `tgs_client_send_ime_commit/preedit/cancel/candidates/select` (`tgs_client.h:64-81`) |
| **Known gap (§H.2)** | **Preedit overlay.** `TGS_CMD_IME_PREEDIT` (96) is a **documented no-op stub** in the dispatch (`window_manager.c:796-799`, "rendered by the backend (deferred)"). `set_widget_preedit` is **not a member of `struct tgs_backend`** (`tgs_backend.h:42-92` — only `insert_widget_text` exists), so the backend can't render preedit as an overlay and the invariant "preedit is never widget text" is unenforced in the render half. `docs/navigation.md:356-360` specifies the exact fix: add `set_widget_preedit(handle, text, cursor)` to `struct tgs_backend`, implement an overlay in `lvgl_backend.c`, and un-stub the dispatch case. **Cancel is done** (disarm sends cancel) |
| **Not implemented at all** | **Candidates.** `TGS_CMD_IME_CANDIDATES` (98) and `TGS_CMD_IME_SELECT` (99) are declared (`tgs_protocol.h:65-66`), specced (`protocol/tgs-spec-layer0.md:150-155`), and sent by the client API — but the compositor dispatch has **no case for either** (the switch in `window_manager.c:499-803` only reaches `IME_COMMIT` and the `IME_PREEDIT` stub; 98/99 fall into `default:` → silent drop). No candidate-strip UI, no routing of selection. The reference IME (`examples/ime_app.c`) is Direct Input and never uses candidates |
| **Other wrinkle** | The IME app creates a `TGS_WINDOW_TOOL` window (`examples/ime_app.c:21`), which joins the shared window ring; nothing excludes TOOL windows from activation, so pointer-focus can land on the IME's own window |
| **Target (L3 IME, internal)** | Direct-Input IME works with visible preedit overlay, cancel clears it, commit inserts text; a Chinese-input demo is the acceptance |
| **Gap** | §H.2 overlay (small, fully specified) + candidates/select (greenfield protocol→compositor→render path). Disarm/arm/commit/cancel are done |
| **Effort** | **Small** for preedit overlay (the doc even lists the 3 steps); **medium** for a minimal candidate strip + select routing |

## G. Cross-cutting: declared-but-noop protocol commands

Silent no-ops / dead declarations to fix (ordered by user impact):

| Cmd | Status | Evidence |
|---|---|---|
| `TGS_CMD_IME_PREEDIT` (96) | **No-op stub** | `window_manager.c:796-799` empty body |
| `TGS_CMD_IME_CANDIDATES` (98) | **No dispatch** — silent drop | not in `wm_handle_frame` switch; client sends it (`tgs_client.c:625-648`) |
| `TGS_CMD_IME_SELECT` (99) | **No dispatch** — silent drop | as above |
| `TGS_LAYOUT_GRID` (runtime, via 37) | **Stub → flex row** | `lvgl_backend.c:596-598` |
| `TGS_STYLE_FONT_SIZE` (8) | **No-op** | `lvgl_backend.c:578-580` |
| `TGS_CMD_NTF_DESTROY` (66) | **Never emitted** | declared `tgs_protocol.h:49`; zero emit sites in `src/compositor` |
| `TGS_CMD_NTF_STATE` (67) | **Never emitted** | declared `tgs_protocol.h:50`; no window-state machinery exists |
| `TGS_STREAM_RESOURCE` (2) | **Unused** | declared `tgs_protocol.h:18`; zero references in `src/` |
| `TGS_CMD_WGT_UPDATE` (33) | **Partial** | spec says `[id, property, value]` (`tgs-spec-layer0.md:85`); impl treats arg[1] as content only and ignores arg[2] (`window_manager.c:631-639`); client sends 2 args (`tgs_client.c:321-322`) — the `property` dimension is unimplemented |
| `TGS_CMD_EVT_FOCUS` (83) | Retired by design | `tgs_protocol.h:60` "MUST NOT be emitted" — correct, not a bug; `NTF_FOCUS` is the channel |

Not bugs (reserved): 39 `GET_FOCUS`, 41 `NAV_BIND`, 68 `NTF_FOCUS_PRE`.

---

## Prioritized, minimal L3 acceptance set

Realistic bar — *not* the desktop system; L4+ stays out. Each criterion is end-to-end observable.

**P1 — Styles render (mostly-implementation).**
1. App sets every `TGS_STYLE_*` prop on a button/label/input; each produces a visible LVGL change on screen (bg/fg/radius/border/shadow/opacity today; FONT_SIZE after a font mechanism lands).
2. `TGS_STYLE_FONT_SIZE` selects among ≥2 built-in font sizes (14px + one more), and text re-renders.
3. Acceptance test: a widget-render test (extends `test_lvgl_click.cpp` style harness or a new snapshot) sets a style and asserts the resulting surface differs.

**P2 — Widget library mostly complete.**
1. Basic 8 + 4 containers + LIST/SLIDER/SWITCH/DROPDOWN/TIMEPICK/DATEPICK/TAB function with events (CLICK/VALUE/KEY) end-to-end in one demo.
2. TABLE supports row/col addressing (new protocol args); MENU can be populated; RADIO is exclusive within its parent container.
3. IMAGE displays an asset once P1-resources (below) lands.
4. Acceptance: `container_demo`-style program exercises every functional type; `nav_probe.c` joins ctest.

**P3 — Containers genuinely lay out.**
1. VLAYOUT stacks, HLAYOUT rows, GLAYOUT grids (2×N), SCROLL scrolls — verified by `NTF_GEOMETRY` values in `test_geometry.cpp`-style assertions against the real backend.
2. Runtime `WGT_LAYOUT` GRID actually grids (fix `:596-598`), and geometry is re-reported.
3. Grid column/row counts configurable via protocol.

**P4 — Multi-window.**
1. Two windows coexist; pointer-click on a background window activates it (`NTF_FOCUS` with `WINDOW_ACTIVATE`/`POINTER`).
2. Alt+Tab (or Alt+`n`) cycles windows and restores each window's remembered focus (`WINDOW_RESTORE`).
3. Terminal resize broadcasts `NTF_RESIZE` to existing windows.
4. Acceptance: an automated test drives `wm_nav_key` with the alt key (extends `test_l2_focus.cpp` fixture) and asserts the `NTF_FOCUS` pair + window switch.

**P5 — Resources (greenfield, bounded).**
1. One resource type ships end-to-end: app uploads an image over `TGS_STREAM_RESOURCE` (base64 APC), compositor caches it, `TGS_WIDGET_IMAGE` renders it by resource id.
2. Same mechanism (or a builtin-font id) feeds `TGS_STYLE_FONT_SIZE`.
3. Acceptance: an image renders beside a TGS form (the roadmap's own visual bar).

**P6 — IME (internal) reaches a Chinese-input demo.**
1. §H.2 preedit overlay: `set_widget_preedit` added to `struct tgs_backend`, implemented in LVGL as an overlay, `IME_PREEDIT` un-stubbed; empty preedit clears; cancel (already sent) drops the overlay — and **the widget's own text is never polluted** by preedit.
2. Candidates: compositor receives `IME_CANDIDATES`, renders a minimal candidate strip, routes `IME_SELECT`/arrow-key selection back; a pinyin-ish IME demo commits CJK text into an INPUT.
3. Acceptance: type pinyin, see preedit overlay + candidate strip, pick a candidate, committed text lands in the textarea (focus-guarded `IME_COMMIT` already works).

**Ordering rationale:** P1–P3 are nearly all in-repo implementation (highest value per hour, smallest risk). P4 is mostly wiring. P5 is the largest single new subsystem and gates IMAGE + FONT_SIZE. P6's preedit half is small and specified; candidates is the other new-UI work and can trail resources.

---

## Key files touched by this survey (for the implementer)

- `src/common/tgs_protocol.h` — enums, stream ids, cmd ids (styles/widgets/layouts/IME/stream 2)
- `src/backends/lvgl/lvgl_backend.c` — `create_lvgl_widget` (173-235), `set_widget_content` (469-533), `set_widget_style` (548-582), `set_widget_layout` (584-603), `create_window` (379-419)
- `src/compositor/window_manager.c` — dispatch (499-803), `wm_nav_key` (252-322), `activate_window` (128-154), `ime_cancel` (71-88), `wm_flush_geometry` (409-444)
- `src/compositor/main.c` — IME fork (318-330), IME poll feed (404-418), geometry flush (437)
- `src/client/tgs_client.c/.h` — widget/style/layout/IME APIs, NTF_GEOMETRY cache (445-459)
- `src/common/tgs_backend.h` — struct (42-92); **no `set_widget_preedit`** (§H.2)
- `src/compositor/nav.c` — container/focusable/arrow-consumer policy (16-53)
- `docs/navigation.md:356-360` (§H.2 exact fix), `docs/ime.md`, `docs/widgets.md`, `protocol/tgs-spec-layer0.md:80-155`

**No code, tests, or docs were modified by this survey.**
