# TGS Architecture v2 — Display Server + Programs

> **Status:** target architecture. [`docs/architecture.md`](architecture.md) documents the *current*
> implementation (4 layers, PTY-only, single economy). This document describes where the design is
> going and how each rung gets there. Basis: [`spec-revision-proposal.md`](spec-revision-proposal.md),
> [`requirements-rationale.md`](requirements-rationale.md).

---

## 0. Priority — pure character programs first

**The first thing TGS must do is run ordinary character programs, unchanged.** `bash`, `vim`, `htop`,
`tmux` — programs that know nothing about TGS — must work with **zero adaptation**. This is not a
compatibility mode bolted on later: it is the **base**, and everything graphics is layered above it.

**And one program may be both at once.** A program's connection to the server is a single byte
stream, read as:

- **ordinary bytes → its character region** (the cell grid, its `stdout`), and
- **TGS APC frames (`ESC _ TGS;… ESC \`) → directives** (create a widget, bind an event, …).

So **every TGS program is a terminal program**: text is the default reading of the stream, and TGS
frames are control interleaved into it — exactly how a terminal already mixes text with escape
sequences. A pure character program is the case with **no TGS frames**; it just works. A program that
wants graphics interleaves frames. **One program can print a line, create a widget, and print again —
character and graphics in the same process, on the same stream, at the same time.**

## 1. Role model — the self-consistency principle

**Everything above the server is a terminal program.** One primitive set, many programs.

| Role | What it is | Responsibility |
|---|---|---|
| **Display server** (the compositor) | one process owning a display | display ownership, rendering, input capture, input routing, **surface/widget primitives**, and *mechanisms* (raise/lower, focus delivery, resize notify, destroy, crash cleanup). **It holds no policy.** |
| **WM program** | a TGS client | window *policy*: decorations, layouts (tiled/floating), workspaces, focus policy, window list — through the public API. |
| **App programs** | TGS clients | character output (the default) **and/or** widget trees + client pixel surfaces — one program may be both at once (§0). |
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

**TGS evolved from the terminal character system: the character grid is the foundation.** Widget and
pixel surfaces are *extensions layered on that base* — the same move Sixel and Kitty graphics make on
a character terminal. A TGS-unaware program still works (it is a character program on the base); a
TGS-unaware terminal still works (it shows the base).

The **base is a cell grid** — cells of char + attributes, driven by ANSI/escape sequences. It is
always present and always the reference model. On top of it, richer content is placed in surfaces:

A **surface** is a region of the display: an id, a content buffer, a geometry rect (server-owned), an
input region, and a z-order slot. Each richer content kind is a surface *over the base*:

- **Character content (the base).** A region carrying the cell grid itself. A **terminal surface**
  binds such a region to a PTY running an **ordinary character program** (`bash`, `vim`, `htop`,
  `tmux`) — the base made addressable, so existing TUIs keep running unchanged.
- **Widget surface (extension).** Server-rendered from declarative commands (`WGT_CREATE` …) — the
  character model upgraded to interactive controls.
- **Client pixel surface (extension).** The buffer is provided by the client. This is §5.4 *redefined*:
  a pixel-backed **widget** at L3, a first-class **overlapping surface** at L4. The app never owns the
  surface's geometry — it paints inside a rect it was granted.

**Coexistence is the point — at two scales.**

- **Across programs:** a TGS form, a pixel surface, and an `htop` in a terminal surface can be visible
  at once.
- **Within one program:** a single program emits character output *and* TGS frames on its one stream,
  so it can print text and drive widgets/pixels simultaneously (§0). It is not "a char app" *or* "a
  TGS app" — it is a terminal program that may use graphics.

TGS is thus a *superset* of the character terminal — and §6.2's "degrade to char" becomes **the base it
always rests on**: a TGS-unaware program is simply a program running on character content.

**Lifecycle:** `create → map → draw/damage → resize (server→client notify) → raise/lower → unmap →
destroy`. On client crash or disconnect the server **auto-cleans** the surface (no orphans).

### Nesting — recursive composition (the completion principle)

**A complete terminal graphics window system is composed by multi-level nesting.** A surface is the
primitive, and a surface may be backed by:

- server-rendered widgets, or
- client pixels, or
- **a nested display server** — a full TGS server running as a *client program* inside the surface.

Because the server is just a program (§1), it can itself be a client of another server. So a surface
can host a whole window system, which hosts surfaces, which can host another window system —
recursively, to arbitrary depth:

```
terminal → server → surface → nested server → surface → nested server → …
```

Every level is the *same primitive set*; nesting adds isolation (a session, a container, a
window-in-a-window) — exactly as tmux panes or Xephyr-in-X11 extend their hosts.

**What nesting forces the design to get right — and therefore proves it complete:**

- **Hierarchical input routing** — an event goes to the topmost surface under the pointer; a nested
  server consumes events within its rect and re-routes them to its own children.
- **Coordinate spaces** — each nested server has its own origin inside the rect it was granted.
- **Hierarchical focus** — outer focus = the nested server's surface; inner focus = a widget inside it.
- **No special cases** — a nested server is an ordinary client, so nesting needs *no* mechanism beyond
  the surface, transport, and input primitives already defined.

**Completeness test:** when a TGS server can run as a TGS program inside a TGS surface and behave
correctly — input, focus, and geometry right at every level — TGS is a complete terminal graphics
window system, not a toolkit with a window feature bolted on.

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

**Terminal surfaces** receive input as **PTY bytes**, not as widget events: the server encodes the
focused terminal surface's input (keys, pointer reports, paste) into the byte stream its child
expects (e.g. arrow → `ESC [ A`), exactly as a terminal emulator does. An ordinary char program gets
what it always got; the rest of the system never sees the difference.

---

## 6. The path — L0 → L6 (to a complete window system)

Each rung reuses the previous; no rung is a lesser product.

**The base (below L0): character mode.** The cell grid — the terminal emulator, ANSI/escape
sequences — is the foundation every rung rests on. No rung replaces it; each extends it. The
**terminal surface** (a char program on a PTY) is that base made addressable, and it can land early
because it is what a terminal already is.

| Layer | Adds | Exit criterion |
|---|---|---|
| **L0** | single window + size-notify + input/textarea/button/label + events + **resize→relayout** | `simple_form` works end-to-end **and responds to a resize** |
| **L1** | styles + full 20+ widget library + container widgets + focus/navigation | `container_demo` + Tab/arrow/focus with `NTF_FOCUS` |
| **L2** | multi-window + focus/z-order + resources (`data`/`file`/`theme`/`builtin` + in-memory cache) + IME engine (internal) + **terminal surface (char grid + PTY)** | two TGS windows coexist; an image renders; CJK input commits; **`htop` runs in a terminal surface beside a TGS form** |
| **L3** | client pixel surface (a widget kind) + binary DCS transport + animation | an app opts into a pixel widget and paints into it |
| **L4** | multi-surface scene compositor (z-order/alpha/transforms) + transport pluggability (local shm/dmabuf zero-copy) | two client surfaces overlap — one local (shm), one remote (DCS) |
| **L5** | desktop graphics system: a **WM program** (decorations, layouts, workspaces, launch/activate) + GPU path | a TGS **WM program** decorates and moves a local app's window beside a TGS widget window |
| **L6** | **Nesting** — a TGS server runs as a TGS program inside a TGS surface: a window system inside a window, recursively | a TGS server runs inside a TGS pixel surface; input, focus, and geometry are correct at both levels |

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
