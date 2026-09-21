# TGS 协议重写汇报 — 第一性原理最小形态

> 汇报日期：2026-09-20 · 代码基线：`7b0fc8f`
> 协议版本：v2.0 · 3 原语 + kitty G1 APC · 49/49 测试

---

## 1. 定案：TGS = 终端图形 API

从第一性原理推导：TGS = 程序 ⇄ 渲染器的**点对点绘制协议**。一程序一画布。协议只回答两个问题：**画什么**（绘制原语）和**输入怎么来**（事件）。像图形 API（X11/OpenGL/Wayland）一样——不提供控件、不提供布局、不提供焦点。那三层是 toolkit 的，在程序侧组合。

| 决策 | 内容 | 状态 |
|------|------|------|
| **协议 = 绘制语义** | 线上只有 3 个绘制原语 + 事件；无控件目录、无 win_id、无焦点 | ✅ 定案 |
| **无窗口管理** | 合成器不管理窗口/激活/z-order——那是外层 WM 的事；协议无 win_id | ✅ 定案 |
| **无焦点模型** | 焦点是图形 API 层概念（窗口级键盘路由），不由终端实现；控件级焦点完全程序侧 | ✅ 定案 |
| **IME 独立程序** | IME 是独立程序，候选词由 IME 程序自己绘制（外层 WM 呈现）；TGS 只做输入变换管道 | ✅ 定案 |
| **TGS ⊃ kitty** | 帧走 kitty G APC 通道（G1 子命名空间）；呈现层用 kitty 图形序列；超集=能力包含+扩展 | ✅ 定案 |

**第一性判据：** 协议只承载**机制**（server holds mechanism）——绘制、命中、输入采集、IME 管道。一切**策略**（布局、控件、焦点、文本缓冲、值状态）在程序侧（program holds policy）。

---

## 2. 协议原语目录（3 kind）

| 值 | 原语 | 渲染器持有 |
|----|------|-----------|
| 1 | `TEXT` | 文本渲染 |
| 8 | `BOX` | 矩形/结构（可作父，子控件在程序算好的 rect 上） |
| 16 | `GRAPHIC` | 矢量图形（circle/path，形状由 STYLE 选；像素图走 RESOURCE 流） |

退役值（0, 2-7, 9-15, 17-19）永久保留——BUTTON/INPUT/CHECKBOX/SLIDER/SCROLL/LIST/TABLE/MENU/TAB/DROPDOWN/TIMEPICK/DATEPICK 全部是程序侧组合，不再是协议 kind。

---

## 3. 命令与事件

### 命令（程序 → 渲染器，stream 1）

| 命令 | 参数 |
|------|------|
| `WGT_CREATE` | [id, type, parent, x, y, w, h] — parent=0 是画布顶层 |
| `WGT_UPDATE` | [id, value] |
| `WGT_STYLE` | [id, prop, value] |
| `WGT_DESTROY` | [id] |

### 事件（渲染器 → 程序，stream 4，全量发，程序自过滤）

| 事件 | 参数 | 说明 |
|------|------|------|
| `KEY` | [key, mods] | 键盘 |
| `CLICK` | [id] | 命中=渲染器机制 |
| `HOVER_ENTER/LEAVE` | [id] | 指针进出元素 |
| `POINTER` | [x, y, phase] | 原始坐标（0=down 1=move 2=up）——程序自绘交互的原语 |
| `IME_COMMIT` | [text] | IME 程序提交文本 |

**删除：** WIN_* 全族、win_id、窗口类型、FOCUS/BLUR、VALUE_CHANGED、SET_FOCUS、NTF_FOCUS/STATE/GEOMETRY、EVT_BIND、WGT_LAYOUT/WGT_ATTR。

---

## 4. 帧格式：kitty G APC 通道 + G1 子命名空间

TGS 帧走 kitty 图形协议的 APC 通道：

```
ESC _ G1;<stream>;<frame>;<command>;<args...> ESC \
```

- `G1` = TGS 在 kitty G 通道的**子命名空间**标识
- kitty 原生帧 = `ESC _ G<key=value,...>;<data> ESC \`（像素传输）
- 两者共享同一 `ESC _` APC 通道——解析器双识别
- **可扩展性**：命令编号空间稳定（退役命令保留值），新命令不破坏旧版本

kitty 协议通过"未知 key 静默忽略"提供隐式扩展兼容——TGS 在 G1 子命名空间内定义自己的命令空间，不依赖 kitty 的扩展能力。超集 = 合成器同时理解 G1（TGS 语义）和 G（kitty 像素）两种帧。

---

## 5. 渲染器：单画布 scene painter

| 模块 | 职责 |
|------|------|
| `scene_core.c` | 节点池、绝对几何、命中测试、事件队列、绘制（TEXT/BOX/GRAPHIC） |
| `scene_backend.c` | 元素 create/rect/content/style/destroy + 指针注入 + hit_test |
| `paint_sdl2.c` | 软件光栅 + SDL2_ttf 字形合成（4bpp Blended alpha） |
| `paint_skia.cpp` | SkCanvas + SkFont（第二参考后端，`-DTGS_USE_SKIA`） |
| `term_view.c` | 字符基 TTF 渲染（underlay 通道，垫在场景下） |

无窗口、无焦点视觉、无文本编辑状态机、无 checkbox/slider/scroll 状态。渲染器只画 + 命中 + 报事件。paint 端口是唯一引擎缝——第三个后端只加一个 `paint_*.c`。

---

## 6. 合成器与客户端

### 合成器（window_manager.c）

薄路由：帧→后端元素调用，输入事件→程序（或 IME 管道）。无焦点、无订阅门控、无几何回报、无布局。

IME 路由：IME 连接时，按键按窗口级路由进 IME 管道；IME 程序 COMMIT 文本回来，合成器作为 `IME_COMMIT` 事件发给程序。合成器不画 preedit——候选词由 IME 程序自己绘制。

### 客户端（tgs_client）

元素 API：

```c
tgs_client_create_element(type, id, parent, x, y, w, h, content);
tgs_client_update_element(id, value);
tgs_client_set_element_style(id, prop, value);
tgs_client_destroy_element(id);
tgs_client_poll_event(&ev, timeout_ms);
tgs_client_send_ime_commit(text);
```

程序自持一切状态——文本缓冲、值、焦点、布局。

### 示例（simple_form.c = 参考）

程序用 KEY 事件自编辑文本缓冲；按钮 = BOX + TEXT 子控件组合；CLICK 响应。完整演示了 SVG 模型：程序组合一切，渲染器只画。

---

## 7. 截图与视觉验收

**视觉模型验收通过。** 文字渲染正常、颜色正确、布局整齐、无黑屏/豆腐块。GRAPHIC 元素画圆（默认形状），符合预期。

| 场景 | 截图 | 说明 |
|------|------|------|
| 原语总览 | ![library](screenshots/library.png) | 3 原语（TEXT/BOX/GRAPHIC）逐一渲染 |
| 程序自编辑 | ![simple_form](screenshots/simple_form.png) | 程序用 KEY 事件拼文本缓冲 |
| 程序摆位 | ![container](screenshots/container.png) | BOX 纯盒子，子控件 rect 程序算 |

### singles（每原语 × 三状态）

| 原语 | 正常 | 悬停 | 按下 |
|------|------|------|------|
| TEXT | ![text](screenshots/singles/text_normal.png) | ![hover](screenshots/singles/text_hover.png) | ![pressed](screenshots/singles/text_pressed.png) |
| BOX | ![box](screenshots/singles/box_normal.png) | ![hover](screenshots/singles/box_hover.png) | ![pressed](screenshots/singles/box_pressed.png) |
| GRAPHIC | ![graphic](screenshots/singles/graphic_normal.png) | ![hover](screenshots/singles/graphic_hover.png) | ![pressed](screenshots/singles/graphic_pressed.png) |

---

## 8. 质量

| 维度 | 结果 |
|------|------|
| 测试 | **49/49 通过**（frame/protocol/term/pty/snapshot/l1/scene_click 8 套件） |
| 删除的测试 | test_nav / test_l2_focus / test_geometry / d2_repro / nav_probe（焦点导航模型已退役） |
| 视觉验收 | 3 张截图 + 9 singles 视觉模型评审通过 |
| 历史清理 | 全仓 lvgl 计数 = 0（上一轮完成） |
| 双后端 | SDL2 + Skia 共用同一 paint 端口 |

---

## 9. 提交记录

| 提交 | 内容 |
|------|------|
| `7b0fc8f` | feat(frame): G1 prefix — TGS sub-namespace in kitty G APC channel |
| `616fe5f` | feat(protocol)!: rewrite to first-principles minimal — 3 primitives, kitty G APC, no windows/focus/layout |
| `fab3c67` | docs(report): HTML progress report for protocol rewrite |

---

## 10. 下一步

1. **kitty 呈现通道**（`output_kitty.c`）：fb→RGBA→PNG→base64→kitty 图形序列→stdout。TGS 的呈现基础，非编译选项。
2. **parser 双识别**：G1 载荷 → TGS 帧；裸 G → kitty 原生像素（超集兼容）。
3. **文档/brain 更新**：spec 重写、brain 记录 TGS ⊃ kitty 定案。
4. **程序侧控件库**（libtgs-ui）：图形语义价值闭环的最后缺口——radio 互斥组、list=SCROLL+子控件、diff/重排。组合过程中每个摩擦点都是协议缺口的真实信号。

---

## 11. 呈现刷新率：60Hz / 120Hz（2026-09-21）

**需求**：支持 60Hz，争取 120Hz。实测与实现：

| TGS_FPS | 呈现节拍 | 实测 fps | 像素一致（PIL） |
|---------|----------|----------|-----------------|
| 60（默认） | 16.7ms | **59.8** | 100% / maxdiff=0 |
| 120 | 8.3ms | **101-102** | 100% / maxdiff=0 |
| 240 | 4.2ms | 102（编码上限） | 100% / maxdiff=0 |

**瓶颈与修法**：
- stb 自带 deflate ~25 帧/s（40ms/帧）→ 换系统 zlib 级 1：136 帧/s（7.3ms/帧），
  60Hz 和 120Hz 预算内（PNG 结构手写 IHDR/IDAT/IEND + CRC32，stb 退役出 wire path）
- poll 循环 5ms 固定 → 120Hz 时 poll+编码串行化 ≈12ms/帧 只到 74fps；
  改为 poll 恒 1ms，节拍全权交给 presenter 门控 → 102fps
- `TGS_FPS` 环境变量：默认 60，上限 240

**验证**：合成器 stdout 捕获 → kitty APC 解码（PIL 权威）→ 与 render_snapshot
ground truth 逐像素对比：60/120/240 三档全部 2242/2242 = 100%，maxdiff=0。
（修复了验证脚本的 PNG filter 3/4 混淆 bug——此前 88% 是解码器问题，非协议问题。）

**诚实边界**：120Hz 档在参考硬件（Ryzen 7 5800H）达 102fps（编码占预算 86%）；
脏区传输是逼近真 120 的后续路径。49/49 测试绿。
