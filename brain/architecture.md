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
- 输出后端抽象（SDL2 桌面 / FB 嵌入式）
- IME 路由（路由键盘输入到 IME App）

### 应用层
- C 客户端库（tgs_client）
- IME 应用（独立进程）
- 示例应用（simple_form）

## Module graph

Protocol Parser → Window Manager → Backend (LVGL)
Event Engine → Backend → Window Manager → PTY (to app)
LVGL Backend → Output Backend (SDL2 / FB)

## Output Backends

TGS 支持两种输出后端，编译时选择：

### SDL 后端（桌面 Linux）
- SDL2 创建窗口，模拟 framebuffer
- LVGL 渲染到像素缓冲区，通过 SDL texture 显示
- SDL 事件处理键盘/鼠标输入
- 编译选项：cmake -DTGS_USE_SDL=ON

### Framebuffer 后端（嵌入式）
- 直接写入 /dev/fb0（mmap）
- evdev 读取 /dev/input/event*
- 无桌面环境依赖
- 编译选项：cmake -DTGS_USE_SDL=OFF（后续实现）

## Constraints
- C99, 最小依赖
- 终端兼容 ANSI/VT100
- 性能：CPU 单控件 <1ms, 复杂界面 <50ms
