# TGS Design Review — First Principles, Anchored in the Demos

**Date:** 2026-09-14 · **Scope:** `examples/simple_form.c`, `examples/container_demo.c`, protocol, client, compositor, the old rendering backend, docs.
**Method:** every claim cites the working tree. Verdicts are decisive: **sound** / **latent-risk** / **against-YAGNI**.

---

## A. What the system fundamentally is

The two demos reveal the real core contract. An app is a *declarative client*: it
writes a flat sequence of widget-tree commands (`WIN_CREATE`, `WGT_CREATE`,
`EVT_BIND`, `WGT_UPDATE`) to a single PTY as APC-framed text, the compositor
parses those frames and renders the whole tree **server-side** onto
SDL/`/dev/fb0`, and the app is driven by a blocking `poll_event(-1)` loop that
receives input events back over the same stream
(`examples/simple_form.c:23-62`, `examples/container_demo.c:28-73`).

**Verdict: sound.** This is a **server-side-widgets** model — the classic Xlib /
network-UI-toolkit shape — not the modern Wayland shape where the client renders
pixels and the compositor only composites them. The architecture doc agrees
(`docs/architecture.md:5` — "Applications build UIs through a byte-stream
protocol"). Nothing in the demos contradicts this, and nothing in the code
implements the client-paint path (see §F). The honest consequence: TGS is a
remote UI toolkit with a terminal transport, and its hard limits are the limits
of text-framed widget commands (§C).

## B. "Terminal manages window structure; app manages layout" — validated against the demos

### Who decides what (per the demos)

| Decision | Decided by | Evidence |
|---|---|---|
| Window existence, type, title, **id** | App (client invents the id!) | `simple_form.c:28`; `tgs_client.c:234` (`next_window_id++`) |
| Widget type, id, parent, **all coordinates** | App, as literal constants | `simple_form.c:35-48` (`20,60,300,40` …); `container_demo.c:40-59` |
| Event bindings | App | `simple_form.c:51` |
| Window geometry | Compositor, but = full display, sent once | `window_manager.c:342-361` (`wm->disp_w/h`), called only at `WIN_CREATE` (`:440`) |
| Z-order / activation / focus | Compositor | `window_manager.c:441-442`, `:249-319` |
| Rendering | Compositor (backend) | `scene/scene_backend.c:347-428` |

So the app manages **both** structure and layout; the compositor manages
stacking, focus and pixels. The principle "terminal manages structure" holds
only in the weak sense (compositor owns stacking/focus); the demos show the app
creating windows and assigning ids, i.e. structure too.

### The blocking question: does the app respond to `win.x/y/w/h` at all?

**No — in both directions.** Trace it:

1. The client *does* receive the window rect: `create_window` blocks on
   `NTF_RESIZE` and fills `tgs_window_info` (`tgs_client.c:247-260`).
2. Both demos use only `win.window_id`; `win.x/y/w/h` are never read
   (`simple_form.c:28,35-48`; `container_demo.c:33-59`).
3. `NTF_RESIZE` can never reach a running app: `poll_event` filters to
   `TGS_STREAM_EVENT` only (`tgs_client.c:425`) and drops `NTF_RESIZE` (sent on
   stream 1, `window_manager.c:358`) in `default: continue` (`:498-500`).
4. The compositor has no resize path: `send_resize` has exactly one caller
   (`window_manager.c:440`); the SDL window is created non-resizable
   (`output_sdl.c:26-28`, flags `0`); `backend_set_size`
   (`scene/scene_backend.c:329-333`) has **zero** callers.
5. Widget rects are absolute pixels applied unconditionally
   (`window_manager.c:509` → `scene/scene_backend.c:426-427`). Only the window root
   scales (`LV_PCT(100)`, `scene/scene_backend.c:363`); children never reflow on
   display change, and the protocol exposes no percentage/relative sizing
   (`WGT_CREATE` rect args are plain ints, spec §4.3 line 84).

**Verdict: FR-4.1.6/4.1.7 responsive layout is aspirational, not real.** It is
not just unimplemented — it is unreachable from both ends (no event to the app,
no relayout in the compositor). The demos are truthful to what exists: fixed
windows, fixed pixels.

## C. Framing / transport, from first principles

Wire format: `TGS;<stream>;<frame>;<cmd>;<arg>;...` (spec §2.2 line 31),
semicolon-delimited, args UTF-8 strings, "Maximum 16 arguments per frame, each
≤ 128 bytes" (spec line 42), **no escaping** (encode is `snprintf(out+written,
";%s", args[i])`, `tgs_frame.c:27-32`; decode splits on every `;`,
`tgs_frame.c:41-56`).

Two hard consequences, both verified in code:

1. **`;` in content breaks framing.** A content arg containing `;` is split
   into extra fields; the receiver reads the truncated prefix and silently
   ignores the tail (decode stops consuming known fields). No error is
   reported anywhere on either side.
2. **Soft 128 B / hard ~2 KB ceiling.** The declared 128 B/arg limit is not
   enforced per-field; the real limit is the fixed payload buffer
   `TGS_MAX_ARGS*TGS_MAX_ARG_LEN + 64` = 2112 B on both encode
   (`tgs_frame.c:110`) and decode (`tgs_frame.c:60,66`). Any payload ≥ 2112 B
   fails the whole frame; `tgs_frame_encode` returns -1 and most callers ignore
   it (e.g. the demo discards `tgs_client_update_widget`'s return,
   `simple_form.c:60`).

**Verdict: fine for the demos (short ASCII only), latent-risk for the product.**
This caps what the protocol can ever carry. It will bite at: resource transfer
(stream 2, spec:50), the framebuffer stream (stream 3, spec:51 — pixel data is
impossible under this framing; see §F), IME composition text and any i18n input
> ~128 B, and multiline content. **Recommended fix (scoped to Layer 2):**
length-prefix or escape content args, base64 for binary payloads. Do not churn
Layer 0 — the demos prove the simple form works; extend the transport when the
first real payload needs it.

Also note: the spec's §4.4/§4.5 argument tables are already out of sync with
the implementation — `NTF_RESIZE` documented as `width, height` (spec:126) is
sent as `[win_id, 0, 0, w, h]` (`window_manager.c:350-356`); `EVT_CLICK`
documented as `widget_id, x, y, button` (spec:135) is sent as `[win_id,
widget_id]` (`window_manager.c:684-689`) and parsed that way
(`tgs_client.c:431-434`); `WIN_CREATE` documented as `type, title` (spec:74)
carries an app-invented `win_id` (`tgs_client.c:234`). The spec is a draft and
says so (spec:4), but as the wire contract it must be reconciled before Layer 2.

## D. Text ownership — the demo's core loop

The greeting "Hello, %s!" (`simple_form.c:58`) depends on
`tgs_client_get_widget_text` (`:56`), which reads a **client-side shadow cache**
(`tgs_client.c:511-515`). The input text exists in three places: the backend
textarea (authoritative render state, `scene/scene_backend.c:722`), the `EVT_VALUE`
frames pushed on every change (`scene/scene_backend.c:719-729` →
`window_manager.c:691-697`), and the cache.

**Verdict: latent-risk — the split is a classic justified pattern (server
authoritative + client shadow), but as built the shadow has no sync path.**

- The cache is written in exactly one place: the `EVT_VALUE` branch of
  `poll_event` (`tgs_client.c:447-461`).
- The demo never binds `VALUE_CHANGED` — it works **only** because `EVT_BIND`
  is a no-op and *all* events are forwarded (`window_manager.c:589-591`). The
  app's core logic is a side effect of an event it never asked for. The first
  real `EVT_BIND` implementation silently breaks the demo.
- No sync path exists: no `GET_TEXT` command (command 39 is reserved for
  `GET_FOCUS`, `tgs_protocol.h:42`); the cache is never refreshed from the
  compositor; it is not updated by `create_widget(content)` or
  `update_widget` (`tgs_client.c:265-305`).
- Frames on the "wrong" stream are **consumed and dropped** during blocking
  calls: `read_stream_frame` discards any frame whose stream doesn't match
  (`tgs_client.c:145-150`). In a multi-window app, `create_window`'s wait for
  `NTF_RESIZE` (`:247-250`) silently eats concurrent `EVT_VALUE`/`NTF_FOCUS`
  frames — permanent cache divergence with no recovery.

**Cleanest fix (small):** add a synchronous `GET_TEXT`/reply pair (mirror the
reserved `GET_FOCUS` slot), so the app reads the authority instead of a shadow.
Secondary: have the demo read text it already knows (pass the typed value along
in the click handler) — but that only patches the demo; the API hazard remains
for any real app.

## E. What the demos do NOT exercise

| Machinery | Built where | Demo proof | Verdict |
|---|---|---|---|
| Multi-window (per-window roots/groups, `window_slot`, z-order activation, `NTF_STATE`) | `scene/scene_backend.c:347-396`; `window_manager.c:427-444`; `NTF_STATE` defined `tgs_protocol.h:50` | none — one window, type 0, always | **built, unproven**; `NTF_STATE` is never emitted anywhere in `src/` |
| Window resize / relayout | `send_resize` (`window_manager.c:342-361`) | none — sent once at create | **aspirational** (see §B) |
| Navigation (Tab/arrows, `NTF_FOCUS`, reasons) | `wm_nav_key` (`window_manager.c:249-319`), `emit_focus` (`:35-54`), client focus cache (`tgs_client.c:463-478`) | none **bound** — the demos ignore `FOCUS`/`BLUR` events (`simple_form.c:54-62`), though Tab *will* move focus silently | **implemented, demo-blind**; also `docs/navigation.md:20-21` ("NTF_FOCUS never emitted", "compositor does not track focus") is stale vs. current code |
| Styles / `WGT_ATTR` / `SET_FOCUS` | client `tgs_client.c:307-360`; compositor `window_manager.c:542-551,593-643` | none | **built, unproven** (trivially provable) |
| `WGT_LAYOUT` (37) | client `tgs_client.c:557-570` sends it; backend setter wired `scene/scene_backend.c:545-561,862` | none | **dead end-to-end**: `wm_handle_frame` has no case 37 → silently dropped at `default` (`window_manager.c:664-665`) |
| Widget types beyond LABEL/INPUT/BUTTON/VLAYOUT/HLAYOUT | 21 in enum (`tgs_protocol.h:105-127`), names mapped (`window_manager.c:363-393`) | 5 used | **built, unproven** (trivially provable) |
| Caps negotiation | HELLO/READY/REJECT (`main.c:139-156`; client `tgs_client.c:178-223`) | both demos run the fixed handshake | **ceremony** — see below |
| `EVT_BIND` | `window_manager.c:589-591` | demo relies on it being a no-op | **against-YAGNI** as shipped: real filtering breaks the demo's cache (§D) |

**Caps negotiation is ceremony, not negotiation.** Both sides send the same
hardcoded constant (`TGS_CAPS_LAYER0`, `tgs_protocol.h:11-13`); the compositor
sends `READY` unconditionally after *any* HELLO (`main.c:151-154`), never emits
`REJECT`, and the client never inspects READY's caps
(`wait_for_ready_or_reject`, `tgs_client.c:166-174`). Two concrete rot spots:
the compositor detects the IME app by `strstr(args[1], "ime=true")`
(`window_manager.c:419-424`) but **nothing in the tree sends that token** — the
standard client always sends `TGS_CAPS_LAYER0` (`tgs_client.c:196-199`), and
`ime_app.c` uses the standard init (`ime_app.c:14`), so `ime_connected` can
never become true and IME key routing (`window_manager.c:225-228`) is dead
until a producer exists. And the advertised token `layout.tiled`
(`tgs_protocol.h:12`) describes a tiling policy that does not exist (windows
are full-screen stacked roots). **Verdict: against-YAGNI as written — either
implement token checking (version gate is the only part that matters today) or
downgrade caps to a version string.**

## F. The two-economy tension (most important first-principles finding)

The demos are pure server-side widgets. But the protocol reserves a
`RESOURCE` stream (spec:50) and a `FRAMEBUFFER` stream for apps that paint
pixels themselves (spec:51), and the requirement doc promises a GPU/CPU dual
path with "apps can paint video/games/charts". These are **two different
products**:

1. **Widget economy** — declarative commands, compositor renders (proven by both demos).
2. **Client-paint economy** — the app owns pixels, the compositor composites (unproven, and transport-impossible today: pixel data cannot pass a `;`-delimited 2112 B text frame, §C).

The spec treats them as one stream multiplex — "single-stream multiplexed"
(spec:8) — which hides the fact that economy 2 requires a transport redesign
(escape/length-prefix/base64, likely a separate channel, plus damage
tracking) and a different compositor role (compositing instead of widget
rendering). Building both ambiguously in one protocol is how neither gets
feedback.

**Verdict: latent-risk — name it and layer it.** Recommendation: make economy 1
the feedback-able path *now* (the demos are it), and define economy 2 as a
strict Layer-2 surface that reuses the transport fix from §C — a
paint-your-own-surface app gets a framebuffer *window* inside the same WM
(widget model stays the shell), rather than a parallel rendering authority.

## G. Container-as-widget, critiqued from the demo

`container_demo.c` gives every flex child an explicit `(0,0,w,h)`
(`container_demo.c:44-59`) *and* declares the parent a flex container by type
(`:40-49`). The compositor applies the rect unconditionally
(`window_manager.c:509` → `scene/scene_backend.c:426-427`), then the backend applies its (now-retired) layout); grid children get cell-placed, `x,y` again dead
(`scene/scene_backend.c:409-411`). So the app hands a flow container both a layout
mode and coordinates that the layout engine ignores — the protocol conflates
absolute rect and layout mode.

**Verdict: real API smell.** From first principles, a flow container owns child
geometry; absolute rects and flow layout are mutually exclusive intents.

**Clean split:** (a) inside a flow container, `WGT_CREATE` takes a size hint
(`w,h`, `x,y` unused/forbidden) and the container's layout mode owns
placement — align/gap expressed per-container, which the protocol cannot do
today; or (b) containers with explicit rects are absolute-only (e.g. `SCROLL`),
with flow containers never accepting child rects. Either is a small spec+client
change and makes the demo's `(0,0,...)` boilerplate disappear.

## H. Ranked recommendations

| # | Finding | Evidence | Why it matters (first principles) | Minimal next step to prove-or-cut | Size |
|---|---|---|---|---|---|
| 1 | **Text framing: no escaping, ~2 KB ceiling** | spec:42; `tgs_frame.c:27-32,41-56,60,110`; `simple_form.c:60` ignores send failure | Content with `;` or >128 B silently corrupts or fails; gates i18n, resources, FB (§C, §F) | Layer-2 content args: length-prefix/base64; keep Layer 0 as-is | med |
| 2 | **Text ownership: shadow cache with no sync path** | `tgs_client.c:447-461,511-515,145-150,265-305`; `window_manager.c:589-591` | Demo correctness is a side effect of unfiltered event forwarding; any `EVT_BIND` or multi-window change breaks it silently | Add synchronous `GET_TEXT`/reply (mirror reserved 39); make the demo read text it typed only if GET lands in L2 | small |
| 3 | **Responsive layout is aspirational** | `tgs_client.c:425,498-500`; `window_manager.c:440,358`; `output_sdl.c:26-28`; `scene/scene_backend.c:329,426-427,363` | Spec FR-4.1.6/4.1.7 promises what both ends structurally cannot express today | Either add runtime `NTF_RESIZE`→app + relative sizing, or delete the FR claim and document fixed-size contract | med |
| 4 | **Container API conflates rect + layout** | `container_demo.c:44-59`; `window_manager.c:509`; `scene/scene_backend.c:203-212,409-411` | App writes coordinates the layout engine ignores; two contradictory intents in one call | Size-hint + container layout split (spec §4.3.1, client `create_widget`) | small |
| 5 | **Two economies conflated in one protocol** | spec:50-51; §C limits | Widget economy proven; client-paint economy transport-impossible today; building both ambiguously starves feedback | Declare economy 1 the feedback path; frame economy 2 as L2 framebuffer window on the widget WM | med |
| 6 | **Caps negotiation is ceremony; `ime=true` has no producer** | `main.c:151-154`; `tgs_client.c:196-199`; `window_manager.c:419-424,225-228`; `ime_app.c:14` | Dead routing path and fake contract both sides "negotiate" without reading | Implement token/version check or downgrade to version-only; fix or remove IME detection | small |
| 7 | **`WGT_LAYOUT` (37) dead end-to-end** | `tgs_client.c:557-570`; `window_manager.c:664-665`; `scene/scene_backend.c:545-561` | Shipping API that silently no-ops erodes trust in the contract | Wire case 37 in `wm_handle_frame` + one demo, or delete command 37 | small |
| 8 | **Unproven weight: multi-window/`NTF_STATE`/styles/`WGT_ATTR`/`SET_FOCUS`/15 widget types** | `tgs_protocol.h:47-51,105-127`; `window_manager.c:542-643`; `scene/scene_backend.c:347-396` | Spec compliance weight with zero behavioral proof | One 2-window demo (proves WM, states, nav); one widget-gallery demo; delete what neither proves | med |
| 9 | **Spec drift: §4.4/§4.5 arg tables ≠ implementation** | spec:126,135,74 vs `window_manager.c:350-356,684-689`; `tgs_client.c:234` | Wire contract is the doc of record; it currently lies | Reconcile tables to implementation, then freeze Layer 0 | small |
| 10 | **Doc rot: `docs/navigation.md` §0 contradicts code** | `docs/navigation.md:20-21` vs `window_manager.c:35-54,249-319` | Design docs claiming "never emitted" for shipped code mislead the next layer | Refresh §0 as-built table after Layer-1 nav lands | small |

**Top line:** the demos prove a coherent, working server-side-widgets core —
credit where due. But they also hide that the system's three riskiest claims
(responsive layout, text read-back, client-paint economy) are each broken or
aspirational today, and that roughly half the API surface ships without a
single behavioral proof. The order of work should be: fix text framing and
text ownership (they gate every real app), make the demos truthful about
layout, then prove-or-cut the WM/nav/resource weight.

---

*Everything above is grounded in the working tree at commit `ac1aa60`
("feat: Layer 1 widgets + explicit containers + nav"). Line numbers refer to
that tree.*
