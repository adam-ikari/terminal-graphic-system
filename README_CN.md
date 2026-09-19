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
│  ├── 场景后端（绘制原语）                │
│  ├── 输出后端（SDL2 或 FB）              │
│  └── IME 路由 → IME 应用                 │
├─────────────────────────────────────────┤
│  TGS 客户端库（C API）                   │
├─────────────────────────────────────────┤
│  应用程序（你的代码）                     │
└─────────────────────────────────────────┘
```

## Layer 1 特性

- **绘制语义渲染器** — 8 个原语 kind（按钮、标签、输入、复选、滑块、容器、滚动、图像）；paint 端口双参考后端（SDL2 已就绪，Skia 进行中）；渲染器无布局引擎。
- **显式容器** — `VLAYOUT`/`HLAYOUT`/`GLAYOUT`/`SCROLL` 在创建时按类型应用布局；只有容器可作为父控件。
- **键盘导航** — 合成器主导的焦点模型，每窗口焦点环/作用域，`Tab`/`Shift-Tab`/方向键遍历，`SET_FOCUS`/`NTF_FOCUS`（带 reason），`WGT_ATTR`（`FOCUSABLE`/`FOCUS_INDEX`/`NAV_ARROWS`/scope），按键路由优先级。详见 [docs/navigation.md](docs/navigation.md)。
- **稳健输入管线** — 边沿排队的指针/键盘事件（快点击能注册、不重复按键），规范键码空间 + 修饰键。
- **FB 呈现契约** — 场景后端发布帧缓冲，合成器拷贝到 mmap 的 `/dev/fb0`（16/24/32bpp 已验证）。

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
make run-xvfb # 无显示器：在 Xvfb 下运行并截图
```

## 项目结构

```
src/
├── common/          # 协议定义（后端无关）
├── compositor/      # 终端合成器核心
├── backends/scene/  # 场景核心 + SDL2 paint 端口 + 终端视图
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
- [键盘导航](docs/navigation.md)
- [控件参考](docs/widgets.md)

## 许可证

MIT
