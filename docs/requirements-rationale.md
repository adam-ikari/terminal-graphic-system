# TGS Requirements Rationale — First-Principles Judgment

**Question under judgment:** should these things be *required at all* of a terminal
graphics system? Not "is the implementation feasible" — feasibility evidence from
`docs/design-review.md` is cited only where it sharpens an argument about whether the
requirement itself follows from the nature of a terminal.

**Method.** Greenfield judgment, no historical deference. Each requirement is measured
against the terminal's irreducible defining values:

1. **Zero-deploy** — the terminal already exists on every remote host; nothing to install.
2. **SSH-native / byte-stream transport** — escape sequences over PTY/SSH; low-bandwidth,
   char-oriented, ordered, reliable but high-latency.
3. **Char-grid contract** — the display is a grid of cells shared with every existing TUI
   (tmux, vim, htop, readline). Breaking that contract breaks the ecosystem TGS lives inside.
4. **Single surface** — one display, one focus, one stream of keystrokes.
5. **Robustness over degradation** — 50 years of `TERM=dumb`, unknown-sequence-ignored,
   graceful fallback; the terminal survives broken peers.

A requirement is **RATIONAL** when it follows from these. **OVER-SCOPE** when it ports
desktop/legacy thinking that the single-surface, low-bandwidth terminal does not support.
**FATAL-CONTRADICTION** when it makes the spec self-inconsistent against its own premises.
**HIDDEN-FLAW** when the requirement looks sound but is underspecified or measures the
wrong thing in a way that defeats its stated goal.

---

## §2 — Background & goal

**Verdict: RATIONAL (with a root ambiguity the doc does not admit).**

The premise is irreducible and correct: the terminal is the only universal remote-access
surface that is already deployed everywhere, and escape sequences over SSH/PTY are
50-year-validated, ordered, reliable transport. Extending escape sequences to carry
widget-creation commands is exactly the move a terminal-native system makes — it reuses the
transport and the deployment story instead of inventing a new one. "No GPU dependency, no
extra deploy" is the honest differentiator: a TGS app works inside a plain `ssh` session
where X11 forwarding, VNC, and a browser cannot. That is a real product position and it
follows from the terminal's nature.

The caveat — larger than the doc admits — is that the goal says "make the terminal a lightweight
graphics *platform*" without distinguishing the two meanings of "graphics" it smuggles in:
(a) **server-side widgets** (small text frames, the compositor renders) and (b) **client-painted
pixels** (a raw framebuffer over the wire). Those two carry separate transport profiles and
separate ownership models, and §2 reconciles neither. §2's goal statement is the seed from which
§5.4's transport gap grows: by leaving "graphics" unscoped, it lets a pixel-stream requirement
sit inside a widget-toolkit spec with no transport of its own. A greenfield reader who took §2
alone would build the widget economy and stop; §2 does not itself give pixels a way to travel.

**Evidence.** §2 text: "extend escape seqs to make terminal a lightweight graphics
platform… no GPU dep, no extra deploy." `design-review.md:174-196` (§F) names the exact
two-economy tension §2 leaves unresolved: "widget economy (declarative, compositor
renders)" vs "client-paint economy (app owns pixels, compositor composites)." §2 is
rational in spirit; its failure to bound "graphics" is the root ambiguity.

---

## §4.1 — Terminal manages windows; app manages layout

**Verdict: HIDDEN-FLAW.**

The ownership *division* is rational and follows from single-surface: the compositor owns
the one display, so it must own window geometry — the app cannot position a window it
cannot see. That much is sound. The flaw is the *responsive loop* the requirement erects on
top of it: "app lays out widgets per size (responsive); if app doesn't respond to resize,
terminal auto-scales widgets (proportional, text wrap/truncate)." This loop is asserted but
never specified as a protocol contract — there is no defined timeout, no detection of
non-response, no statement of what "proportional" means against a tree whose coordinates the
spec elsewhere allows as absolute pixels (`WGT_CREATE` rect args are plain ints).

Worse, the loop is not merely underspecified — it is vacuous as designed.
`design-review.md:45-69` (§B) traces both directions and finds them dead: the client
receives the window rect once at `WIN_CREATE` but `NTF_RESIZE` is never delivered to a
running app (`poll_event` filters to `TGS_STREAM_EVENT` and drops `NTF_RESIZE`,
`tgs_client.c:425`), the compositor's `send_resize` has exactly one caller (create-time),
the SDL window is non-resizable, `backend_set_size` has zero callers, and widget rects are
absolute pixels applied unconditionally with only the root scaling. "App manages layout" is
aspirational, and "auto-scale if no response" is undefined against an absolute-pixel model
where there is no flex contract to scale (§G of the review shows flow and absolute
coordinates are conflated — the app hands a flow container `x,y` the engine ignores).

So the requirement as written asks for a resize→notify→relayout→auto-scale pipeline, gives
no enforceable contract for any of its four stages, and the design proves none of them can
fire. The director called this "underspecified"; the evidence says it is stronger — the loop
does not exist in either direction.

**Evidence.** §4.1 text: "Terminal notifies app actual size within 16ms… App lays out
widgets per size (responsive)… auto-scales widgets." `design-review.md:45-69` (§B, the
blocking-question trace); `design-review.md:198-218` (§G, absolute/flow conflation that
makes "proportional auto-scale" undefined).

---

## §4.2 — Capability negotiation (hard-no-silent-degrade)

**Verdict: OVER-SCOPE.**

Capability negotiation *itself* is rational: a versioned protocol over unknown peers needs
a handshake so each side knows what the other speaks. That part follows from the
byte-stream-over-SSH premise. What does not follow — and actively fights the terminal's
nature — is the clause "**unsupported = explicit reject, NEVER silent degrade**." The
terminal's 50-year survival strategy *is* silent graceful degradation: `TERM=xterm-256color`
falls to `xterm` falls to `ansi` falls to `dumb`, and applications keep working at each
rung. Forcing hard reject means an app that declares a capability the terminal lacks simply
cannot run — over the very SSH links where partial operation is the whole point. That is
desktop-package-manager thinking (feature gates → install failure) ported onto a substrate
whose ethos is "render what you can."

The layer-level negotiation the spec describes (L0/L1/L2) is the right granularity and a
degraded handshake belongs there: if the terminal supports only L0, an L2 app should be
told and should *itself* decide to fall back, not be rejected outright. "NEVER silent
degrade" also contradicts §6.2's own "degrade to char-terminal when TGS unsupported" — the
spec degrades the *terminal* silently at the transport boundary but forbids degrading the
*app* at the capability boundary. That asymmetry is incoherent. As built, the negotiation is
worse than over-scope: `design-review.md:155-172` (§E) shows both sides send the same
hardcoded constant, the compositor never emits `REJECT`, and the advertised `layout.tiled`
token describes a policy that does not exist — ceremony, not negotiation.

**Evidence.** §4.2 text: "unsupported = explicit reject, NEVER silent degrade." §6.2 text:
"degrade to char-terminal when TGS unsupported" (the contradicting silent-degrade).
`design-review.md:155-172` (§E, caps is ceremony; never rejects).

---

## §5.1 — Window management (full WM)

**Verdict: OVER-SCOPE.**

A terminal has **one surface**. Everything in a window manager that presumes multiple
independent, freely-overlapping, persistent surfaces is desktop-WM thinking with no
substrate to land on. On one surface, the irreducible window contract is: **create,
size-notify, focus/activate, destroy, crash-cleanup.** That is it. Z-order collapses to
"which full-screen root is on top" (the demos are exactly full-screen stacked roots,
`design-review.md:149`), focus is "who gets the keystrokes," and states (min/max/full) are
trivially one state (full) for a single surface.

The over-scope is everything else the spec demands *as requirement*: four layout types
(tiled h/v/grid, floating, scroll/overlay, dock), decorations, split, nesting,
persistence, memory, multi-workspace, groups. Tiled/floating/dock are distinctions that
require a free-form surface to mean anything — a dock implies reserved screen edge on a
multi-window desktop; "floating" implies windows that overlap and are moved by a pointer
across a persistent canvas. None of that exists on one SSH-driven cell grid. Workspaces and
persistence are session-manager features, not graphics-platform features. `design-review.md`
shows the multi-window machinery, `NTF_STATE`, split and nesting are all "built, unproven"
(§E) with zero demo exercise — the spec is requiring a WM no app has needed.

The rational core — create + geometry + focus + destroy + auto-cleanup — is already
covered by §6.4 (reliability) and §4.1 (geometry). The desktop-WM superstructure is
disposable. **Refinement of the director's frame:** z-order/focus/activate are *not* over-scope
on a single surface — they are the irreducible multi-window contract. Only
decorations/docks/workspaces/split/nesting/persistence are the ported desktop thinking. The
director's "minimal = create+size+focus+destroy" is correct; I add that focus entails
z-order, so the minimal set already contains it.

**Evidence.** §5.1 text: "≥4 layout types… dock… nesting… persistence/memory."
`design-review.md:145-156` (§E, multi-window/`NTF_STATE`/split built and unproven);
`design-review.md:149` ("windows are full-screen stacked roots").

---

## §5.2 — UI controls (widget toolkit)

**Verdict: RATIONAL.**

This is the product. A server-side widget toolkit is exactly what makes a terminal a
"lightweight graphics platform" *without pushing pixels over the wire*: the app emits small
declarative commands, the compositor renders, and the transport stays in the char-grid
economy. The requirement for ≥20 widget types (base + containers + complex) is a reasonable
scope for a toolkit that wants to be usable rather than a toy — a toolkit with only buttons
is not a platform. Styles (color/radius/font), event binding, and content update are the
irreducible CRUD surface of any retained-mode UI. Nothing here violates the terminal's
defining values; this is the requirement that *expresses* them.

The one smell — absolute + percentage coordinates conflated in one `WGT_CREATE` — is an API-
design defect, not a requirements-level problem; the requirement to *support* both absolute
and relative sizing is rational, the implementation just conflates them
(`design-review.md:198-218`, §G). "Auto-arrange (should)" and "layout templates + switch on
size" are appropriately downgraded to should/可, so the requirement is not over-reaching.

**Evidence.** §5.2 text: "≥20 types… base/containers/complex… styles… event bind… abs + %
coords." `design-review.md:8-25` (§A, "server-side-widgets model, classic Xlib/network-UI-
toolkit shape" — this is the honest core); `design-review.md:198-218` (§G, the abs/flow
conflation, an API smell not a requirements failure).

---

## §5.3 — Resources (7-scheme URI + proxy + refcount + 3-tier cache)

**Verdict: OVER-SCOPE.**

A widget toolkit must load images and themes — that is rational and irreducible. What is not
irreducible is a full resource subsystem: seven URI schemes (`file`/`https`/`theme`/`builtin`/
`data`/`app`/`remote-proxy`), a remote-filesystem proxy, reference counting with
release-at-zero, and memory/disk/network cache tiers. That is a separate product — a CDN and
cache layer — grafted onto a terminal. Over SSH, the host already *has* a filesystem and
`curl`; making the terminal itself perform HTTPS downloads and maintain disk+network caches
reimplements infrastructure that exists, inside a process whose job is rendering widgets.

The rational minimal is: `data:` (<10KB inline), `file:` (whitelist), `theme:`/`builtin:`
(bundled), plus a single in-memory cache and a load-fallback. Those follow from "a widget
needs an image and a fallback." The `remote:` proxy (fetch from a remote filesystem via the
terminal) and the disk/net cache tiers do not follow from the terminal's nature — they
follow from "what if the app is thin and the terminal is fat," which is the X-terminal model,
not the terminal-graphics model. Reference-counting is an implementation detail, not a
requirement; demanding it at the spec level is premature. The permission/size-limit surface
is real but belongs to §6.3 (security), not to a resource-URI enumeration.

**Evidence.** §5.3 text: "≥7 URI schemes… remote(proxy from remote fs)… cache(mem/disk/net)…
refcount→release at 0." `design-review.md:85-100` (§C, the ~2KB frame ceiling — the current
transport cannot carry resource payloads at all, confirming the resource subsystem is
aspirational scaffolding ahead of its transport).

---

## §5.4 — Framebuffer stream

**Verdict: RATIONAL AS EVOLUTION** — the goal is sound; the defects are transport, modeling, and
staging. *(Revised after owner feedback: an earlier draft called this a "fatal contradiction."
That was wrong.)*

The owner's position — **终端从字符系统演变为图形系统是一条正常的演变路径** (a terminal evolving
from a character system to a graphics system is a normal evolutionary path) — is correct, and the
precedent is forty years old:

| Era | System | Mechanism |
|---|---|---|
| 1970s | Tektronix 4014 | vector graphics over escape sequences |
| 1980s | DEC ReGIS | graphics over escape sequences |
| 1982 | DEC Sixel | raster graphics over DCS |
| today | kitty / iTerm2 / WezTerm / foot | inline images over APC/OSC + base64 |

Each carries pixel data over the SAME PTY/SSH transport, opt-in, via a **dedicated escape namespace
carrying binary/base64**. "Pixels over PTY" is neither novel nor impossible — it is how terminals
have grown graphics for forty years.

So the requirement's *goal* is rational. Its *defects* are three, none of which is "graphics is
wrong":

1. **Transport.** The current text / `;`-delimited / no-escaping / ≈2KB framing cannot carry pixels
   (`design-review.md:85-100`, §C). Every terminal graphics protocol solved exactly this with a
   dedicated namespace for bulk data (DCS for Sixel, APC for kitty). TGS's real error is using ONE
   text format for everything — there is no binary namespace. **Fix: transport split** — small
   control payloads keep the text frame format; bulk data (framebuffer, resource bytes) gets a new
   length-prefixed/binary frame type. Additive, not a rewrite.

2. **Modeling.** The framebuffer must be a **widget (a drawable region)**, not a peer economy. The
   app paints inside the widget's rect; the terminal still owns geometry and placement — exactly
   X11's window/WM split. Under that modeling §4.1 is *not* contradicted: the terminal owns the
   surface, the app paints inside a widget it was granted, and the compositor keeps rendering
   (a widget is a widget whether native or pixel-backed).

3. **Staging.** The graphics path is a LATE layer, opt-in, with native widgets as the DEFAULT. It
   does not invert §2's low-bandwidth premise for apps that do not use it; only pixel-pushing apps
   pay the bandwidth. §2's "low-bandwidth" describes the default path and the control plane, not a
   ceiling on every app.

**Evidence.** §5.4 text; §2 premise; §4.1 ownership; Tektronix 4014 / DEC ReGIS / DEC Sixel / kitty
graphics protocol (graphics over the terminal's own transport); `design-review.md` §C (today's
transport cannot carry it — a fixable defect) and §F (two economies — reconcilable as two widget
backends, not two products).

---

## §5.5 — Events

**Verdict: RATIONAL (two over-scope clauses: the five IME-specific protocol commands, and ≥10-point
multitouch).**

The back-channel is irreducible: any UI toolkit needs input events, and the terminal's
input vocabulary — keyboard sequences, mouse (SGR mouse mode), focus changes — maps cleanly
onto the spec's keyboard/mouse/widget events. Routing rules (kbd→focus window, mouse→window
under cursor, window-id in every event) are the minimal correct contract. The widget events
(click/change/focus/blur) are the toolkit's own feedback loop and belong here.

**IME — the engine is rational; the protocol surface is over-scope.** CJK input over SSH is a real,
common need, and IME must run somewhere; the terminal owns the keystream, so a compositor-side
engine is the honest place for it. But the owner's first-principles insight cuts the *protocol*
cost: **对应用来说输入法的输入和 stdin 没有区别** — for the application, IME committed text is
indistinguishable from keyboard input. The app sees only `EVT_VALUE_CHANGED` (text in its textarea
changed); it does not know or care whether the characters came from a keypress, an IME commit, or a
paste. Therefore the five IME-specific protocol commands are over-engineering:

- **Commit** — the committed text enters the textarea by the SAME path as keyboard input
  (insert-text into the focused widget → `EVT_VALUE_CHANGED`). No dedicated command.
- **Preedit** — the compositor renders it in the textarea (LVGL supports preedit); no protocol event.
- **Candidate window** — the compositor renders it as an LVGL widget; no protocol event.
- **Cancel** — the compositor handles it internally on focus loss; no protocol event.

Cut all five commands (`IME_PREEDIT`/`IME_COMMIT`/`IME_CANDIDATES`/`IME_SELECT`/`IME_CANCEL`) and the
matching client API. The IME engine may still be a separate process (the "IME is an application"
principle holds) — its protocol surface shrinks to "receive keys, produce text", the same text a
keyboard would produce.

The one clause that does not follow is **touch with ≥10-point multitouch.** The director's
stated reason ("terminals lack touch") is *slightly too absolute* — embedded framebuffer
backends read `/dev/input/event*` via evdev (`architecture.md:68-72`) and can see touch
devices — so the substrate is not strictly touchless. But the requirement is still
over-scope: (a) no standard escape sequence carries multitouch over SSH/PTY, so the
transport cannot deliver it on the terminal's defining link; (b) ≥10 simultaneous points is
large-touchscreen/tablet territory, not terminal territory (a single-user SSH session is one
pointer at a time). Single-touch or 2-point gesture might be rational for the embedded FB
path; 10-point multitouch as a *requirement* is ported tablet thinking.

**Evidence.** §5.5 text: "Touch(down/move/up, ≥10pt multitouch)… kbd→focus, mouse→window
under cursor." `architecture.md:68-72` (evdev input on FB backend — touch substrate exists
on embedded, but not over SSH transport); `design-review.md:145-156` (§E, IME routing is
built but the `ime=true` token is never sent — IME is rational but currently dead).

---

## §6.1 — Performance

**Verdict: HIDDEN-FLAW.**

Having performance budgets is rational — an interactive UI that is slow is a failed UI. The
per-hop budgets (CPU<1ms/widget, event dispatch<5ms, win op<16ms) are individually
reasonable. The hidden flaw is that *every budget is per-hop, and the architecture puts the
app on the critical path of every event.* The loop is: input → compositor → PTY → app → PTY
→ compositor → render. The app is a *separate process* reached over a PTY
(`architecture.md:48`), and over SSH each PTY hop is 10–50ms of latency before the app's
code even runs. So even if every hop meets its NFR (5ms dispatch + ~1ms app + 16ms render),
the *perceived* input-to-photon latency is the sum of two round-trips over the transport —
easily 40–100ms+ on a real SSH link, where the spec's own §5.5.3 NFR demands <1ms event
dispatch (an internal inconsistency, since §6.1 says <5ms).

The requirement measures the wrong thing. A responsive terminal UI is bounded by end-to-end
input latency, not by any single hop's budget. The spec never sets an end-to-end target, so
it can meet every NFR and still feel laggy — the failure mode it most needs to prevent.
The rational fix is an end-to-end input-to-frame budget (e.g., <50ms local, <100ms over LAN
SSH) that the per-hop budgets must sum under, plus a server-side-interpolation story for the
round-trip gap (predictive focus movement, immediate visual feedback the app later
confirms). Without that, per-hop budgets are a feel-good metric that cannot fail in the way
that matters.

**Evidence.** §6.1 text: "event dispatch<5ms (NFR says <1ms in §5.5.3)… 60fps simple."
`architecture.md:48` (app as separate process over its own PTY — app is on the critical
path); `design-review.md:111-137` (§D, the blocking `poll_event` loop and frames dropped on
the wrong stream — the event path is already lossy under blocking calls).

---

## §6.2 — Compatibility (ANSI/VT100, degrade-to-char, multi-transport)

**Verdict: RATIONAL.**

This is the requirement that keeps the spec honest as a *terminal* system rather than a new
display server wearing a terminal costume. Its three pillars each follow from the terminal's
defining values. (1) **ANSI/VT100 baseline + unknown-seq-ignored** is the 50-year
robustness contract — the same tolerance that let VT100 survive every malformed sequence
since 1978; a TGS that broke on unknown bytes would brick the host terminal. (2) **Degrade
to char-terminal when TGS unsupported** is the one requirement that respects the char-grid
contract with every existing TUI: if the terminal cannot do TGS, it falls back to being a
plain terminal, and tmux/vim/htop keep working. (3) The transport list (SSH/PTY/pipe/serial/
WebSocket) follows directly from the byte-stream premise.

**But the director missed that this honesty is one-sided.** §6.2 degrades the *terminal*;
it does not degrade the *apps*. When the terminal falls to char-mode, a TGS widget app has
no char-grid rendering path — it is widget-only and simply cannot display. A truly
terminal-honest spec would require that the widget toolkit be able to render to a char grid
as a fallback (a curses/notcurses path), so that a TGS app degrades *with* the terminal rather
than vanishing. The spec protects the substrate and strands every app on it. This is the
most important finding the director's thesis under-weights: §6.2 is rational but incomplete.

**Evidence.** §6.2 text: "ANSI/VT100, unknown seq ignored… degrade to char-terminal when TGS
unsupported… SSH/PTY/pipe/serial/WebSocket." `design-review.md:8-25` (§A, the system is a
server-side widget toolkit — there is no char-rendering fallback for the app, only for the
terminal).

---

## §6.3 — Security

**Verdict: RATIONAL.**

A terminal graphics platform accepts untrusted byte-stream apps that, through §5.3's
resources, can fetch files and network — so the trust boundary is real and the terminal is
it. App sandbox isolation, file whitelist, TLS cert verification, declared permissions,
and resource-size DoS limits are the irreducible security surface for that posture. Nothing
here is ported desktop thinking; it follows from "untrusted code reaches the host's
filesystem and network through a byte stream." Cut any of these and the platform is an
attack surface.

**One hidden interaction the director did not flag** (requirements-level, not impl): §5.2
requires *server-side* widget rendering — the compositor process runs the widget library
(LVGL) on behalf of every app. So a malicious/buggy widget (e.g., an image decode overflow
in LVGL) executes *inside the compositor*, not inside the app sandbox. The "app://
isolation" in §6.3 isolates app-to-app but not app-to-compositor, because the rendering
authority is shared. The rational consequence is that the sandbox boundary is drawn wrong:
isolation must also cover the server-side render path, or widget rendering must be
per-app-faultable. This is a design consequence of §5.2+§6.3 interacting, visible at the
requirements level.

**Evidence.** §6.3 text: "app:// isolation… file whitelist… net cert verify… resource size
limit." `design-review.md:8-25` (§A, compositor renders the whole tree server-side — the
render authority is in the trusted process); `architecture.md:48` (apps are separate
processes, but all render through one compositor).

---

## §6.4 — Reliability

**Verdict: RATIONAL.**

Over SSH/PTY, connections drop and apps crash. A long-lived terminal session shared by many
apps cannot be bricked by one app dying. Crash auto-cleanup and disconnect auto-cleanup are
the irreducible reliability contract for a multi-app platform on one surface — and they are
exactly the §6.2 robustness value applied to lifecycle instead of transport. Load-fallback
(a missing resource renders a placeholder) and parse-tolerant (bad command ignored, not
fatal) are the same "survive the broken peer" ethos. None of this is desktop thinking; it
is the terminal's defining robustness applied to app lifecycle. The only caveat is that
"crash auto-cleanup" presupposes the §5.1 WM's resource tracking exists — and §5.1 is
over-scope — but the *reliability requirement* itself is sound and should survive any
trimming of the WM.

**Evidence.** §6.4 text: "Crash auto-cleanup… disconnect auto-cleanup… load fallback… parse
tolerant." `design-review.md:145-156` (§E, the cleanup machinery is among the "built,
unproven" set — the requirement is rational, the proof is not yet there).

---

## §7 — Progressive delivery (layering)

**Verdict: HIDDEN-FLAW.**

Progressive delivery is rational — ship the smallest feedback-able slice, learn, extend.
The flaw is *what L0 chooses to prove first.* L0 includes "tiled layout + decorations"
alongside base widgets — i.e., it front-loads WM machinery. But the demos
(`design-review.md:8-25`, §A; `:145-156`, §E) prove only the widget economy: a single
full-screen window, base widgets, events, and a one-shot size notify. The rational L0 is
exactly what the demos prove: **single-window + base widgets + base events + resize-notify.**
Tiling and decorations are WM features that presume multi-window and a persistent canvas —
exactly the over-scope of §5.1 — and putting them in L0 means you cannot get feedback on
the toolkit (the actual product) before building WM scaffolding that may be cut entirely.

The deeper problem: because §5.4 (framebuffer) is L2, the spec *does* defer the fatal
contradiction to a late layer — which is good instinct — but L0/L1 still front-load the
desktop-WM surface (L1 adds floating/grid/split/nesting/states) before the toolkit has
proven itself under real apps. Rational layering would prove the toolkit end-to-end first
(real apps, real events, real resize loop), then add the *minimal* WM (focus/activate/destroy
for a second window), and treat docks/workspaces/templates as speculative late layers. The
spec's layering optimizes for "look like a desktop" rather than "prove the core."

**Evidence.** §7 text: "L0: single-win, tiled, decos, focus, base widgets… L1: floating,
grid, split, nesting…" `design-review.md:8-25` (§A, demos prove only single-window widget
economy); `design-review.md:145-156` (§E, multi-window/split/nesting all "built, unproven").

---

## Synthesis — Is the requirements set coherent?

**Yes — after one reframing.** The spec is not two irreconcilable products; it is **ONE toolkit with
TWO rendering backends**, and the fusion condition is the transport split.

**(A) Native widgets — the default path.** Declarative widget commands as small text frames; the
compositor renders with LVGL; the transport stays in the char-grid economy. This is what the demos
prove (`design-review.md:8-25`, §A), what §5.2 requires, and what the terminal's nature directly
supports: zero-deploy, SSH-native, char-grid contract, low bandwidth (a form ≈100B), single surface.

**(B) A pixel surface — the opt-in path.** The app paints a widget-sized drawable region; the
compositor composites it alongside native widgets. This is not Wayland-over-SSH and it is not a
second product: it is the shape every mature toolkit has (Qt widgets + QCanvas; LVGL widgets +
canvas), and it is how terminals have grown graphics for forty years (Sixel, ReGIS, kitty graphics).

The two are reconcilable under three conditions the spec is currently missing:

1. **Transport split** — control stays text; bulk data gets a length-prefixed/binary frame type.
   Without it (B) is unreachable; with it both paths share one stream.
2. **Widget modeling** — (B) is a *widget kind* (a drawable region), so the app never owns window
   geometry and §4.1 holds.
3. **Staging** — (A) first, (B) late and opt-in, so §2's low-bandwidth premise stays true by default.

What the earlier "two products" framing got right: as currently *written*, §5.4 has no transport
distinction and no ownership resolution, so a greenfield implementer cannot tell which economy the
compositor serves. What it got wrong: that the two economies are irreconcilable. They are not — a
renderer that draws widgets is a renderer whether a widget is native or pixel-backed.

**The single most important finding:** §5.4 is not a contradiction with the terminal's nature (the
text→graphics evolution is normal and precedented). It is, as written, a **transport and modeling
gap**: the spec demands pixels but never gives them a transport, and never says the framebuffer is a
widget. Fix those two things and the spec becomes coherent. The genuine over-scope remains
elsewhere — §5.1's desktop-WM weight, §5.3's full resource subsystem, §4.2's hard-no-degrade — and
those stay on the cut list.

---

## Director's thesis — defend / refute

| Area | Director | Here | Position |
|---|---|---|---|
| §2 premise | RATIONAL (caveat) | RATIONAL | **Defend**, strengthen: §2's unscoped "graphics" is the root ambiguity that lets §5.4 hide — larger than "caveat." |
| §5.2 widget toolkit | RATIONAL | RATIONAL | **Defend.** This is the product the terminal's nature supports. |
| §5.5 events (core) | RATIONAL | RATIONAL | **Defend.** |
| §5.5 ≥10pt multitouch | OVER-SCOPE | OVER-SCOPE | **Defend with refined reason:** director says "terminals lack touch" — too absolute (evdev FB backend has touch); the real grounds are no-SSH-transport-for-multitouch + 10pt is tablet-scale. |
| §6.2 compat+degrade | RATIONAL | RATIONAL | **Defend, extend:** §6.2 degrades the terminal but not the apps — a char-rendering fallback for the toolkit is missing. Most important *missed* finding. |
| §6.1 perf | RATIONAL (per-hop) | HIDDEN-FLAW | **Refute the verdict label, not the reasoning.** The reasoning (per-hop not end-to-end) is exactly why this is a HIDDEN-FLAW, not RATIONAL. The director listed it under both RATIONAL and HIDDEN-FLAW; the per-hop framing makes it flaw, not sound — the end-to-end latency the requirement never bounds is the metric that matters. |
| §6.3 security | RATIONAL | RATIONAL | **Defend, extend:** server-side rendering (§5.2) puts untrusted widget code in the compositor — the app:// sandbox boundary is drawn wrong. |
| §6.4 reliability | RATIONAL | RATIONAL | **Defend.** |
| §7 structure | RATIONAL | HIDDEN-FLAW | **Refute the label.** Progressive delivery is rational, but L0 front-loads WM (tiled+decos) before proving the toolkit — that ordering defeats the progressive-delivery goal, so the requirement *as a layering spec* is a HIDDEN-FLAW, not RATIONAL. The director also flags this; I make it the verdict. |
| §4.1 app-manages-layout | HIDDEN-FLAW | HIDDEN-FLAW | **Defend, strengthen:** director says "underspecified"; evidence shows the loop is dead in both directions, not merely unspecified. |
| §4.2 hard-no-silent-degrade | OVER-SCOPE | OVER-SCOPE | **Defend.** Anti-terminal-ethos; contradicts §6.2's own silent-degrade. |
| §5.1 full WM | OVER-SCOPE | OVER-SCOPE | **Defend with refinement:** z-order/focus/activate are *not* over-scope (irreducible on one surface); only decorations/docks/workspaces/split/nesting/persistence are ported desktop thinking. |
| §5.3 resources | OVER-SCOPE | OVER-SCOPE | **Defend.** Its own product; minimal = image + cache. |
| §5.4 framebuffer | FATAL-CONTRADICTION → **RATIONAL AS EVOLUTION** (revised) | **RATIONAL AS EVOLUTION** | **Revised after owner feedback.** The goal is sound — terminal text→graphics is a normal, precedented path (Tektronix/ReGIS/Sixel/kitty). The defects are transport (no binary namespace), modeling (the framebuffer must be a widget), and staging (late, opt-in) — not the goal. See §5.4. |

**Net:** I refute two *verdict labels* (§6.1 and §7 — the director hedged them as both
RATIONAL and flawed; the flaw is the verdict, not a footnote). I refine three reasonings
(§2 ambiguity larger than admitted; §5.1 z-order is rational core; §5.5 multitouch grounds
are transport not touchlessness). I extend two with findings the director missed (§6.2
one-sided degradation strands apps; §6.3 sandbox boundary vs server-side rendering). I refine
the core thesis per the owner's feedback: the two economies are reconcilable (one toolkit, two
render backends), and §5.4's real defect is a transport/modeling gap — the spec never gives
pixels a transport, nor says the framebuffer is a widget — not a contradiction. The central
finding stands as: **give the framebuffer a binary transport and a widget model, and stage it last.**

---

## Top 3 requirements to CUT or SHRINK first

1. **§5.4 Framebuffer stream — REDEFINE, do not cut.** The goal is sound (terminal text→graphics
   is normal and precedented). Fix its three defects: give bulk data a binary/length-prefixed
   transport, model the framebuffer as a **widget** (not a peer economy), and stage it late and
   opt-in. As written today it is unreachable (pixels have no transport) and unmodeled (ownership
   unspecified) — that is a gap to close, not a feature to delete.

2. **§5.1 Window management — SHRINK to create + size-notify + focus/z-order + destroy +
   crash-cleanup.** Cut decorations, 4 layout types, docks, split, nesting, persistence, and
   workspaces. A single surface needs stacking and focus, not a desktop WM. This also trims
   §7's L0/L1 front-load.

3. **§5.3 Resources — SHRINK to `data:`/`file:`/`theme:`/`builtin:` + in-memory cache +
   fallback.** Cut the `remote:` proxy, disk/net cache tiers, and refcount-as-requirement.
   The terminal already has a filesystem and `curl`; a resource subsystem is a separate
   product.

**Honorable mentions** (not top-3 because smaller): §4.2's "NEVER silent degrade" clause
(replace with degrade-then-inform); §5.5's ≥10-point multitouch (drop to single-touch/2-pt
gesture for the embedded path only); §7's L0 (reorder to single-win+widgets+events first,
defer all WM).
