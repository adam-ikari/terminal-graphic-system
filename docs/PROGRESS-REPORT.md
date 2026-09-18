# TGS 终端图形系统 — 进度汇报

> 汇报日期：2026-09-18 · 代码基线：`9407940`
> 项目：终端图形系统（Terminal Graphic System）——一个从字符终端逐层长出的图形窗口系统

---

## 1. 项目定位

TGS 是一套**显示服务器**（display server）：它首先是一个 xterm 级终端（PTY + VT 模拟 + 渲染），在此之上叠加图形能力——widget、窗口、焦点、事件。设计遵循 X11 模型：**服务器持有机制，策略归程序**（WM 是一个客户端，不是服务器的一部分）。

关键原则：

- **字符优先**：纯字符程序（`bash`、`vim`、`htop`）零改动直接运行，图形是叠加层。
- **一条流**：程序的标准输出既是字符流又是 TGS 控制帧流（APC 帧交错），同一程序可同时打印字符和驱动控件。
- **机制/策略分离**：z-order、焦点、布局是策略，属于程序；表面存在、缓冲、resize 通知是机制，属于服务器。

---

## 2. 分层进度

| 层 | 内容 | 状态 |
|---|---|---|
| L0 | 字符终端：PTY、VT 模拟、字符渲染 | **完成** |
| L1 | 字符 + 图形同一程序（文本与 TGS 帧交错） | **完成** |
| L2 | 单窗口 + size-notify + widget 集 + 事件 + resize→relayout + 焦点/键盘导航 + 容器几何回传 | **完成** |
| L3 | styles + 全 widget 库 + 容器 + 多窗口 + resources + IME | **进行中**（structure 切片已完成） |
| L4+ | 客户端像素表面、场景合成、WM 程序、嵌套 | 未开始 |

---

## 3. 功能截图

### 3.1 基础表单（L0/L1 控件）

`simple_form` 场景：label + input + button，即 L0 验收程序的渲染结果。

![基础表单](screenshots/simple_form.png)

### 3.2 容器布局（L3 P3）

`container` 场景：VLAYOUT 纵向堆叠 + HLAYOUT 横向排列 + 嵌套容器。布局后合成器通过 `NTF_GEOMETRY` 把每个子控件的**实际坐标和大小**回传给程序。

![容器布局](screenshots/container.png)

### 3.3 全 widget 库（L3 P2 基础）

`library` 场景：20 种 widget 类型逐一渲染（label/button/input/checkbox/radio/slider/progress/switch/list/table/menu/tab/dropdown/image/timepick/datepick/vlayout/hlayout/glayout/scroll）。

![widget 库](screenshots/library.png)

### 3.4 字符 + 控件混排（L1）

`mixed_demo` 场景：程序打印 40 行字符，开**透明窗口**（`TGS_WINDOW_TRANSPARENT`）——窗口 root 无背景，字符 base 透过显示，控件浮在文本上；同一 stdout 流中字符与 TGS 帧交错。

![字符+控件混排](screenshots/mixed.png)

### 3.5 控件交互状态（hover / 按下 / 点击 / 焦点）

`states` 场景（`examples/states_demo.c`）：14 种交互控件铺满屏幕，程序通过 `EVT_BIND` 订阅 `HOVER_ENTER` / `HOVER_LEAVE` / `CLICK`，对悬停做出**程序侧策略响应**（蓝框高亮）——合成器做命中测试（机制），程序决定悬停视觉（策略）。

**正常态**（无悬停、无按下）：

![正常态](screenshots/states_normal.png)

**悬停 Button**——程序收到 `HOVER_ENTER`，为该控件画蓝色边框（悬停别的控件只高亮它自己；移开即 `HOVER_LEAVE` 清除）：

![悬停态](screenshots/states_hover.png)

**按下 Button**——LVGL 按压态外观（按下瞬间）：

![按下态](screenshots/states_pressed.png)

**点击后**——按钮获得焦点（合成器画焦点轮廓），且程序收到 `CLICK`：

![点击态](screenshots/states_clicked.png)

**悬停 Slider**——高亮跟随指针切换控件：

![Slider 悬停](screenshots/states_slider_hover.png)

> 控件/状态截图由 `tools/render_demo.c`（`states` 模式）生成：驱动真实 LVGL 后端（合成器同款代码路径）到内存帧缓冲输出 PNG，交互状态由真实的 hover 命中测试与 LVGL 按压/焦点状态驱动，非摆拍；字符混排截图来自真实合成器 + Xvfb 实拍。

---

## 4. 已交付能力

### 4.1 字符终端（L0）

- PTY 子进程 + forkpty winsize 设置 → 流解复用（文本 vs TGS APC 帧）→ VT 模拟器 → LVGL canvas。
- 验证：`htop`、`ls` 以零 TGS 代码运行；`test_term.cpp` 覆盖模拟器与解复用。

### 4.2 图形控件与布局（L0–L3）

- **widget 集**：20/20 类型映射到真实 LVGL 对象；事件（CLICK/VALUE/KEY）经 `wm_backend_event` 单一门控转发。
- **焦点/键盘导航**（L2）：合成器持有焦点权（`NTF_FOCUS` + reason）；Tab/Shift+Tab 前后移动、箭头方向移动、`SET_FOCUS` 程序化聚焦、窗口激活/失活焦点对。
- **容器布局**（L3 P3）：VLAYOUT/HLAYOUT/SCROLL 用 LVGL 原生引擎；**运行时 GRID 真布局**——`WGT_LAYOUT` 支持 `[cols, rows]`，3 子项在 2 列网格中正确换行。
- **容器几何回传**（L2 缺陷修复）：`NTF_GEOMETRY`(69) 布局后把每个容器/控件的屏幕绝对坐标回传，客户端 `tgs_client_get_widget_geometry` 查询。

### 4.3 多窗口（L3 P4）

- 每窗口独立 LVGL root + 焦点组；指针点击后台窗口激活。
- **Alt+Tab / Alt+Shift+Tab** 循环窗口，恢复各窗口记住的焦点（`WINDOW_RESTORE`）。
- `NTF_DESTROY`(66)、`NTF_STATE`(67)（激活切换成对通知）落地发射。

### 4.4 事件订阅模型（L2 D6，第一性原理决策）

`EVT_BIND` 从 no-op 变为真实订阅门控：

- **CLICK / VALUE_CHANGED**：opt-in 订阅，未绑定不送达。
- **KEY**：输入传输，始终送达焦点程序（避免"忘绑定打字静默丢"）。
- **焦点**：`NTF_FOCUS` 永不门控。

依据：设计文档 §F.2 记录 EVT_FOCUS 因 *"requires EVT_BIND"* 被退休——证明 bind 即门控是既定意图；KEY 按 §D.1 流图保持无条件。

### 4.5 协议契约收敛

- `WGT_UPDATE` 对齐为 content-only `[widget_id, value]`（spec 的 `property` 维度从未定义）。
- `TGS_LAYOUT_GRID` 修复（原 flex 桩）；`TGS_STYLE_FONT_SIZE` 仍为 no-op（待 resources 字体机制）。

---

## 5. 质量

| 项 | 值 |
|---|---|
| 自动化测试 | **72/72 通过**（gtest，11 套件），ctest 1/1 |
| 覆盖 | 帧编解码、PTY、VT 模拟、导航/焦点、事件门控、容器几何、多窗口激活、LVGL 点击命中、GRID 布局 |
| 证据链 | 每个修复都做红→绿（stash 前失败/恢复后通过） |
| 工作树 | 干净（提交 `28b1918`、`d8faf76`、`9407940` 等） |

## 6. 下一步

- **P5 resources**（L3 最大缺口，greenfield）：`TGS_STREAM_RESOURCE` 资源上传/缓存，gate IMAGE widget 与 FONT_SIZE。
- **P1 styles**：`TGS_STYLE_FONT_SIZE` 生效（需字体机制）+ 样式查询通道。
- **P6 IME**：preedit 覆盖层（§H.2）+ 候选条（`IME_CANDIDATES`/`IME_SELECT` 目前静默丢弃）。

---

*完整设计：`docs/architecture-v2.md`、`docs/navigation.md`、`docs/ime.md`；逐层验收见 `protocol/tgs-spec-layer0.md` 与 `.spec/l3-survey-report.md`。*
