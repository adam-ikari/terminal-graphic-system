---
id: fullscreen-window-z-order
title: "Full-screen opaque window occluding character base is normal z-order, not a defect"
category: decision
status: active
tags: [z-order, compositor, scene, D3]
created: "2026-09-18T00:56:50"
updated: "2026-09-18T00:56:55"
---

<!-- compiled_truth -->
The character base is the BOTTOM layer (scene underlay); widgets/windows composite ABOVE it (src/compositor/main.c:346 "Character base: always present, widgets composite above it"). A full-screen opaque window root (scene/scene_backend.c: NORMAL window root is full-screen with opaque bg 0x222222) covering the character base is CORRECT, expected window-system z-order: top-most opaque window occludes what is beneath. The compositor MUST NOT force character-on-top and MUST NOT auto-make windows transparent/smaller. A program that wants "characters + widgets on screen together" is responsible for its own window geometry (non-fullscreen, semi-transparent, or placing the character area inside its window) — app manages layout, terminal manages structure.


## Timeline

- time: 2026-09-18T00:56:50
  kind: decision
  summary: "Created this page: Full-screen opaque window occluding character base is normal z-order, not a defect"
  source: "D3 design decision (closed NORMAL, not a bug)"
  affects: [fullscreen-window-z-order]

- time: 2026-09-18T00:56:55
  kind: decision
  summary: "Character base is the bottom layer; windows composite above; full-screen opaque window occluding it is correct z-order, not a defect"
  source: "src/compositor/main.c:346; src/backends/scene/scene_backend.c"
  affects: [fullscreen-window-z-order]
