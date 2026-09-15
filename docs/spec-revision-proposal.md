# TGS Requirements Revision Proposal

**Date:** 2026-09-14 · **Basis:** `docs/requirements-rationale.md` (per-area verdicts) and
`docs/design-review.md` (feasibility evidence). This document turns the first-principles
critique into concrete edits to the requirements.

---

## 0. Revised product statement

**TGS is one toolkit with two rendering backends, not two products.**

- **Default path — native widgets.** Apps issue declarative widget-tree commands as small text
  frames; the compositor renders them with LVGL; the transport stays in the char-grid economy
  (a form is ≈100 bytes). This is what the demos prove and what the terminal's nature supports.
- **Opt-in, late path — a pixel surface.** An app that needs video/games/custom drawing gets a
  **widget-sized drawable region** fed by a **binary DCS frame with an explicit length prefix**;
  the compositor composites it alongside native widgets. This is the normal text→graphics
  evolution (Tektronix 4014 → ReGIS → Sixel → kitty/iTerm2), not a second product.

TGS is **not** a desktop window manager, and its pixel path is **not** Wayland-over-SSH: the
terminal owns geometry, the app paints inside a widget it was granted.

---

## 1. Revision summary

| § | Verdict (`requirements-rationale.md`) | Action |
|---|---|---|
| §2 premise | RATIONAL (root ambiguity) | **REWRITE** — state the claim honestly + add char-fallback guarantee |
| §4.1 terminal-manages / app-lays-out | HIDDEN-FLAW | **KEEP + SPECIFY** the missing resize→relayout loop |
| §4.2 capability negotiation | OVER-SCOPE | **SOFTEN** — drop "never silent degrade" |
| §5.1 window management | OVER-SCOPE | **SHRINK** to create + size-notify + focus + z-order + destroy + crash-cleanup |
| §5.2 widget toolkit | RATIONAL | **KEEP** (this is the product) |
| §5.3 resources | OVER-SCOPE | **SHRINK** to data/file/theme/builtin + in-memory cache + fallback |
| §5.4 framebuffer stream | RATIONAL AS EVOLUTION | **REDEFINE** — pixel surface as a widget + binary transport + late layer |
| §5.5 events | RATIONAL (2 over-scope clauses) | **KEEP** kbd/mouse/touch; **CUT** 5 IME protocol commands + ≥10pt multitouch |
| §6.1 performance | HIDDEN-FLAW | **FIX MEASUREMENT** — add an end-to-end latency budget |
| §6.2 compat + degrade-to-char | RATIONAL (one-sided) | **KEEP + EXTEND** — add an app-side char fallback |
| §6.3 security / §6.4 reliability | RATIONAL | **KEEP** |
| §7 progressive delivery | HIDDEN-FLAW | **RE-LAYER** — prove the toolkit before the WM |

---

## 2. Per-area detail

### §2 — Background & goal · REWRITE
- **Change.** Replace "make the terminal a lightweight graphics *platform*" with the honest
  claim: *extend the terminal's display model from a char grid to a widget surface, over the
  terminal's existing transport.* Name the two meanings of "graphics" the current text conflates
  (native widgets vs client-painted pixels) and state that **native widgets are the default;
  pixels are a later, opt-in surface.** Add the guarantee: **a TGS-unaware terminal degrades to
  its char grid, and a TGS app that loses TGS falls back to text output.**
- **Tradeoff.** Admits that overlaying a widget surface on a char grid breaks the grid contract
  with existing TUIs — a bigger claim than "extend escape sequences".

### §4.1 — Terminal manages windows; app manages layout · KEEP + SPECIFY
- **Keep** the split (sound). **Specify** the missing feedback loop:
  - `NTF_RESIZE` must be delivered on the **event stream** (it is currently sent on the command
    stream and silently dropped by the client — `design-review.md` §B).
  - Define the app's relayout hook: on resize the app may reposition/resize its widgets;
    the terminal's **auto-scale is the fallback only**, not the primary mechanism.
- **Tradeoff.** More protocol/spec surface; without it "responsive layout" (FR-4.1.6/4.1.7) stays
  unimplementable and the demo's ignoring of `win.x/y/w/h` stays the norm.

### §4.2 — Capability negotiation · SOFTEN
- **Keep** negotiation; **delete "NEVER silent degrade".** Replace with: *each capability has a
  declared fallback; the app adapts to the negotiated subset.*
- **Tradeoff.** Apps must handle capability absence (they already do in practice). But this is the
  terminal's 50-year tradition (`TERM=dumb`, unknown-sequence-ignored) — hard rejection fights the
  ecosystem TGS lives in.

### §5.1 — Window management · SHRINK
- **Keep (rational core):** window create, **size notification**, **focus**, **z-order**,
  destroy, and crash/disconnect auto-cleanup.
- **Cut:** window decorations, the four named layout types (tiled/floating/scroll/dock),
  split, nesting, state persistence, multi-workspace.
- **Tradeoff.** Loses desktop-WM behavior. A terminal is one surface; demanding a WM's semantics
  inside it is ported desktop thinking, and none of it is exercised by any demo.

### §5.2 — Widget toolkit · KEEP
- Unchanged. This is the product: 20+ widget types, styles, event binding, absolute + % coords,
  container widgets that carry their own layout.

### §5.3 — Resources · SHRINK
- **Keep:** `data:` (inline), `file:` (whitelist), `theme:`, `builtin:`, an **in-memory cache**,
  and **fallback on failure**.
- **Cut → later layers:** `https:`, `remote:` + proxy, reference counting, three-tier
  (mem/disk/net) cache.
- **Tradeoff.** No network-loaded images initially. The full resource subsystem is its own product;
  the toolkit needs image + cache, not a CDN.

### §5.4 — Framebuffer stream · REDEFINE (do not cut)
- **Change.** Redefine as: *an app may request a **pixel-backed widget** (a drawable region).*
  - The framebuffer is a **widget kind**, not a peer economy — the terminal still owns its geometry.
  - It is a **late layer** (L3), **opt-in**, with native widgets the default.
- **Transport split (decided).** Two payload classes get two transports:
  - **Embedded images / resource bytes → base64 in the existing APC frame** (kitty / iTerm2 style).
    Text-safe — base64 is `A–Z a–z 0–9 + / =`, which cannot collide with the frame's `;` delimiter
    or the `ESC \` terminator — needs no new parser, costs ≈ +33% size. Fine for occasional,
    bounded payloads.
  - **Framebuffer stream → binary DCS with an explicit byte-length prefix**, e.g.
    `ESC P TGSFB;<w>;<h>;<format>;<len> ST <len raw bytes> ESC \`. The length prefix is what makes
    raw binary unambiguous: the parser reads exactly `<len>` bytes, so any `ESC` inside is data, not
    a terminator. **0% overhead** (vs +33% for base64), no escaping — required for a continuous,
    high-bandwidth stream.
- **Tradeoff.** Requires a second frame parser (binary DCS) alongside the APC one. Justified: base64
  on a 60fps framebuffer would waste a third of the terminal's scarcest resource (SSH bandwidth),
  while base64 on an occasional image costs nothing measurable. Bandwidth itself is still managed by
  **dirty-region incremental sends** (spec §5.4) and optional compression — this decision fixes
  *framing*, not *volume*.

### §5.5 — Events · KEEP + CUT
- **Keep:** mouse, keyboard, single/2-point touch, widget events, routing rules, window-id per event.
- **Cut the five IME-specific protocol commands** (`IME_PREEDIT`/`IME_COMMIT`/`IME_CANDIDATES`/
  `IME_SELECT`/`IME_CANCEL`). The app sees only `EVT_VALUE_CHANGED`; **for the app, IME input is
  indistinguishable from stdin.** IME is a compositor-internal input transformation: preedit and
  candidate UI are rendered by the compositor; committed text enters the textarea by the same path
  as keyboard input. The IME engine may still be a separate process; its protocol surface shrinks
  to "receive keys, produce text".
- **Cut ≥10-point multitouch** (no SSH transport carries it; 10 points is tablet-scale).
- **Tradeoff.** No per-app IME customization; the IME engine can no longer drive candidate UI
  through the protocol — the compositor owns it.

### §6.1 — Performance · FIX MEASUREMENT
- **Keep the numbers; add an end-to-end latency budget** (keypress → visible change), because the
  app sits on the critical path of every event. Per-hop targets (5ms dispatch, 16ms resize) can all
  pass while the perceived interaction still feels remote.
- **Tradeoff.** The end-to-end budget is harder to meet; it forces the reactive model to be honest.

### §6.2 — Compatibility · KEEP + EXTEND
- **Keep** ANSI/VT100, SSH/PTY transport, degrade-to-char. **Extend:** add an **app-side fallback
  contract** — what a TGS app does when the terminal lacks TGS (it currently has no char-grid
  rendering path, so the "degrade" only protects the terminal, not the app).

### §6.3 / §6.4 — Security / Reliability · KEEP
- Unchanged.

### §7 — Progressive delivery · RE-LAYER
- Current L0 front-loads a WM (tiled layout, decorations, focus) before the toolkit is proven.
  Re-order so the toolkit is proven first (see §3).

---

## 3. Revised progressive delivery

| Layer | Content | Exit criterion (must demo) |
|---|---|---|
| **L0** | Single window + size notification + input/textarea/button/label + click/change/key events + **resize→relayout** | `simple_form` works end-to-end **and responds to a window resize** |
| **L1** | Styles + full 20+ widget library + container widgets + focus/navigation | `container_demo` renders + Tab/arrow/focus work with `NTF_FOCUS` |
| **L2** | Multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal, no protocol commands) | Two windows coexist; an image renders; CJK input commits into a textarea |
| **L3** | Pixel surface (a widget kind) + binary/length-prefixed frame transport + animation | An app opts into a pixel widget and paints into it |

---

## 4. Migration from the current code

Immediate deletions (nothing else depends on them):

1. **`src/common/tgs_protocol.h`** — delete `TGS_CMD_IME_PREEDIT` (96), `IME_COMMIT` (97),
   `IME_CANDIDATES` (98), `IME_SELECT` (99), `IME_CANCEL` (100). Deprecate `TGS_CMD_NTF_STATE`
   (67) unless a comp→app state channel is still wanted.
2. **`src/client/tgs_client.h` / `.c`** — delete `tgs_client_send_ime_commit`,
   `tgs_client_send_ime_preedit`, `tgs_client_send_ime_candidates`, `tgs_client_send_ime_select`,
   `tgs_client_send_ime_cancel`.
3. **`src/compositor/window_manager.c`** — delete the `ime_cancel` call and the
   `TGS_CMD_IME_COMMIT` / `TGS_CMD_IME_PREEDIT` handling. **Keep** the focus registry and `nav.*`.
4. **`examples/ime_app.c`** — rework: the IME engine is compositor-internal; drop all `IME_*`
   frame usage. (The "IME is an application" principle can survive as a compositor-internal module
   or a text-injecting helper.)
5. **`docs/ime.md`** — rewrite to "IME is a compositor-internal input transformation; the app sees
   only text."

Deprecations (keep the code, narrow the spec): the four named window layouts collapse to "tiled is
the only L0/L1 layout"; §5.3's richer schemes and §5.4's app-facing stream are marked "later layers".

**Do not delete:** `src/backends/output_fb.c` — that is the compositor's **own display** backend
(`/dev/fb0`), *not* the §5.4 app-facing framebuffer stream. They are unrelated.

No code exists yet for §5.3 (full resource subsystem) or §5.4 (app-facing framebuffer) — so those
revisions are "do not build as specified", not deletions.

---

## 5. What stays exactly as-is

So the reader knows the core is untouched:

- The **widget toolkit** (§5.2) — types, styles, event binding, coordinates, containers.
- The **event core** (§5.5) — mouse/keyboard/touch/widget events, routing rules.
- **Compatibility** (§6.2) — ANSI/VT100, SSH/PTY, degrade-to-char.
- **Security** (§6.3) and **reliability** (§6.4).
- The **focus/navigation model** (`src/compositor/nav.*`) and the **SDL / FB display backends**.
- The **server-side-widget architecture** — it is the product's differentiator and the demos prove it.
