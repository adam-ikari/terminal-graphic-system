# TGS 终端图形系统 — 进度汇报

> 汇报日期：2026-09-18 · 代码基线：`b8eca9c`
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
| L1 | 字符 + 图形同一程序（文本与 TGS 帧交错） | **完成**（含透明窗口混排） |
| L2 | 单窗口 + size-notify + widget 集 + 事件 + resize→relayout + 焦点/键盘导航 + 容器几何回传 | **完成** |
| L3 | styles + 全 widget 库 + 容器 + 多窗口 + resources + IME | **进行中**（structure 切片 + hover 完成） |
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

`library` 场景：20 种 widget 类型逐一渲染。

![widget 库](screenshots/library.png)

### 3.4 字符 + 控件混排（L1）

`mixed_demo` 场景：程序打印 40 行字符，开**透明窗口**（`TGS_WINDOW_TRANSPARENT`）——窗口 root 无背景，字符 base 透过显示，控件浮在文本上；同一 stdout 流中字符与 TGS 帧交错。

![字符+控件混排](screenshots/mixed.png)

### 3.5 控件交互状态（hover / 按下 / 点击 / 焦点）

`states` 场景：14 种交互控件铺满屏幕。程序通过 `EVT_BIND` 订阅 `HOVER_ENTER` / `HOVER_LEAVE` / `CLICK`，对悬停做出**程序侧策略响应**（蓝框高亮）——合成器做命中测试（机制），程序决定悬停视觉（策略）。

**正常态**：

![正常态](screenshots/states_normal.png)

**悬停 Button**——程序收到 `HOVER_ENTER`，为该控件画蓝色边框：

![悬停态](screenshots/states_hover.png)

**按下 Button**——LVGL 按压态外观：

![按下态](screenshots/states_pressed.png)

**点击后**——按钮获得焦点（合成器画焦点轮廓），且程序收到 `CLICK`：

![点击态](screenshots/states_clicked.png)

**悬停 Slider**——高亮跟随指针切换控件：

![Slider 悬停](screenshots/states_slider_hover.png)

### 3.6 控件分列展示（每控件独立渲染）

每种控件单独一张（居中、独立窗口），附悬停 / 按下状态。全部 54 张（18 控件 × 3 状态）经**视觉模型逐张验收**。布局已归程序（**SVG 场景语义**）：CONTAINER 是纯盒子，子控件 rect 由程序计算，渲染器不跑布局引擎。

| 控件 | 正常 | 悬停 | 按下 |
|---|---|---|---|
| Button 按钮 | ![b](screenshots/singles/button_normal.png) | ![bh](screenshots/singles/button_hover.png) | ![bp](screenshots/singles/button_pressed.png) |
| Input 输入框 | ![i](screenshots/singles/input_normal.png) | ![ih](screenshots/singles/input_hover.png) | ![ip](screenshots/singles/input_pressed.png) |
| Checkbox 复选框 | ![c](screenshots/singles/checkbox_normal.png) | ![ch](screenshots/singles/checkbox_hover.png) | ![cp](screenshots/singles/checkbox_pressed.png) |
| Radio 单选钮 | ![r](screenshots/singles/radio_normal.png) | ![rh](screenshots/singles/radio_hover.png) | ![rp](screenshots/singles/radio_pressed.png) |
| Slider 滑块 | ![s](screenshots/singles/slider_normal.png) | ![sh](screenshots/singles/slider_hover.png) | ![sp](screenshots/singles/slider_pressed.png) |
| Switch 开关 | ![sw](screenshots/singles/switch_normal.png) | ![swh](screenshots/singles/switch_hover.png) | ![swp](screenshots/singles/switch_pressed.png) |
| Progress 进度条 | ![p](screenshots/singles/progress_normal.png) | ![ph](screenshots/singles/progress_hover.png) | ![pp](screenshots/singles/progress_pressed.png) |
| List 列表 | ![l](screenshots/singles/list_normal.png) | ![lh](screenshots/singles/list_hover.png) | ![lp](screenshots/singles/list_pressed.png) |
| Dropdown 下拉框 | ![d](screenshots/singles/dropdown_normal.png) | ![dh](screenshots/singles/dropdown_hover.png) | ![dp](screenshots/singles/dropdown_pressed.png) |
| Tab 标签页 | ![t](screenshots/singles/tab_normal.png) | ![th](screenshots/singles/tab_hover.png) | ![tp](screenshots/singles/tab_pressed.png) |
| Timepick 时间滚轮 | ![ti](screenshots/singles/timepick_normal.png) | ![tih](screenshots/singles/timepick_hover.png) | ![tip](screenshots/singles/timepick_pressed.png) |
| Datepick 日历 | ![da](screenshots/singles/datepick_normal.png) | ![dah](screenshots/singles/datepick_hover.png) | ![dap](screenshots/singles/datepick_pressed.png) |
| Container 容器（程序摆位） | ![vl](screenshots/singles/container_normal.png) | ![vlh](screenshots/singles/container_hover.png) | ![vlp](screenshots/singles/container_pressed.png) |
| Scroll 滚动容器 | ![sc](screenshots/singles/scroll_normal.png) | ![sch](screenshots/singles/scroll_hover.png) | ![scp](screenshots/singles/scroll_pressed.png) |

> 图片由 `render_demo singles` 模式生成：每控件独立窗口、居中渲染，驱动真实 LVGL 后端到内存帧缓冲输出 PNG。

---

## 4. 视觉模型逐张验收

所有 singles 截图（60 张）+ 交互状态图经**视觉模型逐张评审**，维度：对比度可读性、对齐间距、视觉一致性、产品文档可用性。结论：

### 4.1 验收通过（15/20 控件）

button、input、checkbox、slider（含悬停蓝描边）、switch（修复后 9/10）、progress、list、dropdown、tab（选中高亮+下划线）、datepick（日历完整、当日蓝框）、vlayout/hlayout/glayout（子控件排布正确）、scroll。客观度量：20 张 normal 图控件**居中偏差 (0,0)**，对比度 142-661（全部远超可读阈值）；14 个交互控件悬停蓝框 880-2346 像素/张。

### 4.2 评审抓出并修复的渲染缺陷

| 缺陷 | 视觉评分 | 根因 | 修复 | 复验 |
|---|---|---|---|---|
| switch 滑块溢出轨道 | 3-5/10（"像渲染错误"） | LVGL 把 knob 画在开关全高，小圆角轨道挡不住四角 | 轨道改胶囊形（radius=高度/2） | **9/10** "全在轨道内，垂直居中" |
| radio 方框与 checkbox 不可区分 | 外观重复 | 圆角设在 main part，方块画在 `LV_PART_INDICATOR` | 圆角移到 INDICATOR part | ✅ "圆钮白心蓝环，可区分" |
| button hover/press 无反馈 | 交互缺失 | 内容 label 继承 CLICKABLE 拦截指针 + user_data 编码冲突 | label 去 CLICKABLE + `type+1` 编码 | ✅ hover 蓝框 1067px、按下 diff 8404px |

### 4.3 确认非 bug

- **timepick** 只露 2-3 个选项：roller 可视范围语义（singles 已加高到 150px，可见 3 项）。
- **label 无悬停响应**：非交互控件，正常。
- **image 空白**：resources 子系统未做（L3 P5 已排期）。
- **menu 只有返回箭头**：已知 stub（L3 P2 范围）。

---

## 5. 已交付能力

### 5.1 字符终端（L0）

- PTY 子进程 + forkpty winsize → 流解复用（文本 vs TGS APC 帧）→ VT 模拟器 → LVGL canvas。
- 验证：`htop`、`ls` 以零 TGS 代码运行；`test_term.cpp` 覆盖模拟器与解复用。

### 5.2 图形控件与布局（L0–L3）

- **widget 集**：20/20 类型映射到真实 LVGL 对象；事件经 `wm_backend_event` 单一门控转发。
- **焦点/键盘导航**（L2）：合成器持有焦点权（`NTF_FOCUS` + reason）；Tab/箭头/程序化聚焦/窗口激活焦点对。
- **容器布局**（L3 P3）：VLAYOUT/HLAYOUT/SCROLL 用 LVGL 原生引擎；**运行时 GRID 真布局**——`WGT_LAYOUT` 支持 `[cols, rows]`。
- **容器几何回传**（L2）：`NTF_GEOMETRY`(69) 布局后回传每个容器/控件的屏幕绝对坐标，客户端查询 API。

### 5.3 多窗口（L3 P4）

- 每窗口独立 LVGL root + 焦点组；指针点击后台窗口激活。
- **Alt+Tab / Alt+Shift+Tab** 循环窗口，恢复各窗口记住的焦点（`WINDOW_RESTORE`）。
- `NTF_DESTROY`(66)、`NTF_STATE`(67)（激活切换成对通知）落地发射。

### 5.4 指针交互全模型（新增）

- **鼠标悬停**：`TGS_EVENT_HOVER_ENTER/LEAVE`（7/8）——backend 递归 hit-test 窗口 root，只报状态变化；订阅门控（同 CLICK）；触摸不产生悬停（无 hover 语义）。
- **点击/按压**：CLICK 事件 + LVGL 原生按压/聚焦视觉。
- **事件订阅模型**（L2 D6）：CLICK/VALUE/HOVER 走 opt-in 订阅；KEY 是输入传输始终送达；焦点永不门控。

### 5.5 字符 + 控件混排（L1）

`TGS_WINDOW_TRANSPARENT` 窗口类型：root 背景透明，字符 base 透过显示，控件浮在文本上。

### 5.6 协议契约收敛

- `WGT_UPDATE` 对齐为 content-only `[widget_id, value]`。
- `TGS_LAYOUT_GRID` 修复（原 flex 桩）；`TGS_STYLE_FONT_SIZE` 仍为 no-op（待 resources 字体机制）。

---

## 6. 质量

| 项 | 值 |
|---|---|
| 自动化测试 | **74/74 通过**（gtest，11 套件），ctest 1/1 |
| 覆盖 | 帧编解码、PTY、VT 模拟、导航/焦点、事件门控（含 hover）、容器几何、多窗口激活、LVGL 点击命中、GRID 布局 |
| 证据链 | 每个修复红→绿（stash 前失败/恢复后通过） |
| 视觉验收 | 60 张控件截图 + 5 张交互状态图，视觉模型逐张评审，缺陷闭环 |
| 工作树 | 干净 |

---

## 7. 下一步

1. **P5 resources**（L3 最大缺口，greenfield）：`TGS_STREAM_RESOURCE` 资源上传/缓存，gate IMAGE widget 与 FONT_SIZE。
2. **P6 IME**：候选条（`IME_CANDIDATES`/`IME_SELECT` 静默丢弃）——preedit overlay 已实现一半。
3. **P1 styles**：`TGS_STYLE_FONT_SIZE` 生效（依赖 resources 字体机制）+ 样式查询通道。
4. **触摸接入**：复用鼠标管道（`input_sdl.c` 加 `SDL_FINGER*` 分支，触摸=指针）。
5. **menu 渲染**（P2 stub）：需要菜单项结构 API。

---

*完整设计：`docs/architecture-v2.md`、`docs/navigation.md`、`docs/ime.md`；逐层验收：`protocol/tgs-spec-layer0.md` 与 `.spec/l3-survey-report.md`。*
