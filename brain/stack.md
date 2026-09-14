---
slug: stack
title: Tech stack
role: tech-stack choices
updated: "2026-09-14T05:53:58"
---

# Tech stack

## Technology choices

| domain | decision | rationale |
|--------|----------|-----------|
| 语言 | C99 | 最小依赖，最高控制力 |
| 构建 | CMake + Makefile | 工程标准 |
| 依赖管理 | git submodule (deps/) | 版本锁定 |
| UI 后端 | LVGL v9 | 开源控件库，40+ 控件，CPU/GPU 双路径 |
| Sixel 输出 | 内联编码器 / libsixel | 终端像素输出 |
| 传输 | 单流多路复用 (APC) | SSH/PTY 兼容 |

## Decision mindmap

Protocol (APC frames)
  ├─ Compositor (PTY, window mgr, event engine)
  │   ├─ LVGL Backend (widgets, rendering)
  │   │   └─ Sixel Output (terminal display)
  │   └─ IME Router → IME App (separate process)
  └─ Client Library (tgs_client)
      └─ Apps (simple_form, ime_app)

## Open items
- GPU 渲染路径（Layer 2+）
- 多工作区（Layer 3）
- 网络资源代理（Layer 2）
