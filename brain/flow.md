---
slug: flow
title: Key flows
role: key flows
updated: "2026-09-14"
---

# Key flows

See: [docs/architecture.md](../docs/architecture.md)

## End-to-end path
- App (tgs_client) → stdout → PTY → Parser → WM → Scene painter → Render → Terminal
- Terminal input → stdin → Event Engine → Backend → WM → PTY → App
