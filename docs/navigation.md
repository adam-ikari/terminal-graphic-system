# TGS Navigation Design

**Status:** Design — to be implemented (Layer 1)
**Version:** 1.0
**Scope:** focus model, focus order, focus scopes, keyboard navigation, programmatic focus, focus events, window activation/restoration, IME composition, LVGL mapping, protocol additions.

Companion docs: [architecture.md](architecture.md) · [ime.md](ime.md) · [widgets.md](widgets.md) · [../protocol/tgs-spec-layer0.md](../protocol/tgs-spec-layer0.md)

---

## 0. As-built inventory (what the code does today)

The design below is a delta against this reality. Facts verified against the working tree.

| Fact | Evidence |
|---|---|
| The LVGL backend creates **one** group for the whole compositor; every non-container widget is added to it, focusable or not | `src/backends/lvgl/lvgl_backend.c:206`, `:280` |
| `lv_group_set_default()` is never called | no occurrence in `src/` |
| That group is attached to the single KEYPAD indev | `src/backends/lvgl/lvgl_backend.c:207-210` |
| `TGS_CMD_NTF_FOCUS` (65) is declared but **never emitted** | `grep -rn TGS_CMD_NTF_FOCUS src/` → header only |
| Focus reaches the app only as `EVT_FOCUS` (83) with payload `[win_id, widget_id, focused]`, `reason` does not exist | `src/compositor/window_manager.c:388-399`, `src/client/tgs_client.c:421-430` |
| The compositor does not track focus at all; `win_id` for every event is hardcoded to `window_map[0]` | `src/compositor/window_manager.c:360-364` |
| Key codes arriving from SDL: TAB=9, ESC=27, arrows=**1000-1003**, HOME=1004, END=1005 | `src/backends/input_sdl.c:19-37` |
| `map_tgs_key()` maps arrows from **1-4**, so SDL arrow codes 1000-1003 fall through to `default` and reach LVGL unmatched | `src/backends/lvgl/lvgl_backend.c:43-57` |
| Modifiers are dropped: `input_sdl.c` always injects `mods = 0`, so Shift+Tab cannot be distinguished | `src/backends/input_sdl.c:73-74`, `src/common/tgs_backend.h` (`inject_key(key, mods, pressed)`) |
| `LV_USE_GRIDNAV 1` is enabled in project config | `src/backends/lvgl/lv_conf.h:153` |
| LVGL version 9.6.0 | `deps/lvgl/include/lvgl/lv_version.h:9-11` |

Two consequences drive the design: (1) focus needs an **authority** (today nobody has one), and (2) the key-code space and the Shift modifier must be fixed before any navigation binding can work.

---

## A. Focus model

### A.1 Who owns focus: the compositor

**The compositor owns focus.** The app *requests* it; the backend *renders and executes* it; neither owns it.

Justification against principle #1 (*terminal manages structure; app manages layout*):

1. Focus is **structure**, equivalent to z-order and window activation: it answers "which widget receives input", not "where is the widget drawn". The app already delegates z-order and activation to the compositor; focus is the same class of state.
2. Focus is **not local to one process**. The compositor must route keystrokes to the IME process based on the focused widget (`docs/ime.md:18-25`). A single authority is required or the routing decision and the visual focus can disagree.
3. Focus must stay **consistent across window activation**, hide/destroy and widget destruction — events the compositor owns (`WIN_DESTROY`, `NTF_STATE`, `WGT_DESTROY`). An app-side focus owner would be required to implement restoration for windows it did not hide.
4. Apps may be slow, buggy, or dead. Focus changes cannot be gated on an app round-trip (see §F.3).

Division of labour, stated exactly:

| Concern | Owner |
|---|---|
| Focus registry (`window_id → focused widget_id`, `last_focus_widget_id`), reasons, restoration | compositor |
| Ring computation (tree order + `FOCUS_INDEX`), scope membership, wrap | compositor |
| Binding table (key → nav action) | compositor (defaults; app-overridable in Layer 2, §K) |
| IME activation/deactivation and key hand-off to the IME process | compositor |
| Emitting `NTF_FOCUS` / forwarding residual `EVT_KEY` | compositor |
| Focus visibility (outline, ring order inside LVGL, scroll-into-view) | backend |
| Key translation (TGS key code → LVGL key), execution of focus moves, geometric candidate search | backend |
| Widget contents, activation semantics (what Enter *does*), dialog visibility | app |

The compositor is authoritative when the two disagree: the backend reports focus changes and the compositor decides whether they stand.

### A.2 Granularity

- **One focused widget per window.** A window's focus is either a widget id or "none" (`0`).
- **One focused window** globally: the active window. It owns the keyboard; all other windows are frozen (`lv_group_focus_freeze`, §I).
- Focus **scopes** (§C) refine *within* a window: a window's ring may consist of plain widgets and of scope containers, each of which has an inner ring with its own cursor. At any instant exactly two cursors are live: the window's ring cursor and at most one inner scope cursor per level of nesting on the path to the focused leaf. No two leaf widgets are focused at the same time.

### A.3 Focusable widget types

Default focusability is a property of the widget **type**; it can be overridden per widget with `TGS_ATTR_FOCUSABLE` (§J).

| Widget (enum) | Focusable | Consumes arrows | Consumes Tab | Notes |
|---|---|---|---|---|
| BUTTON (0) | yes | no | no | Enter/Space activate; arrows navigate away |
| LABEL (1) | **no** | — | — | static text |
| INPUT (2) | yes | yes | no (opt-in) | arrows move the caret; `TGS_ATTR_NAV_TAB=1` turns Tab into text (multiline editor use) |
| CHECKBOX (3) | yes | no | no | Space toggles |
| RADIO (4) | yes | no | no | Space toggles; arrows do **not** walk the radio set |
| SLIDER (5) | yes | **yes** | no | arrows change the value — must never navigate |
| PROGRESS (6) | **no** | — | — | read-only indicator |
| SWITCH (7) | yes | no | no | Space toggles |
| VLAYOUT (8) | no | no | no | container; transparent unless declared a scope (§C) |
| HLAYOUT (9) | no | no | no | same |
| GLAYOUT (10) | no | no | no | same |
| SCROLL (11) | no | **yes*** | no | *consumes arrows only while it can still scroll in that direction: `LV_OBJ_FLAG_SCROLL_WITH_ARROW` (`deps/lvgl/include/lvgl/core/lv_obj.h:64`). Tab is the way out |
| LIST (12) | yes | yes | no | arrows move item selection |
| TABLE (13) | yes | yes | no | arrows move the cell cursor |
| MENU (14) | yes | yes | no | treated like LIST: it is an item ring, not a layout container |
| TAB (15) | yes | yes (L/R) | no | L/R switch pages; U/D navigate away |
| DROPDOWN (16) | yes | yes | no | U/D open/step the list, L/R switch the value |
| IMAGE (17) | **no** | — | — | |
| TIMEPICK (18) | yes | yes | no | U/D step the field |
| DATEPICK (19) | yes | yes | no | arrows step the field |

Additional rules:

- A widget in `LV_STATE_DISABLED` is treated as non-focusable and is skipped by every ring (it stays in the tree).
- Containers are never focus stops by default: they are **transparent** for focus (their children take part in the enclosing ring). Exception: a container declared `TGS_ATTR_FOCUS_SCOPE = GROUP` is a single tab stop (§C.2).
- An app may mark any widget focusable/non-focusable with `TGS_ATTR_FOCUSABLE` (0/1), and may turn arrow consumption on/off with `TGS_ATTR_NAV_ARROWS` (0/1) — this is the "consumes arrows" notion the binding precedence in §D depends on.
- `MENU` is included in the focusable set deliberately: it is an item ring (like LIST/TABLE), not a layout container. Nothing else in the table deviates from principle #2.

---

## B. Focus order

### B.1 Default order = tree order

The default ring order is **pre-order depth-first traversal of the window's widget tree**, children visited in creation order. The compositor assigns a monotonically increasing `seq` to each widget of a window at `WGT_CREATE`; the tree is reconstructed from `parent_id`.

For well-formed trees (parents created before children, which the protocol already requires — `WGT_CREATE` rejects unknown `parent_id`), pre-order traversal **is** creation order. The design states tree order because it is the definition that survives re-parenting and container insertion, and because it matches the app's mental model of the visible layout.

Deterministic tie-break everywhere: `(seq)`.

### B.2 Explicit app control: `FOCUS_INDEX`

The app may pin a widget's position with `TGS_ATTR_FOCUS_INDEX` (int32, default `-1` = auto) via `TGS_CMD_WGT_ATTR`.

Sort key inside a ring:

```
explicit first (index 0), ascending FOCUS_INDEX,
then auto entries (index 1), ascending tree-order position.
```

Rationale: a fully indexed ring behaves as if renumbered in a fresh `0..n-1` space, and a partially indexed ring still has a total order with no ambiguity. Partial indexing is legal but discouraged (documented in the client API).

`FOCUS_INDEX` is recomputed into the ring lazily, on the next traversal, not on every attribute write.

### B.3 Containers are transparent

Containers do not appear in their parent ring — their children do, in tree order. `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` therefore cost no Tab press. The only exception is a container declared a scope (`§C`), which exposes **one** ring entry that represents its whole subtree.

---

## C. Focus scopes

A **scope** is an ordered ring of focusable members plus a kind. Every window root is a scope. A container may declare an inner scope.

### C.1 Kinds

| Kind | Value | Behaviour |
|---|---|---|
| `NONE` | 0 | default; container is transparent |
| `GROUP` | 1 | container is a **single tab stop** in the parent ring (a toolbar). Tab enters it as one stop; inside, **arrows** move between its members; Tab immediately leaves to the next parent-ring entry. The container holds two cursor levels while active. |
| `TRAP` | 2 | container's descendants form a **modal ring**: Tab/Shift+Tab wrap inside it and cannot leave. Turning a `TRAP` on freezes every enclosing ring. Intended for modal panels declared as widgets. |

Window-derived scopes:

| Window type | Root scope kind |
|---|---|
| `NORMAL` (0) | ring with wrap = true |
| `TOOL` (3) | ring with wrap = true |
| `FULLSCREEN` (2) | ring with wrap = true |
| `DIALOG` (1) | **TRAP**: wraps inside, freezes the previously active window's rings |

Wrap is on for every ring (matches `lv_group_set_wrap(group, true)`).

### C.2 Semantics

- Scope membership: a scope's ring contains exactly the focusable **descendants reachable without crossing another scope boundary**, in tree order (§B.1).
- Declaring or changing `TGS_ATTR_FOCUS_SCOPE` on a container rebuilds the parent ring and the inner ring (rebuild = recompute + re-enroll in LVGL; §I).
- Nested scopes are legal (a `TRAP` dialog inside a `GROUP` toolbar). Depth is bounded by the widget tree depth.
- Entering/leaving: focus entering a scope targets the scope's **remembered** member if it still exists and is focusable, otherwise the first member in ring order (reason `SCOPE_RESTORE` for the remembered case).

### C.3 Modal dialogs

- A `DIALOG` window's root scope is a `TRAP`. While it is active, every underlying window's group is frozen (`lv_group_focus_freeze(other, true)`); Tab cycles inside the dialog only.
- **Escape is not swallowed by the compositor.** The compositor never closes or hides a window on its own: windows have no app-facing "show" command (`NTF_STATE` is compositor → app only, id 67), so an ESC that hid a dialog could not be undone by the app. ESC is forwarded to the focused app as `EVT_KEY` (key code 27); the app destroys (`WIN_DESTROY`) or hides the dialog. The compositor's half of the contract is **focus restoration** (§G.2), which is what makes the dialog cycle close cleanly.
- Closing a dialog therefore is: ESC → app → `WIN_DESTROY` / `NTF_STATE`-driven hide → compositor unfreezes the underlying window, restores its remembered focus, and emits `NTF_FOCUS` (reason `WINDOW_RESTORE`) to the dialog's app for the defocus and to the restored app for the focus.

---

## D. Keyboard navigation

### D.1 The split: compositor decides, backend executes

This is the crux of the design, so it is stated as a rule rather than a sketch:

- The **compositor** owns the binding table and the policy: which keys are navigation for the current focused widget, what happens when a navigation key is consumed, and what the app sees. It computes ring order (it owns `seq` and the attributes) and it owns the focus registry.
- The **backend** owns execution, because only it has LVGL's real geometry (flex/grid move children after `set_widget_rect`, so app-declared rectangles are not authoritative) and LVGL's key semantics. It translates TGS key codes to LVGL keys, maintains group membership in compositor-defined order, performs the focus move, and reports the resulting focus change back with a reason.
- Consequence: **a key consumed by navigation never crosses the protocol boundary.** It produces no `EVT_KEY`. Conversely, a key the widget/app consumes is delivered normally.

Routing of one key press:

```
terminal / SDL / evdev
      │  normalise to TGS key code + mods        (§D.4)
      ▼
compositor input  ──►  backend -> inject_key(key, mods, pressed)
                             │
                 navigation binding? (compositor default table, §D.2)
                     ┌───────┴────────┐
                 consumed          not consumed
                     │                 │
        backend moves focus    LVGL sends key to the focused widget
        (ring / spatial)              │
                     │            widget event ──► TGS_EVENT_KEY
                     │                              │
                     │              ┌───────────────┴───────────────┐
                     │        IME app active?                otherwise
                     │        (focused widget is IME-eligible)      │
                     │              │                        EVT_KEY → app
                     │        EVT_KEY → IME process
                     │
        focus report (TGS_EVENT_FOCUS/BLUR + reason)
                     │
        compositor updates registry, restores preedit rules (§H),
        emits NTF_FOCUS to the owning app
```

### D.2 Default binding table (compositor defaults)

| Key | Action | Precedence detail |
|---|---|---|
| `Tab` (9) | `focus_next` in the current ring, wrap | consumed unless the focused widget has `NAV_TAB=1` |
| `Shift+Tab` | `focus_prev` in the current ring, wrap | same; requires the modifier fix (§D.4) |
| `Ctrl+Tab` | `focus_next` in the current ring | **always** consumed; the escape hatch out of a `NAV_TAB=1` widget |
| `Left`, `Right`, `Up`, `Down` (1000-1003) | 1) widget consumes arrows → delivered to the widget; 2) else spatial `focus_dir()` inside the current ring; 3) else forwarded as `EVT_KEY` | the widget-consuming set is the §A.3 table plus `TGS_ATTR_NAV_ARROWS` |
| `Home` (1004), `End` (1005) | 1) widget consumes arrows → delivered to the widget (list/table/input use them); 2) else first/last member of the current ring | same notion of "consumes arrows" |
| `PageUp` (1006), `PageDown` (1007) | delivered to the widget; if unconsumed, forwarded as `EVT_KEY` | reserved codes; no ring action in Layer 1 |
| `Enter` (13), `Space` (32) | delivered to the focused widget (LVGL press → `LV_EVENT_CLICKED` → `EVT_CLICK` for the app) | the compositor never activates anything itself |
| `Escape` (27) | delivered to the widget/app, **always** | never a navigation key (§C.3) |
| `Alt+Tab`, `Ctrl+Alt+Tab` | unbound in Layer 1 | window switching deferred (§G.1) |

Everything in this table is the compositor's default binding. Layer 2 adds `TGS_CMD_NAV_BIND` (id 41, capability `nav.bind`) for per-app overrides; Layer 1 has no override channel and says so at negotiation time (§J.5).

### D.3 Precedence rule (ordered, exhaustive)

1. **Navigation binding test.** `Tab`/`Shift+Tab`/`Ctrl+Tab` are navigation unless the focused widget opts out via `NAV_TAB=1`; a non-focusable widget is never the focused widget, so this test always has a subject.
2. **Widget hand-off.** If the focused widget consumes the key (type table + `NAV_ARROWS`/`NAV_TAB` override), the backend delivers it to that widget. The widget changes its own value/cursor and may emit `EVT_VALUE`/`EVT_KEY`; navigation does not run.
3. **Ring navigation.** Otherwise the block is for the compositor: `focus_next`/`focus_prev` (Tab family), spatial `focus_dir` (arrows), ring ends (Home/End).
4. **Residual forward.** If the key was neither consumed by the widget nor usable as navigation (e.g. `Up` with no widget above), it is forwarded to the app as `EVT_KEY` — or to the IME process when the focused widget is IME-active (§H). Apps therefore keep full access to raw keys.

This is what makes "a slider needs arrows; a plain container does not" true without special cases: the slider matches rule 2, the container matches rule 3.

Spatial search (`focus_dir`, backend): candidates are the members of the current ring (scope members when a scope cursor is active). A candidate qualifies if its center lies in the arrow's half-plane relative to the focused widget's center. Score = distance along the arrow axis + 2 × |perpendicular offset|; lowest score wins. `LV_OBJ_FLAG_SCROLL_*`: the target is scrolled into view (`lv_obj_scroll_to_view`) after the move. If no candidate qualifies, the key falls to rule 4.

### D.4 Key-code normalisation (required prerequisite)

Navigation cannot work on the current key space: `input_sdl.c` emits arrows as 1000-1003 and HOME/END as 1004/1005, while `map_tgs_key()` in the LVGL backend maps 1-4 (`src/backends/lvgl/lvgl_backend.c:43-57`), and SDL injects `mods = 0` always. The TGS key space is therefore defined once, in the compositor, and every input source (SDL, framebuffer/evdev, future backends) MUST normalise into it:

| Key | Code | | Key | Code |
|---|---|---|---|---|
| Backspace | 8 | | Left | 1000 |
| Tab | 9 | | Right | 1001 |
| Enter | 13 | | Up | 1002 |
| Escape | 27 | | Down | 1003 |
| Space | 32 | | Home | 1004 |
| Delete | 127 | | End | 1005 |
| printable ASCII | ASCII | | PageUp / PageDown | 1006 / 1007 |

Modifiers travel alongside (`inject_key(key, mods, pressed)`), with `TGS_MOD_SHIFT = 1`, `TGS_MOD_CTRL = 2`, `TGS_MOD_ALT = 4`. `map_tgs_key()` MUST map 1000-1007 to `LV_KEY_LEFT/RIGHT/UP/DOWN/HOME/END`, and MUST translate `Tab + SHIFT` to `LV_KEY_PREV` (11) rather than `LV_KEY_NEXT` (9) — LVGL has no modifier concept, so Shift must be resolved before injection. Codes 1-4 MUST NOT be used for arrows (they collide with LVGL's `LV_KEY_HOME = 2` / `LV_KEY_END = 3`, `deps/lvgl/include/lvgl/core/lv_group.h:24-35`).

`EVT_KEY` (`[win_id, widget_id, key, mods]`) carries these codes verbatim to the app — the app-facing key space is the table above.

---

## E. Programmatic focus

- **Set:** `TGS_CMD_SET_FOCUS` (38, app → compositor), args `[window_id, widget_id]`. `widget_id = 0` clears focus for that window.
  - The compositor validates: window must exist, widget must exist and belong to that window, must be focusable, and must not be disabled. **An invalid request is ignored** (logged); the compositor never focuses a label, an image, a progress bar, a container, or a widget from another window.
  - On success: update the registry, call `backend.set_focus(handle)`, emit the focus pair (§F) with reason `PROGRAMMATIC = 5`, unless the focused widget already was the requested one (then emit nothing).
  - Setting focus in a non-active window is allowed and does **not** activate the window; it only updates the remembered focus.
- **Query:** `tgs_client_get_focus(win_id)` returns the client's cached value, kept up to date from `NTF_FOCUS`. No protocol round-trip in Layer 1. A cold query (`TGS_CMD_GET_FOCUS`, id 39, reply = `NTF_FOCUS` with reason `NONE`) is reserved for Layer 2; the cache is correct as long as the client applies every notification.
- **Automatic focus on creation:** the first focusable widget created in a window whose focus is "none" receives focus (reason `INIT = 6`). This keeps trivial apps usable with no focus code at all, and gives the ring a well-defined starting point.
- **Pointer focus:** the backend marks every focusable widget `LV_OBJ_FLAG_CLICK_FOCUSABLE`; LVGL's pointer path then calls `lv_group_focus_obj()` for click-focusable objects that belong to a group (`deps/lvgl/src/indev/lv_indev.c:1761-1776`). The resulting change is reported upward with reason `POINTER = 4`. Clicking a non-focusable widget or the background does not change widget focus.
- Pointer and keyboard compose through one registry: whichever route moved focus last wins, and both produce the same notification shape. A click inside window B activates B (raising it and restoring B's remembered focus, reason `WINDOW_ACTIVATE = 7`) *and*, if it lands on a focusable widget, focuses that widget (reason `POINTER`), in that order: activation pair first, then the widget-level pair within the newly active window.

---

## F. Focus events to the app

### F.1 Payload

`TGS_CMD_NTF_FOCUS` (65, compositor → app, stream 4), args:

```
[win_id, widget_id, focused, reason]
```

- `focused`: `1` gained, `0` lost.
- On every focus change the compositor emits **two** frames, in this order: the old widget with `focused=0`, then the new widget with `focused=1`, both carrying the same `reason`. If there was no previously focused widget only the gained frame is emitted; when focus is cleared only the lost frame is emitted with `widget_id = 0`.
- `reason` values:

| Value | Name | Meaning |
|---|---|---|
| 0 | `NONE` | unspecified / cold query reply |
| 1 | `TAB` | Tab traversal |
| 2 | `SHIFT_TAB` | Shift+Tab traversal |
| 3 | `ARROW` | spatial arrow navigation |
| 4 | `POINTER` | pointer click on the widget |
| 5 | `PROGRAMMATIC` | `SET_FOCUS` from the app |
| 6 | `INIT` | initial focus of a window |
| 7 | `WINDOW_ACTIVATE` | window activated / raised / unhidden |
| 8 | `WINDOW_RESTORE` | focus restored because a window was hidden or destroyed |
| 9 | `SCOPE_RESTORE` | focus restored inside a scope (dialog/modal closed) |
| 10 | `DESTROYED` | the focused widget was destroyed |
| 11 | `HIDDEN` | focus lost because the widget's window was hidden/minimized |

Delivered to the **owning app** of the window (one PTY). Notifications are deduplicated: identical `(win_id, widget_id, focused)` in a row MUST NOT be re-sent. This is what keeps the compositor's optimistic `SET_FOCUS` (`§E`) from producing an echo when the backend reports the same focus back.

### F.2 Relationship to `EVT_FOCUS` (83)

`EVT_FOCUS` (83) is retired: it carries the same information minus the reason, requires `EVT_BIND`, and its existence guarantees two divergent focus channels. After this change `NTF_FOCUS` (65) is the **only** focus channel. The client maps frame 65 onto `TGS_EVENT_FOCUS` / `TGS_EVENT_BLUR` (with `widget_id`, `focused` and a new `reason` field), so app-visible behaviour is unchanged except for the added reason. Id 83 stays allocated but MUST NOT be emitted.

This is a fix to a stale spec entry, not a break: id 65 is currently never emitted anywhere in the tree.

### F.3 No veto in Layer 1

The app **cannot** prevent a focus change. Reasons:

1. A veto is a synchronous round-trip, and the compositor must not block on app latency — the protocol has no request/response correlation, no timeouts, and apps can hang.
2. Focus is structure (§A.1). An unresponsive app must not be able to freeze the terminal's keyboard.
3. The use case behind "focus-out validator" (do not leave a field with invalid input) is served by re-requesting focus: the app receives the lost/gained pair and issues `SET_FOCUS` back to the field with reason `PROGRAMMATIC`. The result is observable and strictly bounded by the app's own round-trip.

A pre-notification with a bounded timeout (`NTF_FOCUS_PRE`, id 68, capability `nav.focus_veto`) is reserved for Layer 2 (§K).

---

## G. Window navigation

### G.1 Keyboard window switching: deferred

`Alt+Tab`/`Ctrl+Tab` across windows is **not Layer 1**. Reasons:

1. A window switcher is compositor-owned chrome — an overlay UI listing windows, which is exactly the kind of surface the Layer 1 milestone does not include (Layer 1 = floating/grid/split layouts, full widget library, resource URIs).
2. Multi-window Layer 1 apps (a form plus a dialog) can already switch by pointer, and dialogs are modal by construction.
3. The prerequisite work — per-window focus registries, activation and restoration order — is in Layer 1, so adding the switcher later is additive and does not reshape the protocol.

Layer 1 does include the window **activation contract** below, because dialogs need it.

### G.2 Activation and restoration

- Every window stores `last_focus_widget_id` (updated on each focus gain).
- **Activate** (pointer click on a window, raise, unhide): the window becomes the active window, the keyboard group switches to its root scope, and focus is restored to `last_focus_widget_id` if it is alive and focusable, else to the first member in ring order (reason `WINDOW_ACTIVATE`). The previous window receives a lost pair with reason `WINDOW_ACTIVATE`.
- **Hide / minimize** (`NTF_STATE` = `HIDDEN` or `MINIMIZED`): the window's app receives a lost pair with reason `HIDDEN`; focus moves to the topmost still-visible window, restoring its remembered focus with reason `WINDOW_RESTORE`.
- **Destroy** (`WIN_DESTROY`): identical to hide, plus the window's registry entry is dropped and its LVGL groups deleted.
- **Widget destroy while focused**: focus moves to the next ring member after the destroyed widget, else the previous member, else "none"; reason `DESTROYED`. A destroyed widget is removed from every ring before the successor is chosen.
- **Scope destroyed/hidden** (a `TRAP` panel closed): unfreeze the enclosing ring, restore its remembered member, reason `SCOPE_RESTORE`.
- Z-order is used only to pick the restoration target; the compositor MUST NOT reorder z-order as a side effect of a focus change.

---

## H. Composition with IME

Per `docs/ime.md`, the IME is a separate TGS app; the compositor routes keys to it based on the focused widget. Navigation changes exactly two things: when IME routing is armed, and what happens to the preedit when focus leaves.

### H.1 Arming and disarming

- **Arm:** focus gains a widget of type `INPUT` (the IME-eligible type) → the compositor records `ime_target = (window_id, widget_id)` and routes subsequent keys that reach the `EVT_KEY` stage to the IME process instead of the app. The existing routing condition in `wm_backend_event` (`wm->ime_connected && focused type == TGS_WIDGET_INPUT`) is the same predicate; it must additionally require `ime_pty_fd >= 0`.
- **Disarm:** focus leaves that widget (any reason) → clear `ime_target`, send `TGS_CMD_IME_CANCEL` (100) to the IME process, clear the widget's preedit, then emit the focus pair. Order matters: cancel → preedit cleared → `NTF_FOCUS`, so an app that reads widget text in its focus handler never observes a stale preedit.

### H.2 Preedit is an overlay, never widget text (invariant)

The compositor renders preedit through a dedicated backend call, `set_widget_preedit(handle, text, cursor)`; the widget's text is untouched until `IME_COMMIT` (97) inserts committed text via `insert_widget_text`. This is a deliberate change from "preedit as content": it makes cancel trivial (drop the overlay), keeps the app's own text model coherent, and removes the need for the compositor to cache committed text in order to restore it. Every `IME_PREEDIT` received for the armed widget replaces the overlay; an empty preedit clears it.

**Layer-1 known gap — preedit overlay not yet rendered.** The invariant above is the design contract, but the rendering half is not implemented in this layer. Specifically: `set_widget_preedit` is **not** a member of `struct tgs_backend` yet (it is only proposed in §J.6), the LVGL backend has no overlay widget, and the compositor's `TGS_CMD_IME_PREEDIT` (96) case in `window_manager.c` is a documented no-op stub that points back here. `IME_CANCEL` (100) — the other half of H — *is* implemented (disarm sends cancel, focus visuals clear). Closing this gap is small and localised: (1) add `void (*set_widget_preedit)(void *handle, const char *text, int cursor)` to `struct tgs_backend` (`src/common/tgs_backend.h`); (2) implement it in `src/backends/lvgl/lvgl_backend.c` as a per-`INPUT` overlay `lv_label` (shown when `text` is non-empty, hidden on empty, dropped on `destroy_widget`); (3) in `window_manager.c`'s `IME_PREEDIT` case, call `be->set_widget_preedit(w->handle, text, cursor)` and clear it (call with `""`/`NULL`) both on empty preedit and in the disarm path of §H.1. No protocol change is needed — command 96 already carries `[win_id, widget_id, text, cursor]`.

### H.3 Tab (or any focus change) while composing: cancel

**Cancel, never auto-commit.** Tab is an explicit navigation intent; committing mid-composition can insert partial or wrong text into the field the user is leaving, and the user has no way to undo a move they did not ask for. Rules:

1. `Tab`/`Shift+Tab`/`Ctrl+Tab` are compositor navigation and are **never** routed to the IME process, so the IME cannot swallow Tab as a candidate-page key.
2. Before the focus move is executed, the compositor sends `IME_CANCEL` (100) to the IME process; the IME MUST drop the active composition and MUST NOT send a late `IME_COMMIT` for it. A late commit for a widget that no longer has focus MUST be discarded by the compositor.
3. `Escape` and `Enter` while composing are forwarded to the IME process unchanged (they are not navigation keys, §D.2): the IME decides cancel vs commit. That is where "commit on Enter" belongs, not in the compositor.
4. Arrows while composing are delivered to the widget per §D.3 rule 2 (INPUT consumes arrows) and therefore reach the IME process as `EVT_KEY`, so candidate/caret semantics stay with the IME app.
5. Disarming by window deactivation follows the same path (`WINDOW_ACTIVATE`/`HIDDEN` reasons): cancel first, never commit into a background window.

---

## I. LVGL mapping (LVGL 9.6.0, `deps/lvgl/`)

All APIs below were verified in the vendored tree; no function is invented.

### I.1 Groups = scopes

- **One `lv_group_t` per focus scope**: the window root scope, plus one per `GROUP`/`TRAP` container. (Today: one group for the whole compositor, `lvgl_backend.c:206`.)
- Widget creation: add the object to the group of its **nearest enclosing scope**, in compositor-defined ring order (`lv_group_add_obj(group, obj)`, `deps/lvgl/include/lvgl/core/lv_group.h:84`). Non-focusable widgets are **not** added at all (today every non-container widget is, `lvgl_backend.c:280`).
- Destroy / scope change: `lv_group_remove_obj(obj)` (`:97`), or `lv_group_remove_all_objs(group)` (`:103`) when a scope is rebuilt.
- Ring order inside LVGL is insertion order; `FOCUS_INDEX` is realised by removing and re-adding members in the computed order (bounded by the ring size, and only when an index attribute is present).
- `lv_group_set_focus_cb(group, cb)` (`:143`) plus the group's own `LV_EVENT_FOCUSED`/`LV_EVENT_DEFOCUSED` deliveries (`deps/lvgl/src/core/lv_group.c:257-265`, `:536-543`) feed the compositor's focus reports; the event callback already registered per object (`lvgl_backend.c:283`) is where the reason string is attached.
- Frozen rings: `lv_group_focus_freeze(group, true)` (`:128`) for every group outside a modal `TRAP`.
- `lv_group_set_default(group)` (`:71`) MAY be pointed at the active window's root group so widgets created later auto-enroll; explicit `lv_group_add_obj` is still required for scope containers and for ordering.

### I.2 Keys

- One KEYPAD indev per compositor (already exists, `lvgl_backend.c:207-210`). On window activation: `lv_indev_set_group(kb_indev, root_group_of_active_window)`; `NULL` when the active window has no focusable widget.
- Tab / Shift+Tab: LVGL consumes `LV_KEY_NEXT` (9) and `LV_KEY_PREV` (11) inside the group and calls `lv_group_focus_next/prev` (`deps/lvgl/src/indev/lv_indev.c:877-887`). Wrap via `lv_group_set_wrap(group, true)` (`lv_group.h:173`). Because LVGL has no modifiers, the backend maps `Tab + SHIFT` → `LV_KEY_PREV` and plain `Tab` → `LV_KEY_NEXT` before injection.
- Other keys: `lv_group_send_data(group, key)` (`:136`) delivers to the focused object — the LVGL default path (`lv_indev.c:905-908`) — which is exactly §D.3 rule 2 ("widget consumes arrows"). For a widget that does **not** consume arrows, the backend MUST intercept the arrow **before** `lv_group_send_data` and run its geometric `focus_dir()` instead (§D.3).
- Home/End: the same interception applies; for consuming widgets they are handed to the object.
- Scroll containers: `lv_obj_set_scroll_with_arrow(obj, true)` (`deps/lvgl/include/lvgl/core/lv_obj.h:336`, flag `LV_OBJ_FLAG_SCROLL_WITH_ARROW` at `:64`); arrows scroll, Tab leaves.

### I.3 Focus visuals

- Explicit, theme-independent focus ring: `lv_obj_set_style_outline_width/color/opa/pad(obj, ..., LV_PART_MAIN | LV_STATE_FOCUSED)` (`deps/lvgl/include/lvgl/core/lv_obj_style_gen.h:1986-2027`), applied to every focusable widget; unfocused state keeps outline width 0.
- On focus: `lv_obj_scroll_to_view(obj, LV_ANIM_OFF)` so the target is visible inside `SCROLL`/`LIST`/`TABLE` viewports; the compositor never scrolls on its own.
- Disabled widgets (`LV_STATE_DISABLED`) are skipped by every ring.

### I.4 Pointer

`lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE)` (`lv_obj.h:54`, `:239`) on focusable widgets; LVGL's `indev_click_focus` then calls `lv_group_focus_obj()` only when the pressed object is click-focusable and belongs to a group (`lv_indev.c:1761-1776`). This is precisely the desired "click focuses a widget, clicking a label does not" behaviour, so the backend does not need its own hit-test focus logic.

### I.5 `lv_gridnav` — considered, not used

`lv_gridnav` is enabled in the project config (`src/backends/lvgl/lv_conf.h:153`) and implements arrow-key navigation inside a container. It is **not** used for TGS arrow navigation because its focus cursor (`lv_gridnav_dsc_t.focused_obj`) is private: the public header exposes only `lv_gridnav_add` / `lv_gridnav_remove` / `lv_gridnav_set_focused` (`deps/lvgl/include/lvgl/indev/lv_gridnav.h:70-84`), with no accessor for the current inner focus. The compositor could therefore not learn which inner widget holds focus — which would break IME routing (§H) and `NTF_FOCUS`. A backend geometric `focus_dir()` over the current ring's members gives the same UX with an observable cursor.

### I.6 Prerequisite: one LVGL root per window

`backend_create_window()` currently returns `lv_screen_active()` (`lvgl_backend.c:245-255`), i.e. every window shares one root object, and `wm_backend_event` hardcodes `window_map[0].win_id` (`window_manager.c:360-364`). Per-window focus registries, per-window groups, freezing inactive windows and window activation all require each window to own a distinct root object (`lv_obj_create(lv_screen_active())`) and an id → root mapping. Until that lands, multi-window focus semantics are defined but not observable; single-window behaviour is unaffected.

---

## J. Protocol proposal

### J.1 New commands

| ID | Name | Direction | Stream | Args | Purpose |
|---|---|---|---|---|---|
| **38** | `TGS_CMD_SET_FOCUS` | app → compositor | 1 (COMMAND) | `window_id`, `widget_id` (`0` = clear) | Programmatic focus request |
| **40** | `TGS_CMD_WGT_ATTR` | app → compositor | 1 (COMMAND) | `widget_id`, `attr`, `value` | Behavioural widget attribute (focusable, arrow/tab consumption, focus index, scope kind) |
| **100** | `TGS_CMD_IME_CANCEL` | compositor → IME app | 4 (EVENT) | `win_id`, `widget_id` | Drop the active composition; sent before focus leaves an IME-armed widget |

Stream note: comp → app frames use stream 4 (`TGS_STREAM_EVENT`, spec §3), which is also what the compositor already uses for the IME key hand-off (`src/compositor/window_manager.c:403-434` writes `EVT_KEY` to `ime_pty_fd` on stream 4). `NTF_RESIZE` is currently written on stream 1 by `send_resize()` (`window_manager.c:140-142`), which contradicts spec §4.4; that inconsistency is pre-existing and belongs to the protocol worker, not to this design.

### J.2 Reused commands (no new ID)

| ID | Name | Direction | Change |
|---|---|---|---|
| 65 | `TGS_CMD_NTF_FOCUS` | comp → app | **Args become `[win_id, widget_id, focused, reason]`.** Currently declared but never emitted. Becomes the single focus channel. |
| 83 | `TGS_CMD_EVT_FOCUS` | comp → app | **Retired** (MUST NOT be emitted); id stays allocated. Superseded by 65 so that reason is always present. |
| 67 | `TGS_CMD_NTF_STATE` | comp → app | Unchanged; now also a trigger for focus restoration (§G.2). |
| 17 | `TGS_CMD_WIN_DESTROY` | app → comp | Unchanged; trigger for focus restoration (§G.2). |
| 35 | `TGS_CMD_WGT_DESTROY` | app → comp | Unchanged; trigger for successor focus with reason `DESTROYED`. |
| 81 | `TGS_CMD_EVT_KEY` | comp → app | Unchanged shape `[win_id, widget_id, key, mods]`; semantics tightened: it now carries **only keys not consumed by navigation** (§D.3). |
| 96/97 | `IME_PREEDIT` / `IME_COMMIT` | IME app → comp | Unchanged; preedit becomes an overlay, empty preedit clears it, late commits for unfocused widgets are discarded. |
| 34 | `TGS_CMD_WGT_STYLE` | app → comp | Unchanged: **appearance only**. Behaviour lives in `WGT_ATTR` (40). |

### J.3 Reserved (Layer 2 — do not allocate elsewhere)

| ID | Name | Direction | Planned args |
|---|---|---|---|
| 39 | `TGS_CMD_GET_FOCUS` | app → comp | `window_id` → reply `NTF_FOCUS` with reason `NONE` |
| 41 | `TGS_CMD_NAV_BIND` | app → comp | `key`, `mods`, `action` (per-app binding override) |
| 68 | `TGS_CMD_NTF_FOCUS_PRE` | comp → app | `win_id`, `widget_id` (pre-notification for a bounded veto window) |

`NTF_NAV` and `EVT_NAV` are **deliberately not introduced**: every navigational outcome is either a focus change (→ `NTF_FOCUS`, reason-carried) or an ordinary key/value event (→ `EVT_KEY`/`EVT_VALUE`). A third event family would duplicate both.

### J.4 Widget attributes (`TGS_CMD_WGT_ATTR`)

`attr` values (new enum `tgs_widget_attr`), `value` is int32:

| Value | Name | Value range | Applies to | Meaning |
|---|---|---|---|---|
| 0 | `TGS_ATTR_FOCUSABLE` | -1 / 0 / 1 | any widget | `-1` = type default (§A.3), `0` = never focusable, `1` = focusable |
| 1 | `TGS_ATTR_NAV_ARROWS` | -1 / 0 / 1 | any widget | `-1` = type default, `1` = widget consumes arrows, `0` = arrows always navigate from this widget |
| 2 | `TGS_ATTR_NAV_TAB` | 0 / 1 | any widget | `1` = the widget consumes Tab as text (multiline INPUT); escape hatch is `Ctrl+Tab` |
| 3 | `TGS_ATTR_FOCUS_INDEX` | -1 or ≥ 0 | any widget | explicit ring position (§B.2) |
| 4 | `TGS_ATTR_FOCUS_SCOPE` | 0 / 1 / 2 | containers only | `0` none, `1` GROUP (single tab stop), `2` TRAP (modal ring) |

Rules: attributes may be set at any time after `WGT_CREATE`; a write against an unknown widget id or a container-only attribute on a leaf widget is **ignored** (never partially applied, previous value retained); `value` outside the documented range is ignored the same way. Attribute writes do not themselves emit a focus event, except that a `FOCUS_SCOPE`/`FOCUS_INDEX`/`FOCUSABLE` change may reorder rings and therefore produces a normal focus pair on the next traversal if the focused widget was removed from its ring.

### J.5 Capabilities

| Token | Layer | Meaning | If unsupported |
|---|---|---|---|
| `nav.focus` | 1 | compositor-driven focus traversal, `SET_FOCUS`, `NTF_FOCUS` with reason | compositor answers `REJECT` with this token; the app must not send 38/40 |
| `nav.scope` | 1 | focus scopes on containers (`TGS_ATTR_FOCUS_SCOPE` ≠ 0), dialog focus trap | as above |
| `event.focus` | 0 (exists) | app receives focus notifications | already in the Layer 0 set |
| `nav.bind` | 2 | app-overridable key bindings (`NAV_BIND`) | `REJECT` |
| `nav.focus_veto` | 2 | pre-notification with a bounded veto window | `REJECT` |

Unsigned layers: an app that requests `nav.scope` without `nav.focus` is contradictory and MUST be rejected (`REJECT`, `nav.scope`). Nothing is silently degraded (principle #4): a compositor that cannot trap focus refuses `nav.scope` rather than pretending.

### J.6 Backend interface additions (`tgs_backend`)

Proposed additions to the abstract backend (mirrors §I; keeps the compositor's policy layer backend-agnostic):

```c
/* Focus & navigation — added to struct tgs_backend */
void  (*set_focus)(void *handle);                    /* focus a widget; NULL clears */
void *(*get_focus)(void);                            /* currently focused widget handle */
void  (*set_widget_attr)(void *handle, tgs_widget_attr attr, int32_t value);
int   (*focus_dir)(int dir);                         /* dir: tgs_nav_dir; returns 1 if focus moved */
void  (*set_active_window)(void *window_handle);     /* switch the keyboard group / freeze others */
void  (*set_widget_preedit)(void *handle, const char *text, int cursor);
```

`focus_dir` returns 0 when no candidate exists in that direction, which is the compositor-visible signal for rule 4 (residual forward). `inject_key`, `set_widget_style`, `create_widget`, `destroy_widget` are unchanged.

### J.7 Client API additions (proposal, `src/client/`)

```c
int tgs_client_set_focus(int win_id, int widget_id);                 /* → command 38 */
int tgs_client_get_focus(int win_id);                                /* cached from NTF_FOCUS, §E */
int tgs_client_set_widget_attr(int id, tgs_widget_attr attr, int32_t value); /* → command 40 */
```

`tgs_event` gains `int reason;` filled from `NTF_FOCUS`. The client maps frame 65 → `TGS_EVENT_FOCUS` / `TGS_EVENT_BLUR` (`src/client/tgs_client.c:421-430` is the current 83-based path and moves to 65).

### J.8 Wire examples

```
TGS;1;12;40;200;1;1        app → comp  WGT_ATTR  widget 200, NAV_ARROWS=1
TGS;1;13;38;1;200          app → comp  SET_FOCUS window 1, widget 200
TGS;4;9;65;1;100;0;1       comp → app  NTF_FOCUS window 1, widget 100 lost,  reason TAB
TGS;4;10;65;1;200;1;1      comp → app  NTF_FOCUS window 1, widget 200 gained, reason TAB
TGS;1;14;40;300;4;2        app → comp  WGT_ATTR  container 300 is a TRAP scope
TGS;4;11;100;1;200         comp → IME  IME_CANCEL window 1, widget 200
```

---

## K. Layer 1 vs later

Layering follows the roadmap: Layer 1 = floating/grid/split layouts + full widget library + resource URIs; Layer 2 = docking, scroll overlay, IME engine work; Layer 3 = multi-workspace, templates, drag-to-resize.

| Feature | Layer | Rationale |
|---|---|---|
| Focusable/non-focusable table + `TGS_ATTR_FOCUSABLE` | **1** | minimum for any keyboard use |
| Tree-order ring + wrap | **1** | deterministic default; no app work required |
| `TGS_ATTR_FOCUS_INDEX` | **1** | apps must be able to match the visual order |
| `Tab` / `Shift+Tab` traversal (compositor bindings) | **1** | core |
| Arrow hand-off to consuming widgets (`NAV_ARROWS`) | **1** | sliders/lists/inputs are in the Layer 1 widget library |
| Geometric spatial navigation (`focus_dir`) | **1** | arrow nav between non-consuming widgets |
| Pointer focus + window activation/restoration | **1** | dialogs and multi-window Layer 1 apps depend on it |
| `SET_FOCUS` + `NTF_FOCUS` with reason | **1** | the app contract |
| `TRAP` / `GROUP` scopes (`nav.scope`) | **1** | modal dialogs are Layer 1 |
| IME arm/disarm + cancel-on-focus-change | **1 ✓** | `IME_CANCEL` (100) implemented in the disarm path |
| IME preedit overlay (`set_widget_preedit`, §H.2) | **1 ⚠ gap** | invariant + protocol ready; backend method + LVGL overlay deferred — see §H.2 known-gap note |
| Key-code + modifier normalisation (§D.4) | **1** | prerequisite; currently broken |
| Per-window LVGL roots (§I.6) | **1** | prerequisite for multi-window focus |
| Focus outline style | **1** | focus must be visible to be usable |
| `Alt+Tab` window switcher | 2 | needs compositor chrome/overlay |
| `Tab` crossing window boundaries | 2 | needs an explicit window ring order |
| `NAV_BIND` per-app bindings (`nav.bind`) | 2 | defaults suffice for Layer 1 |
| `GET_FOCUS` cold query | 2 | client cache suffices |
| `NTF_FOCUS_PRE` veto (`nav.focus_veto`) | 2 | async protocol has no round-trip yet |
| Focus while dragging / drag-to-resize interplay | 3 | feature arrives in Layer 3 |
| Focus navigation inside `DROPDOWN` overlay / candidate window | 2 | overlay content is Layer 2 scroll/overlay work |

---

## L. Acceptance criteria

Testable statements. Each is observable from a TGS app (protocol frames or widget state) with the reference backend.

1. `Tab` moves focus to the next focusable widget in ring order; `Shift+Tab` to the previous; focus **wraps** at both ends of a window's ring.
2. Containers are transparent: with a `VLAYOUT` holding three buttons, the ring is the three buttons; `Tab` never stops on the container.
3. `LABEL`, `IMAGE` and `PROGRESS` are never focused by `Tab` and cannot be focused by `SET_FOCUS` (the request is ignored).
4. Arrows on a focused `SLIDER` change its value and emit no focus change; arrows on a focused `BUTTON` move focus to the neighbour in that direction.
5. Arrow navigation with no candidate in that direction forwards the key to the app as `EVT_KEY` (no focus change, no silent drop).
6. `SET_FOCUS` on a focusable widget emits exactly one lost frame and one gained frame with `reason = PROGRAMMATIC` (5); repeating the same `SET_FOCUS` emits nothing.
7. `FOCUS_INDEX` reorders traversal: two widgets with indexes 5 and 1 are visited 1 then 5, before unindexed widgets.
8. A `TRAP` container (or a `DIALOG` window) keeps `Tab` inside: after the last member, focus wraps to the first member; the frozen outer window receives no focus gain while the trap is active.
9. `Escape` is delivered to the focused app as `EVT_KEY` (key 27) and never moves focus; when the app destroys the modal window, the previously focused widget of the underlying window regains focus with `reason = WINDOW_RESTORE` (8).
10. A `GROUP` container is exactly one Tab stop: `Tab` into it focuses its remembered/first member; arrows move between its members; the next `Tab` leaves the container entirely.
11. Focus leaving an IME-armed `INPUT` emits `IME_CANCEL` (100) to the IME process, clears the preedit overlay, and emits no `IME_COMMIT` for the composition; the widget's committed text is unchanged by the preedit and by the cancel.
12. `Tab` while the IME app is composing cancels the composition and moves focus; `Tab` is never delivered to the IME process as `EVT_KEY`.
13. A late `IME_COMMIT` for a widget that no longer has focus is discarded (widget text unchanged, no `EVT_VALUE`).
14. Destroying the focused widget moves focus to the next ring member with `reason = DESTROYED` (10); destroying the last focusable widget clears focus (`widget_id = 0`, `focused = 0`).
15. Clicking a focusable widget focuses it (`reason = POINTER = 4`); clicking a `LABEL` inside the same window leaves the focused widget unchanged.
16. Hiding the active window (`NTF_STATE = HIDDEN`) moves focus to the next visible window's remembered widget with `reason = WINDOW_RESTORE` (8); re-activating the first window restores its remembered widget with `reason = WINDOW_ACTIVATE` (7).
17. A disabled focusable widget is skipped by `Tab` and by `focus_dir`, and rejects `SET_FOCUS`.
18. `HELLO` requesting `nav.scope` from a compositor that does not implement scopes yields `REJECT` with `rejected_cap = nav.scope` — no silent downgrade.
19. The focus outline is visible on exactly the focused widget, including after a pointer focus change and inside a `SCROLL` viewport (target scrolled into view).

---

## M. Open questions for the owner

Recorded here for the implementation hand-off; each has a recommended default already reflected above.

| # | Question | Recommended default |
|---|---|---|
| 1 | Should `Escape` on a `DIALOG` close it in the compositor, or stay app-driven? | App-driven (§C.3): the compositor has no app-facing "show" command, so a compositor-initiated hide would be irreversible for the app. |
| 2 | Is a generic error frame (`CMD_ERROR`) worth adding for rejected `WGT_ATTR`/`SET_FOCUS` requests? | No for Layer 1 — ignore + log; revisit in Layer 2 together with `NTF_FOCUS_PRE`. |
| 3 | Should `Tab` cross window boundaries? | No in Layer 1 — a window's ring wraps; cross-window traversal belongs with the window switcher (Layer 2). |
| 4 | Radio groups: should arrows walk the set? | No in Layer 1 — Space toggles, arrows navigate. LVGL's radio behaviour is opt-in per app. |
| 5 | Does `MENU` count as focusable? | Yes, treated like `LIST` (§A.3): it is an item ring, not a layout container. |
