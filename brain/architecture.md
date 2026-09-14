---
slug: architecture
title: System architecture
role: system architecture
updated: "2026-09-14T05:53:58"
---

# System architecture

## Overview

TGS 分三层：协议层、Compositor、应用层。

### 协议层（后端无关）
- APC 帧格式：ESC _ TGS;stream;frame_id;cmd;args... ESC \
- 四流复用：handshake(0), command(1), resource(2), framebuffer(3), event(4)
- 能力协商：HELLO/READY 握手

### Compositor
- PTY 管理 + 多进程（主应用 + IME 应用）
- 协议解析器
- 窗口管理器（ID→handle 映射，事件路由）
- 事件引擎（终端输入→后端注入）
- LVGL 后端（控件/渲染/样式/事件）
- Sixel 输出（libsixel 或内联编码器）
- IME 路由（路由键盘输入到 IME App）

### 应用层
- C 客户端库（tgs_client）
- IME 应用（独立进程）
- 示例应用（simple_form）

## Module graph

Protocol Parser → Window Manager → Backend (LVGL)
Event Engine → Backend → Window Manager → PTY (to app)
LVGL Backend → Sixel Output → Terminal stdout
IME App → TGS Protocol → Compositor → textarea

## Constraints
- C99, 最小依赖
- 终端兼容 ANSI/VT100
- 性能：CPU 单控件 <1ms, 复杂界面 <50ms
