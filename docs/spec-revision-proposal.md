# TGS Requirements Revision Proposal

**Date:** 2026-09-14 · **Basis:** `docs/requirements-rationale.md` (per-area verdicts) and
`docs/design-review.md` (feasibility evidence). This document turns the first-principles
critique into concrete edits to the requirements.

---

## 0. Product statement — the endpoint and the path to it

**The endpoint is a desktop graphics system.** TGS starts as a terminal-native widget toolkit and
evolves, rung by rung, into a compositing graphics system with client-shaded surfaces and
desktop-class window management. The earlier layers are the *path*, not a ceiling.

**The rungs (each reuses the previous):**

1. **Native-widget toolkit** (now). Declarative widget-tree commands as small text frames; the
   compositor renders with LVGL. A form is ≈100 bytes. The demos prove this rung.
2. **Client pixel surfaces.** An app gets a **widget-sized drawable region** — the "client surface"
   primitive — fed by a binary DCS frame (§5.4). This is where client-shaded pixels enter.
3. **Surface compositor.** Multiple overlapping surfaces with z-order, alpha, and transforms; LVGL
   renders the *widget* surfaces, the compositor scenes them together.
4. **Desktop graphics system.** Local clients share buffers zero-copy (shm / dmabuf, GPU-accelerated)
   and get full WM semantics (overlap, decorations, layout, workspaces). The terminal transport
   becomes the *remote* path, not the only one.

**Transport pluggability is the load-bearing requirement.** A *surface* is a buffer + metadata; how
it reaches the compositor is an implementation detail:

- **Remote client → binary DCS over PTY** (damage-tracked, compressed; the X11-forwarding economics
  apply — bounded updates, not a raw 60fps river).
- **Local client → shared memory (shm) or dmabuf (GPU)** — zero-copy, because client and compositor
  share a host.

**Honest constraint.** A desktop graphics system cannot be reached over PTY/SSH alone — that is
exactly why X11 forwarding was abandoned for local shm/GPU compositors. The terminal path delivers
the *toolkit* and the *remote* story; the desktop endpoint needs the compositor to own a local
display and accept local surfaces. TGS already has a display-owning compositor (`tgs_backend.h`:
SDL / `/dev/fb0`), so this is an extension, not a new system. Where LVGL stops being enough — scene
composition of independent surfaces — L4 introduces a light scene layer *beside* LVGL, not a
replacement for it.

TGS is **not** Wayland-over-SSH, and it is **not** merely a terminal toolkit. **TGS evolved from the
terminal character system; the character grid is the foundation**, and its surface model grows from
there into a desktop graphics window system.

**Self-consistency principle: everything above the server is a terminal program.** The compositor
is a *display server*: it owns the display, renders, routes input, and exposes surface/widget
primitives — **it holds no window-management policy.** The window manager is itself a TGS program:
a client that positions surfaces, draws decorations, and manages focus/layout/workspaces through
the same API every app uses (the X11 model — the WM is a client, not the server). Apps are TGS
programs. The IME is a TGS program. One primitive set, many programs — that is what keeps the design
self-consistent at every rung, from terminal toolkit to desktop graphics system.

---

## 1. Revision summary

| § | Verdict (`requirements-rationale.md`) | Action |
|---|---|---|
| §2 premise | RATIONAL (root ambiguity) | **REWRITE** — state the claim honestly: TGS evolves from the character terminal; **the char grid is the foundation** (not a fallback) |
| §4.1 terminal-manages / app-lays-out | HIDDEN-FLAW | **KEEP + SPECIFY** the missing resize→relayout loop |
| §4.2 capability negotiation | OVER-SCOPE | **SOFTEN** — drop "never silent degrade" |
| §5.1 window management | OVER-SCOPE *now* | **SERVER: primitives only; POLICY: a WM program.** The server exposes create / size-notify / focus / z-order / destroy / cleanup; decorations / layouts / workspaces are a **separate TGS program** at L4–L5, **deferred, not deleted** |
| §5.2 widget toolkit | RATIONAL | **KEEP** — the primary extension of the character base |
| §5.3 resources | OVER-SCOPE *now* | **SEQUENCE** — data/file/theme/builtin + in-memory cache at L2; https/remote/proxy/refcount at L4, **deferred, not deleted** |
| §5.4 framebuffer stream | RATIONAL AS EVOLUTION | **REDEFINE** — client pixel surface (a widget kind at L3, a first-class surface at L4) + binary transport; the first rung toward the endpoint |
| §5.5 events | RATIONAL (2 over-scope clauses) | **KEEP** kbd/mouse/touch; **CUT** 5 IME protocol commands + ≥10pt multitouch |
| §6.1 performance | HIDDEN-FLAW | **FIX MEASUREMENT** — add an end-to-end latency budget |
| §6.2 compat + degrade-to-char | RATIONAL (one-sided) | **KEEP + EXTEND** — char is the **foundation** (degrade-to-char is the base, not a failure); a char program is a **terminal surface** on a PTY |
| §6.3 security / §6.4 reliability | RATIONAL | **KEEP** |
| §7 progressive delivery | HIDDEN-FLAW | **RE-LAYER** — character base → toolkit → terminal surface → client pixel surface → surface compositor → desktop WM (see §3) |

---

## 2. Per-area detail

### §2 — Background & goal · REWRITE
- **Change.** Replace "make the terminal a lightweight graphics *platform*" with the honest claim:
  *TGS is an evolution of the terminal character system — the **character grid is the foundation**,
  and TGS extends it with escape-sequence-driven widget and pixel surfaces, over the terminal's
  existing transport.* Name the two meanings of "graphics" the text conflates (native widgets vs
  client-painted pixels) and state that **native widgets are the default; pixels are a later, opt-in
  surface.**
- **Guarantee — the base, not a fallback.** A TGS-unaware terminal shows the character base; a
  TGS-unaware program runs as a **terminal surface** (a char program on a PTY). Degrading to char is
  not a failure mode — it is the foundation. **The char grid is preserved as first-class, not
  replaced**, so existing TUIs keep working beside TGS surfaces.
- **Tradeoff.** The TUI contract is not broken (the grid survives as the base); the honest cost is
  that TGS is a *new display model layered on the old one* — more surface kinds to keep coherent.

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

### §5.1 — Window management · SERVER PRIMITIVES ONLY; POLICY IS A PROGRAM
- **Server (all rungs):** window create, **size notification**, **focus**, **z-order**, destroy, and
  crash/disconnect auto-cleanup. These are *mechanisms* — the server performs them, it does not
  decide them. The irreducible core the toolkit rungs need.
- **A WM program (L4–L5):** decorations, the four named layout types (tiled/floating/scroll/dock),
  split, nesting, state persistence, multi-workspace. These are *policy*, and policy belongs to a
  **window manager that is itself a TGS program**, using the same surface/widget API as any app
  (the X11 model: the WM is a client). Called "ported desktop thinking" before — the owner's
  desktop-graphics endpoint makes them the *target*. **Program it, do not bake it into the server;
  defer it, do not delete it.**
- **Tradeoff.** The server stays policy-free (consistent at every rung); the WM program is a real
  deliverable at L4–L5. Baking WM policy into the server would be exactly the desktop thinking the
  critique warned about — and would make the server non-neutral.

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

### Surface transport pluggability · NEW (required by the desktop endpoint)
- **Requirement.** A *surface* is a buffer + metadata; the transport is pluggable:
  - `remote` → binary DCS over PTY (damage-tracked, compressed) — for SSH clients.
  - `local-shm` → POSIX shared memory (zero-copy) — for same-host clients.
  - `local-dmabuf` → GPU buffer (zero-copy, accelerated) — L5.
- **Why.** The desktop endpoint is not viable over PTY/SSH alone; it needs local zero-copy surfaces.
  Making the transport an interface (not the protocol) lets L3 be remote-only while L4/L5 add local
  paths without touching the widget model.
- **Tradeoff.** Introduce a surface-transport abstraction (as `tgs_backend.h` is for displays); the
  compositor's surface input becomes an interface with ≥2 implementations.

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
- **Keep** ANSI/VT100, SSH/PTY transport, degrade-to-char. **Extend:** the char grid is the
  **foundation**, so compatibility is not a degrade path but the base itself — a character program is
  simply a program running in a **terminal surface** (PTY-backed, fed PTY-byte input), unchanged. Add
  the **app-side fallback contract** for a TGS app on a TGS-unaware terminal: it emits text, which is
  character content on the base.

### §6.3 / §6.4 — Security / Reliability · KEEP
- Unchanged.

### §7 — Progressive delivery · RE-LAYER
- Current L0 front-loads a WM (tiled layout, decorations, focus) before the toolkit is proven.
  Re-order so the toolkit is proven first (see §3).

---

## 3. Revised progressive delivery

**The base (below L0): character mode.** The cell grid (terminal emulator, ANSI/escape sequences) is
the foundation every rung rests on; no rung replaces it. The **terminal surface** is that base made
addressable.

| Layer | Content | Exit criterion (must demo) |
|---|---|---|
| **L0** | Single window + size notification + input/textarea/button/label + click/change/key events + **resize→relayout** | `simple_form` works end-to-end **and responds to a window resize** |
| **L1** | Styles + full 20+ widget library + container widgets + focus/navigation | `container_demo` renders + Tab/arrow/focus work with `NTF_FOCUS` |
| **L2** | Multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal, no protocol commands) + **terminal surface (char grid + PTY)** | Two windows coexist; an image renders; CJK input commits into a textarea; **`htop` runs in a terminal surface beside a TGS form** |
| **L3** | Pixel surface (a widget kind) + binary/length-prefixed frame transport + animation | An app opts into a pixel widget and paints into it |
| **L4** | Multi-surface scene compositor: overlapping client surfaces, z-order, alpha, transforms; **local shm/dmabuf transport** (zero-copy) + the remote DCS path | Two client surfaces overlap correctly — one local (shm), one remote (DCS) |
| **L5** | Desktop graphics system: a **WM program** — decorations, layouts, workspaces, launch/activate — built on the public surface API, plus a GPU path | A TGS **WM program** decorates and moves a local app's window beside a TGS widget window |

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
