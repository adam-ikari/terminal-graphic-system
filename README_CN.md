# TGS — 终端图形系统

基于 ANSI/VT100 转义序列的轻量级终端图形平台。

## 什么是 TGS？

TGS 将终端变成图形平台。应用通过字节流协议构建界面——无需 GPU 依赖，无需桌面环境，支持 SSH 远程。

## 架构

```
┌─────────────────────────────────────────┐
│  TGS Compositor（桌面/嵌入式）            │
│  ├── 协议解析器（APC 帧）                │
│  ├── 窗口管理器                          │
│  ├── LVGL 后端（20+ 控件）               │
│  ├── 输出后端（SDL2 或 FB）              │
│  └── IME 路由 → IME 应用                 │
├─────────────────────────────────────────┤
│  TGS 客户端库（C API）                   │
├─────────────────────────────────────────┤
│  应用程序（你的代码）                     │
└─────────────────────────────────────────┘
```

## 快速开始

```bash
# 构建
git submodule update --init --recursive
mkdir -p build && cd build && cmake -DTGS_USE_SDL=ON .. && make -j$(nproc)

# 运行演示
./build/tgs-compositor ./build/simple_form
```

## 环境要求

- C99 编译器
- Linux（macOS/Windows WSL 后续支持）
- SDL2 开发库（`libsdl2-dev`）

## 构建命令

```bash
make init     # 克隆依赖
make build    # 编译
make test     # 运行测试
make run      # 启动 compositor + 演示应用
```

## 项目结构

```
src/
├── common/          # 协议定义（后端无关）
├── compositor/      # 终端合成器核心
├── backends/lvgl/   # LVGL 渲染后端
└── client/          # C 客户端库
examples/
├── simple_form.c    # 演示：按钮、标签、输入框
└── ime_app.c        # 参考实现：输入法应用
protocol/
└── tgs-spec-layer0.md  # 协议规范
```

## 协议

TGS 使用 APC（Application Program Command）帧通过 stdin/stdout 通信：

```
ESC _ TGS;<stream>;<frame_id>;<command>;<args>... ESC \
```

完整规范见 [protocol/tgs-spec-layer0.md](protocol/tgs-spec-layer0.md)。

## 核心设计原则

- **终端管窗口，应用管布局** — 窗口形态由终端决定，应用只根据窗口大小布局控件
- **协议与后端解耦** — TGS 协议不绑定任何渲染后端
- **IME 是独立应用** — 输入法作为独立 TGS 应用运行，不内置到合成器
- **能力协商驱动** — 连接时双向交换能力，不支持的能力明确拒绝

## 文档

- [系统架构](docs/architecture.md)
- [快速上手](docs/getting-started.md)
- [协议规范](protocol/tgs-spec-layer0.md)
- [输入法框架](docs/ime.md)
- [控件参考](docs/widgets.md)

## 许可证

MIT
