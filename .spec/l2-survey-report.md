# L2 Survey: Focus / Key-Routing Interop (READ-ONLY)

**Headline:** `nav.c` + `window_manager.c` + `scene_backend.c` already implement the design in `docs/navigation.md §A–§J`. The doc's §0 "as-built inventory" is **stale** (lists 4 facts now false). L2 is a **verification gap**, not an implementation gap. The unproven claim: real key edge → compositor decision → `NTF_FOCUS` to client + key/click delivered to focused widget, char base unaffected.

---

## A. Per-file survey

### src/compositor/nav.c + nav.h
- **Exists:** Pure policy layer (no 旧后端/PTY/heap). Model structs, add/remove window+widget, `nav_attr_set` (range-validated), ring construction (`build_ring`/`collect`/`scope_new`/`scope_member`/`nav_rebuild`), traversal (`nav_step`/`nav_edge`/`nav_ring_handles`), `nav_set_focus`/`nav_restore`/`nav_successor`/`nav_all_handles`/`nav_first_focusable`/`nav_other_window`. Type policy: containers+label+progress+image non-focusable; input/slider/list/etc consume arrows; DIALOG root = TRAP, others GROUP/plain.
- **Tested:** `tests/test_nav.cpp` (9 tests: ring skips, FOCUS_INDEX sort, attr overrides, GROUP single-stop, TRAP wrap, DIALOG-root-TRAP, reason map, successor, restore). Pure model only.
- **Missing:** None at policy layer.

### src/compositor/window_manager.c
- **Exists:** Focus authority fully wired. `emit_focus`→`NTF_FOCUS(65)` with `[win,wid,focused,reason]`; `focus_commit` (single change path, dedup on identical); `activate_window` (keyboard switch + restore); `push_ring`; `enroll_widget`; `wm_nav_key` precedence hook (Tab/Shift+Tab→`nav_move`, Ctrl+Tab hatch, arrows→`nav_dir_move` or PASS, Home/End→`nav_edge_move`, residual `forward_key`); frame dispatch HELLO/WIN_CREATE(init-focus §E)/WGT_CREATE/WGT_DESTROY(successor)/SET_FOCUS/WGT_ATTR/IME_*; `wm_backend_event` bridges TGS_EVENT_{CLICK,VALUE,FOCUS,BLUR,KEY}→EVT_* frames.
- **Tested:** **Zero.** No test references `wm_handle_frame`/`wm_init`. `test_l1.cpp` drives only the client side.
- **Missing:** No test proves NTF_FOCUS emitted on INIT/TAB/POINTER, or that `wm_nav_key` routes correctly. **Core L2 gap.**

### src/backends/旧渲染引擎/scene_backend.c
- **Exists:** Full `tgs_backend` impl. Per-window root+group (`后端焦点组_set_wrap` true); `backend_set_window_ring`/`set_active_window`/`set_focus`/`focus_dir` (geometric, primary+2·perp)/`set_widget_focusable`/`set_nav_key_cb`; `backend_inject_key` (CONSUMED→drop, WIDGET→`后端焦点组_send_data` bypass, PASS→queue); `kb_read_cb`/`mouse_read_cb`; `旧渲染引擎_event_handler` (LV_EVENT_*→TGS_EVENT_*); `map_tgs_key` maps 1000–1007 + Tab→NEXT/PREV.
- **Tested:** Only indirectly via screenshot/font probes. `nav_probe.c` is a client probe needing xdotool+Xvfb — **not in ctest**.
- **Missing:** No automated test exercises the indev round-trip (inject_key→kb_read_cb→LV_EVENT_KEY→wm_backend_event→EVT_KEY).

### src/common/tgs_backend.h
- **Exists:** Complete abstract interface matching all callers.
- **Missing:** None.

### docs/navigation.md
- **Defect:** **Stale.** §0 claims now false: (1) "NTF_FOCUS never emitted" — false, `emit_focus` emits it; (2) "focus untracked, win_id hardcoded" — false, per-window tracked; (3) "arrows map 1-4, fall through" — false, `map_tgs_key` maps 1000-1003; (4) "modifiers dropped, mods=0" — false, `sdl_mods` computes them. Status line still says "to be implemented".

### src/compositor/term.c + main.c
- **Exists:** PTY fork, wm_init, poll loop, input sinks. Char base = `tgs_term` + `tgs_term_view` (旧后端 canvas). `input_poll` calls both term sink AND `backend->inject_key/mouse`.
- **Defect (char-input cut):** `term_key_sink` returns early when `hello_received`. Post-HELLO the character program gets **zero** keyboard input — only the widget path. Char base is render-only. Flag: if L2 means both take input, this is a gap; if "char base renders output", by-design.
- **Defect (disp resize):** `wm.disp_w/disp_h` set once at init, never updated in `term_resize_to`. `send_resize` reads stale values. No NTF_RESIZE broadcast to existing windows on resize.

### src/client/tgs_client.c/.h
- **Exists:** handshake, create/update/style, `set_focus`, `get_focus` (cached from NTF_FOCUS), `poll_event` (decodes EVT_CLICK/EVT_KEY/EVT_VALUE/NTF_FOCUS→FOCUS/BLUR/IME_*).
- **Tested:** `test_l1.cpp` proves handshake+liveness+split-header. Asserts **no event content**.
- **Missing:** No client-side assertion that NTF_FOCUS/EVT_KEY arrive in response to compositor actions.

---

## B. Existing test coverage of focus/key routing

**Not covered.** Grep of `tests/` for `NTF_FOCUS|wm_nav_key|inject_key|focus_commit|SET_FOCUS|focus_dir|TGS_EVENT_KEY|TGS_REASON_` → only `test_nav.cpp` (nav.c model) and `nav_probe.c` (manual client probe). `test_l1.cpp` asserts handshake+liveness only.

- **Covered:** nav.c ring/scope/reason/successor/restore (test_nav, 9 tests); L1 handshake byte-level (test_l1); L0 terminal/pty/snapshot/frame/protocol.
- **Uncovered:** (1) `emit_focus`→NTF_FOCUS on INIT/TAB/POINTER/PROGRAMMATIC/DESTROYED/HIDDEN; (2) `wm_nav_key` decision CONSUMED vs PASS vs WIDGET; (3) `activate_window` keyboard switch+restore; (4) WGT_CREATE init-focus §E; (5) WGT_DESTROY successor; (6) `wm_backend_event` CLICK/KEY/VALUE bridges; (7) 旧渲染引擎 indev round-trip; (8) mouse click→focus→NTF_FOCUS(POINTER).

---

## C. Minimal plan to make L2 verifiable end-to-end

### Tier 1 — REQUIRED, CI-headless, fake backend (new `tests/test_l2_focus.cpp`)

`tgs_tests` already links `tgs_compositor` (nav.c+window_manager.c+parser.c) but **not** `tgs_scene_backend`. `window_manager.backend` is a `tgs_backend*` — supply stub function pointers. Isolates compositor focus-authority (the L2 concern) from 旧后端 rendering. Reuse `read_frame()` from test_l1.cpp for APC decode.

**Setup:** `fake_be` struct (record-calls stubs); `pty_fd` = pipe; `wm_init(&wm, &fake_be.base, pty_write, -1)`. Stubs return fake handles by id; `set_focus`/`set_active_window`/`set_window_ring`/`set_widget_focusable` record; `set_nav_key_cb` captures cb+ud; `focus_dir` returns NULL (force residual-forward); `set_event_callback` captures `wm_backend_event` so the test can simulate 旧后端 events.

| # | Test | Asserts |
|---|------|---------|
| 1 | init focus | Feed HELLO→READY; WIN_CREATE(1); WGT_CREATE(10,BUTTON). | NTF_FOCUS(win=1,wid=10,focused=1,reason=INIT) |
| 2 | Tab forward | WGT_CREATE(11,BUTTON); nav_key_cb(TAB,0,1). | CONSUMED; NTF_FOCUS(10→0,TAB)+(11→1,TAB); set_focus(handle_11) |
| 3 | Shift+Tab back | nav_key_cb(TAB,SHIFT,1). | CONSUMED; NTF_FOCUS back to 10, reason=SHIFT_TAB |
| 4 | arrow passthrough | focus=10(button): nav_key_cb(LEFT,0,1). | CONSUMED (focus_dir NULL→forward_key); EVT_KEY(key=1000). Then focus SLIDER: nav_key_cb(LEFT,0,1)→PASS (consumes arrows) |
| 5 | click→POINTER | g_event_cb(handle_11, TGS_EVENT_FOCUS). | NTF_FOCUS(reason=POINTER). Re-call same handle→no extra frame (dedup) |
| 6 | key delivery | focus=10; g_event_cb(handle_10, TGS_EVENT_KEY, "97;0"). | EVT_KEY(win=1,wid=10,key=97,mods=0) |
| 7 | destroy successor | focus=11; WGT_DESTROY(11). | NTF_FOCUS(11→0,DESTROYED)+(10→1,DESTROYED) |
| 8 | char demux alive | wire text_cb counter; feed raw text between frames. | text_cb called (char path alive) while NTF_FOCUS still emits |

Proves the 4 L2 claims: (a) NTF_FOCUS with correct reason; (b) key-routing decision per widget type; (c) key/click reach focused widget as EVT_KEY/EVT_CLICK; (d) char path demux unaffected.

### Tier 2 — OPTIONAL smoke, real 旧后端 (reuse `nav_probe`)

Run `tgs-compositor` + `nav_probe` under Xvfb (`scripts/run-xvfb.sh` exists). Inject Tab + letter via xdotool. Assert `/tmp/tgs-nav-probe.log` contains `focus widget=10 reason=INIT`, `focus widget=12 reason=TAB`, `key widget=12`. Proves the 旧后端 indev round-trip Tier-1 fakes. Heavier; gating-on-failure only.

### Defects found (real, not speculative)

| ID | Severity | Where | Issue |
|----|----------|-------|-------|
| D1 | medium | main.c:328-329, 107-136 | `wm.disp_w/disp_h` set once, never updated on resize. `send_resize` sends stale size. No NTF_RESIZE to existing windows on resize. |
| D2 | low-narrow | scene_backend.c:814-819 | `TGS_NAV_WIDGET` branch calls `后端焦点组_send_data` without updating `key_ev_code/key_ev_mods` → app gets stale key via LV_EVENT_KEY. Only Tab on `attr_tab=1` widgets. |
| D3 | medium-visual | scene_backend.c:393-403 | Window root: fullscreen opaque bg, created after term canvas → paints over char base. Once any window exists, character terminal hidden (bytes still flow, not visible). Contradicts "char base still renders". Needs design decision. |
| D4 | medium | scene_backend.c:393-403 vs 454-460 | Window root retains default-theme padding (no `remove_style_all`). Widget pos is content-box-relative; mouse coords are screen pixels → click hit-test misses by root padding. |
| D5 | low-doc | docs/navigation.md §0 | 4 stale facts + status "to be implemented". Misleads. |
| D6 | low-info | window_manager.c:601-603 | `EVT_BIND` is no-op (L0: all forwarded). `tgs_client_bind_event` is cosmetic. Document or implement if selective binding intended. |

## D. Read-only

No files modified. Tier-1 test is **proposed**, not written — awaiting go-ahead. It needs only a new `tests/test_l2_focus.cpp` + one line in `CMakeLists.txt` `tgs_tests` SOURCES; no existing code change (tgs_compositor already linked).
