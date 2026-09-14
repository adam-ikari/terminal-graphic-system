---
slug: stack
title: Tech stack
role: tech-stack choices
updated: "2026-09-14"
---

# Tech stack

See: [docs/architecture.md](../docs/architecture.md)

## Key technology choices
- C99, CMake + Makefile, git submodules
- LVGL v9.6 for widgets/rendering — all 20 `tgs_widget_type` map to real LVGL widgets
- APC frames for protocol; navigation design in `src/compositor/nav.{c,h}`
- SDL2 (desktop) / `/dev/fb0` (embedded) backends; FB present = copy to mmap
- Tests: GoogleTest, `tests/test_nav.cpp` (26 cases); ctest wrapper
