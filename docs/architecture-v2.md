# TGS Architecture v2 — Display Server + Programs

> **Status:** target architecture. [`docs/architecture.md`](architecture.md) documents the *current*
> implementation (4 layers, PTY-only, single economy). This document describes where the design is
> going and how each rung gets there. Basis: [`spec-revision-proposal.md`](spec-revision-proposal.md),
> [`requirements-rationale.md`](requirements-rationale.md).

---

## 1. Role model — the self-consistency principle

**Everything above the server is a terminal program.** One primitive set, many programs.

| Role | What it is | Responsibility |
|---|---|---|
| **Display server** (the compositor) | one process owning a display | display ownership, rendering, input capture, input routing, **surface/widget primitives**, and *mechanisms* (raise/lower, focus delivery, resize notify, destroy, crash cleanup). **It holds no policy.** |
| **WM program** | a TGS client | window *policy*: decorations, layouts (tiled/floating), workspaces, focus policy, window list — through the public API. |
| **App programs** | TGS clients | widget trees + client pixel surfaces. |
| **IME program** | a TGS client (or a server-internal module) | text input transformation; the app sees only value changes. |

The server is policy-free and the WM is a client (the **X11 model**: the WM is not the server). This
is what removes the long-standing "is TGS a window manager or a toolkit?" ambiguity: **TGS is a
display server; the WM is the program you run on it.** Apps, the WM, and the IME all connect the
same way — a program with its own PTY to the server (`architecture.md:48`).

---

## 2. Responsibility matrix — mechanism vs policy

The split that keeps the server neutral:

| Concern | Server (mechanism) | Program (policy) |
|---|---|---|
| z-order | `raise` / `lower` / `stack` primitives | which surface is on top (**WM**) |
| focus | deliver focus events; per-surface focus state | who gets focus, on what gesture (**WM**) |
| geometry | surface rect + `NTF_RESIZE` | placement/layout (**WM**); content layout (**app**) |
| decorations | draw arbitrary surfaces (a frame is just a surface) | the decoration surfaces (**WM**) |
| workspace | show / hide surfaces | workspace membership (**WM**) |
| lifetime | create / map / unmap / destroy / auto-cleanup | when to create or destroy (**app**; **WM** for policy windows) |

If a decision needs taste (which window is on top, who gets focus), it is **policy** and belongs to a
program. If it is a fact about the machine (this surface exists, that buffer was resized), it is
**mechanism** and belongs to the server.

---

## 3. Surface model

A **surface** is the single primitive: an id, a buffer, a geometry rect (server-owned), an input
region, and a z-order slot. Everything visual is a surface.

Two kinds:

- **Widget surface** — server-rendered from declarative commands (`WGT_CREATE` …). The toolkit rung.
- **Client pixel surface** — the buffer is provided by the client. This is §5.4 *redefined*: a
  pixel-backed **widget** at L3, a first-class **overlapping surface** at L4. The app never owns the
  surface's geometry — it paints inside a rect it was granted.

**Lifecycle:** `create → map → draw/damage → resize (server→client notify) → raise/lower → unmap →
destroy`. On client crash or disconnect the server **auto-cleans** the surface (no orphans).

---

## 4. Transport — pluggable

A surface's buffer reaches the server through a **surface transport**, chosen by locality. This is
the load-bearing requirement for the desktop endpoint.

| Transport | Client | Copy cost | Framing |
|---|---|---|---|
| `remote` | SSH / PTY | damage-tracked + compressed | **binary DCS + byte-length prefix** on stream 3 |
| `local-shm` | same host | **zero-copy** | POSIX shared memory |
| `local-dmabuf` | same host | **zero-copy**, GPU | dma-buf (L5) |

The two payload classes already map onto two existing streams:

- **stream 2 RESOURCE** (images, fonts) → **base64 in the APC frame** — text-safe, no new parser,
  ≈ +33% size. Correct for occasional, bounded payloads.
- **stream 3 FRAMEBUFFER** → **binary DCS with a length prefix** — `ESC P TGSFB;<w>;<h>;<fmt>;<len> ST
  <len raw bytes> ESC \`. The length prefix is what makes raw binary unambiguous: the parser reads
  exactly `<len>` bytes, so any `ESC` inside is data, not a terminator. 0% overhead — required for a
  continuous stream.

> **Doc bug to fix:** `docs/protocol.md:38` lists stream 3 as `compositor → app`. The authoritative
> spec (`protocol/tgs-spec-layer0.md:51`) says `app → compositor` ("Direct framebuffer updates").
> `docs/protocol.md` must be corrected.

---

## 5. Input routing, focus, and IME

**Server:** captures input (keyboard, pointer, touch), routes it to the surface under the pointer or
holding focus, and delivers events that carry the surface id. It also maintains per-surface focus
*state* — but not focus *policy*.

**WM program:** decides focus (on click, on map, on raise) and tells the server which surface holds
focus. It sees the same event stream as apps.

**IME:** the server captures keys, applies the IME transform (internally, or by routing to an IME
program), and delivers **text** to the focused text surface. The app sees only `EVT_VALUE_CHANGED`;
for the app, IME input is indistinguishable from stdin. There are **no IME protocol commands**
(§5.5 cut — `IME_PREEDIT`/`IME_COMMIT`/`IME_CANDIDATES`/`IME_SELECT`/`IME_CANCEL` are removed).

---

## 6. The path — L0 → L5

Each rung reuses the previous; no rung is a lesser product.

| Layer | Adds | Exit criterion |
|---|---|---|
| **L0** | single window + size-notify + input/textarea/button/label + events + **resize→relayout** | `simple_form` works end-to-end **and responds to a resize** |
| **L1** | styles + full 20+ widget library + container widgets + focus/navigation | `container_demo` + Tab/arrow/focus with `NTF_FOCUS` |
| **L2** | multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal) | two windows coexist; an image renders; CJK input commits |
| **L3** | client pixel surface (a widget kind) + binary DCS transport + animation | an app opts into a pixel widget and paints into it |
| **L4** | multi-surface scene compositor (z-order/alpha/transforms) + transport pluggability (local shm/dmabuf zero-copy) | two client surfaces overlap — one local (shm), one remote (DCS) |
| **L5** | desktop graphics system: a **WM program** (decorations, layouts, workspaces, launch/activate) + GPU path | a TGS **WM program** decorates and moves a local app's window beside a TGS widget window |

---

## 7. Honest constraints

- **The desktop endpoint is not reachable over PTY/SSH alone.** That is why X11 forwarding was
  abandoned for local shm/GPU compositors. The terminal transport is the *remote* mode; the desktop
  endpoint needs local zero-copy surfaces. TGS already owns a display (`tgs_backend.h`: SDL /
  `/dev/fb0`), so this is an extension, not a new system.
- **The server must stay policy-free.** The moment window-management policy enters the server, the
  model stops being self-consistent (and the server stops being neutral).
- **LVGL is a widget toolkit, not a scene compositor.** L4 adds a light scene layer *beside* LVGL
  (compositing independent surfaces), not a replacement for it.

---

## 8. Open items

- **`docs/protocol.md:38`** — stream-3 direction contradicts the spec; correct it.
- **Naming** — `src/compositor/window_manager.c` currently holds only *primitives*. Rename it (e.g.
  `surface_registry.c`) so the name does not imply policy that now belongs to a WM program.
- **Transport interface** — introduce a `surface-transport` abstraction beside `tgs_backend.h`, with
  `remote` / `local-shm` implementations (≥2), so L3 can be remote-only and L4 adds local paths
  without touching the widget model.
- **§5.1 / §5.3 sequencing** — decorations/layouts/workspaces and the full resource subsystem are
  deferred to L4–L5, not deleted (see `spec-revision-proposal.md`).
