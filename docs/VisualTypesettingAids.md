# 排版视觉辅助

排版视觉辅助（Visual Typesetting Aids）是在 Preview 上方绘制的只读参考层，
用于观察页面的纵向节奏和当前元素经 WebEngine 计算后的排版数值。网格、度量与
相关设置只属于 Sigil-Enhanced 界面，不会写入 XHTML、CSS、OPF 或 EPUB。

本文也是以下设计资料的实现审计记录：

- `todo/Sigil-Enhanced_视觉基线网格与排版辅助系统_PRD.md`
- `todo/Sigil-Enhanced_视觉排版反馈工具调研.md`

## 当前交付范围

本版本实现 PRD 的 P0 技术正确性和 P1 Production MVP：

- Preview 上的基线／纵向节奏参考网格；
- `px` 和固定参考字号的 `em` 网格；
- Document Top 与 Body Content Top 原点、正负偏移；
- 主／次网格线、颜色、不透明度和低缩放阈值；
- 基于文档坐标的滚动相位，以及与 Preview 相同的缩放；
- 当前元素的 computed font、line-height、逻辑 margin/padding 和 writing-mode；
- Preview 右键单次检查元素，不必长期打开状态栏度量；
- 数值相对于当前网格步长的精确比率；
- 一次性用当前元素字号校准固定 `em` 参考值；
- 一键暂时隐藏所有辅助的 Clean Preview；
- 用户设置持久化和键盘快捷键注册；
- 简体中文、繁体中文和日文界面。

以下仍是 PRD 已规划、但不属于本次 Production MVP 的后续阶段：

- P2 Element Box、margin/padding 着色、Visual Gap、CSS source quick jump；
- P3 沿 `vertical-rl` block axis 绘制的竖向节奏线、CJK cell grid 和预设。

当前页面为竖排时，状态栏会明确显示“水平参考网格”，避免把现有水平线误称为
竖排 block rhythm。系统仍会读取并显示 `writing-mode`，为 P3 保留数据接口。

## 使用方法

入口位于：

```text
查看
└─ 排版视觉辅助
   ├─ 显示基线网格
   ├─ 显示排版度量
   ├─ ─────────────────────
   ├─ 以当前元素字号作为网格参考
   ├─ 基线网格设置…
   ├─ ─────────────────────
   └─ 纯净预览
```

所有动作都已注册到“首选项 → 键盘快捷键”，默认不抢占现有快捷键。

Preview 右键菜单还提供“检查排版”和“以此元素的字号作为网格参考”。两个动作都
按照右键位置读取元素；顶部菜单动作则按照 Code View 当前光标对应的元素读取。
“检查排版”是一次性的：结果显示在状态栏，但不会打开或改写“显示排版度量”的
持久设置。光标移到另一个 Code View 元素后，临时结果会被清除。

### 建立 8 px 节奏

1. 打开“基线网格设置…”；
2. Unit 选择 `px`，Grid step 填 `8`；
3. 打开“显示基线网格”和“显示排版度量”；
4. 确认后，Preview 显示网格，状态栏显示当前元素数值及其格数。

### 建立固定 0.5 em 节奏

1. 把 Code View 光标放在正文段落，或在 Preview 中对正文右键；
2. 执行“以当前元素字号作为网格参考”；
3. 在设置中选择 Fixed reference em，Grid step 填 `0.5`；
4. 若参考字号为 `18 px`，Resolved step 将固定为 `9 CSS px`。

切换到其它字号的元素不会改变 9 px 网格。只有再次校准或手动修改参考字号才会
改变它。

### Clean Preview

“纯净预览”只改变当前窗口的会话状态。进入时隐藏网格和状态栏度量；退出后恢复
此前启用的功能。它不会改写“显示基线网格”或“显示排版度量”的持久设置。

## 设置说明

| 设置 | 行为 |
|---|---|
| Unit / Grid step | `px` 直接作为 CSS px；`em` 乘固定 Reference font size |
| Reference font size | `em` 的稳定换算基准，不随当前元素自动变化 |
| Grid origin | 从文档顶部或 body 实际内容 box 顶部开始 |
| Grid offset | 在原点基础上增加正负 CSS px 偏移 |
| Major line every | 每 N 条线绘制一条主线，最小值为 1 |
| Minor/Major color | 默认颜色随 Preview 实际浅色／深色外观变化；用户选色后固定为自定义值 |
| Minor/Major opacity | 独立控制主线和次线的透明度 |
| Minimum zoom | 低于阈值时只显示主线，避免密集次线形成摩尔纹 |

参考字号和换算后的网格步长都必须处于 `0.25–1000 CSS px`。从当前元素取样后，
系统会先验证整组候选设置再应用；若首选项文件中的组合损坏或越界，加载时恢复为
完整、有效的默认 px 配置。

“基线网格设置…”只复用与当前元素严格匹配的缓存；没有匹配值时会异步重新测量。
测量完成前“使用当前元素”保持禁用；测量有效后按钮显示实际 px 值，避免使用来自
旧页面或旧光标位置的缓存。

## 状态栏度量

横排页面的典型输出：

```text
网格 8px │ p │ 字号 16px │ 行高 24px / 3.00× │ 块前距 0px / 0.00× │ 块后距 8px / 1.00×
```

原始英文缩写的含义为：

| 缩写 | computed property |
|---|---|
| LH | `line-height` |
| MBS / MBE | `margin-block-start` / `margin-block-end` |
| PBS / PBE | `padding-block-start` / `padding-block-end` |
| WM | `writing-mode` |

只有 padding 非零时才显示 PBS/PBE，以降低状态栏噪音。状态栏可压缩，完整文本
可从悬停提示读取，标签也设置了可访问名称。

`line-height: normal` 保持显示为 `normal`。系统不会使用 `1.2 × font-size`
伪造一个浏览器没有提供的像素结果。

## 坐标与绘制规则

内部明确区分 CSS px、Qt logical px、设备像素和 Preview zoom：

```text
paint step = resolved CSS step × Preview zoom
phase = (origin + offset - document scroll) × Preview zoom
```

QPainter 自己完成 logical px 到 device pixel 的映射，代码不再乘 DPR。线条使用
cosmetic pen，因此 Retina/HiDPI 下不会重复放大线宽。滚动位置来自
`QWebEnginePage::scrollPositionChanged`，缩放来自 Preview 自身信号，不进行
高频 JavaScript 轮询。

小于 Minimum zoom 时只保留主线；如果主线本身也密到不足一个 logical pixel，
则暂时不绘制，避免形成高频纹理。

## Computed metrics 数据流

Code View 的元素层级变化后先 debounce 75 ms，再在 WebEngine
`ApplicationWorld` 异步执行受控脚本：

```text
Code View hierarchy / Preview point
        ↓
PreviewMetricsProbe
        ↓ getComputedStyle + getBoundingClientRect
PreviewLayoutMetrics
        ↓
VisualTypesettingController
        ↓
status bar / one-shot em calibration
```

每个异步请求都有唯一 ID 和页面 generation。状态栏刷新、菜单校准、右键检查和
设置对话框测量分别跟踪；同一用途的新请求只淘汰自己的旧请求，不会误取消其它
用途仍在等待的结果。页面重新加载后，旧页面的全部回调都会被丢弃。

Code View 连续发出相同元素层级时不会重复查询；状态栏结果还必须匹配当前元素键，
所以较慢的旧元素回调不能覆盖较新的选择。探针失败只隐藏相应用途的度量或显示
不可用状态，不阻止 Preview。

## 非侵入保证

实现仅依赖 Preview、QSettings 和 Qt 绘制组件，不引用 BookManipulation、
ResourceObjects 或 modified-state 接口：

- 不注入 `<style>`、class 或额外 DOM；
- 不修改内存中的 XHTML/CSS/OPF；
- 不产生 undo/redo 项；
- 不改变 book modified state；
- 不进入打印用 HTML 或 EPUB 导出；
- 开关与滚动不会触发 Preview reload。

这项工具测量的是 Sigil Preview 当前 WebEngine 布局。阅读器可能覆盖字体、字号、
行高或分页策略，因此结果不承诺在所有阅读器中具有相同物理尺寸。

## 工程结构

| 组件 | 职责 |
|---|---|
| `BaselineGridModel` | 纯几何、固定 em 换算、相位、主次线和阈值 |
| `BaselineGridOverlay` | 输入透明的 QPainter 屏幕层 |
| `BaselineGridSettingsStore` | `preview/visual_aids` 用户设置 |
| `PreviewMetricsProbe` | 异步 DOM/computed-style 查询、请求 ID 和页面 generation |
| `PreviewMetricsRequestTracker` | 按状态、校准、检查、设置四种用途隔离请求生命周期 |
| `PreviewMetricsJavascript` | 可独立集成测试的 WebEngine 查询协议 |
| `PreviewLayoutMetrics` | 结构化解析结果，不包含 UI 拼接文本 |
| `VisualTypesettingController` | 动作、会话状态、网格、探针和状态栏协调 |
| `BaselineGridSettingsDialog` | 完整参数设置、当前元素取样和恢复默认值 |

Preview 与 BaselineGridOverlay 是 `OverlayHelperWidget` 中的兄弟层，尺寸始终相同。
Overlay 继承 `Qt::WA_TransparentForMouseEvents`，不影响选择、链接、右键、焦点或
Developer Tools。

## 测试

核心测试：

```sh
ctest --test-dir cmake-build-debug \
  -R '^(baseline_grid_model|baseline_grid_overlay|preview_metrics_webengine|preview_metrics_request_tracker|visual_typesetting_aids_contract)$' \
  --output-on-failure
```

覆盖内容包括：

- 正负滚动相位、zoom 只乘一次、主线索引和低缩放阈值；
- 固定 em 换算、设置往返和损坏设置恢复；
- `line-height: normal` 协议与数字 line-height；
- 真实 WebEngine 页面中的 computed font/line-height/margin/padding 与 body 原点；
- 不同用途请求可并存、同用途替换、过期结果拒绝和页面切换清理；
- Overlay 尺寸同步、鼠标透明、Clean Preview 恢复；
- 菜单、右键单次检查、快捷键、持久／会话状态边界、异步探针和非侵入依赖合同；
- 简体中文、繁体中文、日文翻译目录完整性。

跨平台发布前仍应按照原 PRD 的 Windows/macOS/Linux、DPI、Preview Zoom 与
横排／竖排 XHTML 矩阵执行人工视觉检查；自动测试证明坐标公式和组件合同，不能
替代所有 GPU/WebEngine 组合的目视确认。
