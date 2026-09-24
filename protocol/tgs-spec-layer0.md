# TGS Protocol Specification v2.0 — Drawing Semantics

TGS 是运行在终端里的图形 API：程序 ⇄ 渲染器点对点，一程序一画布。
协议只回答两个问题——**画什么**（绘制原语）与**输入怎么来**（事件）。
像 X11/OpenGL 一样不提供控件、布局、焦点模型；那些是程序侧 toolkit 的职责。

**TGS ⊃ kitty**：帧承载于 kitty 图形协议的 APC 通道（`ESC _ … ESC \`）。
kitty 原生像素帧（`G<key=value,…>;`）与 TGS 帧（`G1;…`）共享同一通道；
TGS 在能力上包含 kitty（像素呈现）并扩展原语语义、事件、IME。

C99；唯一外部依赖是系统 zlib（PNG 编码，级 1，~7ms/帧 800×600）。

---

## 1. 架构

```
程序 App（自持：布局、控件组合、焦点、文本缓冲、值状态）
   ↓ tgs_client（元素 API）
G1 APC 帧（kitty 通道）
   ↓
合成器（薄路由：帧→元素调用；输入事件→程序或 IME 管道）
   ↓ tgs_backend vtable
渲染器（scene painter：TEXT / BOX / GRAPHIC + 命中）
   ↓ paint 端口
呈现（kitty 图形序列 → 真终端 | SDL 调试 | fbdev 嵌入式）
```

**机制/策略切割（规范级判据）：**

| 归属 | 内容 |
|------|------|
| 渲染器/合成器（机制） | 画布树、绘制 3 原语、命中测试、输入采集、窗口级键盘路由、IME 管道 |
| 程序（策略） | 布局、控件组合（按钮=BOX+TEXT）、焦点、文本缓冲、值状态、滚动偏移 |

规范级规则（normative）：

1. 新 kind 必须声明渲染器必须原生持有的状态/能力，否则 MUST 被拒（是组合，程序侧做）。
2. 事件保持交互形状（KEY/CLICK/HOVER/POINTER），禁止 widget 形状事件
   （"tab changed" 之类会把线协议绑死到某个 toolkit）。
3. 合成器不做订阅门控：事件全量转发，程序自过滤。

---

## 2. 线格式

### 2.1 APC 通道

所有帧承载于 kitty 图形协议的 APC 通道：

```
ESC (0x1B)  _ (0x5F)  <payload>  ESC \  (0x1B 0x5C)
```

**双识别（TGS ⊃ kitty）：**

| 载荷前缀 | 语义 |
|----------|------|
| `G1;` | TGS 帧（本规范；G1 = kitty G 通道内的 TGS 子命名空间） |
| `G<key=value,…>;` | kitty 原生图形帧（像素传输/放置）——超集兼容面 |
| 其他 | 非 TGS 帧 |

**可扩展性**：命令编号空间稳定，退役命令的值永久保留（reserved）；
新命令不得复用旧值。元素 kind 同理。

### 2.2 TGS 载荷

```
G1;<stream>;<frame>;<command>[;<arg>...]
```

- 分号分隔，空参数合法（保留位置，不得折叠）
- `stream`：见 §3
- `frame`：发送侧单调递增计数（无应答语义，仅调试/排序）
- `command`：见 §4
- args 为 UTF-8 文本；MUST NOT 含 `ESC`、`;` 之外的控制字符

### 2.3 字符流

APC 帧之外的字节全部是终端字符输出（普通程序的 stdout）。
合成器把字符渲染进字符基（term underlay），与元素场景合成同一画面。

---

## 3. 流

| 流 | 方向 | 内容 |
|----|------|------|
| 0 | 双向 | 握手（HELLO/READY/REJECT） |
| 1 | 程序→渲染器 | 命令（元素） |
| 1 | 渲染器→程序 | 通知（RESIZE/DESTROY） |
| 2 | 程序→渲染器 | 资源（IMAGE/GRAPHIC 像素源，kitty 格式） |
| 4 | 渲染器→程序 | 输入事件 |

---

## 4. 命令

### 4.1 握手（stream 0）

程序先说话（`HELLO`），渲染器答 `READY` 或 `REJECT`：

```
G1;0;0;1;<version>;<caps>          # HELLO（ime=true 声明 IME 程序身份）
G1;0;0;2;<caps>                    # READY
G1;0;0;3;<rejected_cap>            # REJECT
```

IME 程序 HELLO 帧的 caps 中带 `ime=true`——合成器把它注册为输入前端。

### 4.2 元素（stream 1）

**无窗口概念。** parent=0 即画布顶层；parent=元素 id 挂到该元素下。

| 命令 | 参数 | 语义 |
|------|------|------|
| `WGT_CREATE` (32) | [id, type, parent, x, y, w, h] | 创建元素；parent 必须是 0 或 BOX |
| `WGT_UPDATE` (33) | [id, value] | 替换内容（TEXT 的文本 / GRAPHIC 的形状数据） |
| `WGT_STYLE` (34) | [id, prop, value] | 设置绘制属性（§5.2） |
| `WGT_DESTROY` (35) | [id] | 销毁元素（子树递归） |

`WGT_CREATE` 的 id 由程序分配（程序侧引用句柄）；重复 id 或未知 parent MUST 拒绝。

### 4.3 通知（渲染器 → 程序，stream 1）

| 命令 | 参数 | 语义 |
|------|------|------|
| `NTF_RESIZE` (64) | [w, h] | 画布尺寸变化（程序重排的触发器） |
| `NTF_DESTROY` (66) | [id] | 渲染器侧拆除（异常恢复时） |

**无 NTF_GEOMETRY**：几何是程序自己算的，无需回报。
**无 NTF_FOCUS/STATE**：窗口与焦点模型不存在。

### 4.4 事件（渲染器 → 程序，stream 4）

全量转发，无订阅门控；程序自过滤。

| 命令 | 参数 | 语义 |
|------|------|------|
| `EVT_KEY` (81) | [key, mods] | 键盘边沿（按下；key 空间见 §5.3） |
| `EVT_CLICK` (80) | [id] | 按下+释放落于同一元素（命中=渲染器机制） |
| `EVT_HOVER_ENTER` (84) | [id] | 指针进入元素（鼠标；触摸无 hover） |
| `EVT_HOVER_LEAVE` (85) | [id] | 指针离开元素 |
| `EVT_POINTER` (86) | [x, y, phase] | 原始指针坐标；phase 0=down 1=move 2=up |
| `IME_COMMIT` (97) | [text] | IME 程序提交的最终文本 |

**事件路由（规范）：**
- CLICK/HOVER 带 hit-test 的元素 id；无命中时 POINTER 事件的 id 为空锚（根）。
- KEY 无元素锚——合成器按**窗口级**路由：有 IME 连接时按键进 IME 管道，
  IME 程序 COMMIT 的文本以 `IME_COMMIT` 事件回流给程序；
  无 IME 时 KEY 直接给程序。
- 程序自己实现 Tab 导航、控件级焦点、快捷键——合成器不做。

### 4.5 IME（合成器 ⇄ IME 程序）

IME 是独立程序（独立连接，HELLO caps 带 `ime=true`）：

```
程序按键 → 合成器（窗口级路由）→ IME 程序（EVT_KEY）
IME 组合完成 → IME_COMMIT [text] → 合成器 → 程序（IME_COMMIT 事件）
```

- 候选窗由 IME 程序自己绘制（上层 WM 呈现）——合成器不画 preedit overlay。
- 本协议只承载文本变换通道；无 PREEDIT/CANDIDATES/SELECT/CANCEL 命令。

---

## 5. 枚举

### 5.1 元素 kind（`type`）

| 值 | Kind | 渲染器持有 | 说明 |
|----|------|-----------|------|
| 1 | `TEXT` | 文本渲染 | 内容 = UTF-8 字符串 |
| 8 | `BOX` | 矩形/结构 | 可作父；子元素在程序算好的 rect 上 |
| 16 | `GRAPHIC` | 矢量形状 | circle/path/polygon，形状由 STYLE_SHAPE 选 |

退役值（0=BUTTON、2=INPUT、3=CHECKBOX、5=SLIDER、11=SCROLL、
4,6,7,9,10,12-15,17-19）**永久保留**，渲染器 MUST 拒绝创建（映射表见
旧版 spec 归档）。它们全部是程序侧组合：

- 按钮 = BOX + TEXT 子元素 + CLICK
- 开关/单选 = BOX + 程序状态 + 样式重绘
- 滑块 = BOX + POINTER 坐标自绘
- 列表/表格 = BOX + TEXT 子元素 + 程序布局
- 滚动 = 程序收到滚轮/箭头后自己改子元素坐标（渲染器不做视口）

### 5.2 样式属性（`tgs_style_prop`）

| 值 | 属性 | 语义 |
|----|------|------|
| 0 | `BG_COLOR` | 填充色 0xRRGGBB |
| 1 | `FG_COLOR` | 前景色（TEXT 字色） |
| 2 | `RADIUS` | 圆角半径 |
| 3 | `BORDER_WIDTH` | 边框宽 |
| 4 | `BORDER_COLOR` | 边框色 |
| 5 | `FONT_SIZE` | 字号（像素） |
| 6 | `OPACITY` | 不透明度（参考实现暂不绘制） |
| 7 | `SHAPE` | GRAPHIC 形状：0=circle 1=rect 2=path |

### 5.3 键空间

- 可打印 ASCII 直映自身（0x20-0x7E）
- 8=backspace 9=Tab 13=Enter 27=Esc 127=Delete
- 1000-1007：LEFT/RIGHT/UP/DOWN/HOME/END/PAGEUP/PAGEDOWN
- mods：0x01 SHIFT 0x02 CTRL 0x04 ALT

### 5.4 POINTER phase

0=down 1=move 2=up。仅主键；phase 语义即拖拽/自绘交互的全部输入。

---

## 6. 握手时序

```
程序                                渲染器
  │ ESC _ G1;0;0;1;2.0;<caps> ESC \    │
  │ ────────────────────────────────→  │
  │        ESC _ G1;0;0;2;<caps> ESC \ │
  │ ←────────────────────────────────  │
```

- 程序先说话——渲染器从不向未声明 TGS 的程序写协议字节
  （纯字符程序永远不会在 stdin 撞见协议帧）。
- `REJECT` 携带不支持的 capability token；客户端 MUST 优雅退出。
- IME 程序以 `caps` 含 `ime=true` 的 HELLO 声明身份。

---

## 7. 能力协商

`TGS_CAPS_LAYER0`：

```
drawing.text,drawing.box,drawing.graphic,
event.click,event.key,event.pointer,event.hover,event.resize
```

渲染器检查程序声明的能力；不支持 → REJECT。程序可按 READY 回包里的
caps 收敛自身行为。

---

## 8. kitty 兼容面（超集规范）

1. **G 通道共存（双识别，两端）**：kitty 原生帧（`Ga=…` 等）在 TGS 通道内
   合法。识别规则在 pty 两端一致——`G1;` 前缀是 TGS，其余 `G…` 是 kitty
   原生：合成器 parser MUST 路由原生载荷给原生接收器、MUST NOT 喂给 TGS
   解码器；客户端库读自己 stdin 时 MUST 消费并跳过非 `G1;` 的 APC 帧
   （合成器回写的 ACK 会与 READY/EVT 共流），`G1;` 解码失败仍视为流错误。
2. **呈现即 kitty**：合成器把渲染画布编码为 kitty 图形序列
   （`Gf=100,a=T,q=2,m=…` PNG base64 分块）推到真终端 stdout。
   呈现节拍由 `TGS_FPS` 环境变量指定（默认 60，支持到 240）：
   60Hz 实测 59.8fps 精确命中；120Hz 档在参考硬件（Ryzen 7 5800H）
   达 ~102fps——编码 7ms/帧占预算 86%，脏区传输是逼近 120 的后续路径。
   raw RGBA 禁用——77MB/s 会淹没终端（stb PNG 编码 ~40ms/帧，
   同样淘汰：换系统 zlib 级 1 后 136 帧/s）。
3. **程序可直用 kitty（接收器已落地）**：程序直接发 kitty 原生帧传像素是
   合法用法，像素直接落画布已在 L0 实现：
   - 动作 `a=t/T/p/d`；格式 `f=100|32|24`（PNG 经 stb_image，IHDR 尺寸
     预检 ≤8192/轴、≤4M 像素、载荷 ≤8MB；raw 精确尺寸）；`m=1..0` 分块
     base64 流式拼装（单流）；`i=` 64 图像槽，缺省自动 id（≥0x40000000），
     满则逐出最旧放置；解析器缓冲 8192 字节以容纳整块 4KB 二进制的
     base64（≈5.5KB）。
   - 放置 = 光标格快照 + `X/Y` 格内像素偏移；`c/r` 为显示尺寸（格为单位，
     最近邻缩放，缺一轴按纵横比推导）；`x/y/w/h` 为显示裁剪（仅无数据帧
     ——数据帧上的 w/h 是 raw 尺寸、x/y 视为部分传输而拒绝）；顺序按
     (z, 放置序)；同 id 重传重置放置。
   - z 序（承重）：图像在字符底之上、场景元素之下——glyph 绘制之后、
     场景渲染之前合成进 term-view 像素，底色重绘时无条件重放。
   - ACK：仅 `i=` 存在时回写 app pty；`q=0` 全部 / `q=1` 仅错误 /
     `q=2` 静默；码 OK/ENOENT/EINVAL/ENOTSUPPORTED；非阻塞写，满则丢弃。
   - 明示缺口（记录而非实现，违者按上码表拒绝）：`U=1` 虚拟放置、动画
     （`a=A/a/f/c`）、zlib 压缩、多放置（`p>0`）、游标策略 `C=0`（从不
     移动 vterm 光标）、放置与滚动解耦（快照格固定，文本在其下滚动）、
     BMP/GIF、部分 raw 传输、file/shm 目标（`t=/o=`）。
   - 作为 GRAPHIC/IMAGE 元素填充源（按 id 引用）——仍是 L4 surface 范畴。

---

## 9. Frame ID 规则

- 发送侧单调递增；无应答语义。
- 仅用于调试与乱序检测；渲染器 MUST NOT 依赖它排序。

---

## 10. 错误处理

| 情形 | 行为 |
|------|------|
| 未知命令 | 忽略（向前兼容） |
| 参数数量/格式错 | 忽略该帧，stderr 记日志 |
| 未知元素 id | 忽略该命令 |
| parent 非 BOX | 拒绝创建，stderr 记日志 |
| 退役 kind | 映射到原语替换或拒绝（见 §5.1） |
| 握手能力不支持 | REJECT |

---

## Appendix A: 参考实现

- 渲染器：`src/backends/scene/`（paint_sdl2 / paint_skia 双后端）
- 合成器：`src/compositor/`（window_manager.c 薄路由 + parser.c 双识别）
- 客户端：`src/client/tgs_client.c`（元素 API）
- 呈现：`src/backends/output_kitty.c`（主）+ output_sdl.c（调试）+ output_fb.c（嵌入式）
- 示例：`examples/simple_form.c`（程序自编辑文本缓冲 + BOX+TEXT 按钮组合）
- 帧编码：`src/common/tgs_frame.c`（G1 载荷）
- kitty 编码：`src/backends/kitty_encode.c`（ARGB→PNG→base64→分块 APC）

## Appendix B: 版本历史

- **v1.0**（归档 `tgs-spec-layer0-v1.md.bak`）：窗口模型、20 kind 控件目录、
  焦点/导航/订阅/布局/几何回报。已被本版取代。
- **v2.0**（本版）：绘制语义 3 原语、无窗口/焦点/布局/订阅、kitty G1 APC、
  POINTER 坐标事件、IME 独立程序化。
