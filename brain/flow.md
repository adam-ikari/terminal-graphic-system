---
slug: flow
title: Key flows
role: key flows
updated: "2026-09-14T05:53:57"
---

# Key flows

## End-to-end path

App (tgs_client) → stdout → PTY → Compositor parser → Window Manager → LVGL Backend → render → Sixel → Terminal stdout

Terminal input → stdin → Event Engine → Backend inject → LVGL → widget event → Window Manager → PTY → App stdin

IME: Terminal input → Event Engine → (if textarea focus) → IME App PTY → IME App → IME_COMMIT → Compositor → LVGL insert_widget_text
