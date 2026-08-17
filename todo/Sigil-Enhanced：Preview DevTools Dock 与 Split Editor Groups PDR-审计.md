# PDR 审计：Preview DevTools Dock 与 Split Editor Groups

- 审计对象：`todo/Sigil-Enhanced：Preview DevTools Dock 与 Split Editor Groups PDR.md`（101 节，约 3673 行）
- 仓库：`3tic-project/Sigil-Enhanced`（本地 `sigil-modified`）
- 基线：分支 `docs/audit-preview-devtools-split-editors`，提交 `08cee5fcf`（`master` / `enhanced/master`）
- 产品版本：`2.8.1.E8`（`version.xml`）
- 审计日期：2026-08-16
- 方法：逐节对照当前源码静态核对。未改实现、未跑 GUI、未测 WebEngine DevTools 在嵌入 splitter 下的焦点/生命周期。

---

## 1. 结论

这份 PDR 作为**产品方向和 UX 约束**是可用的，作为**实现说明书则不能按原文落地**。

三个核心技术决定是对的，应当保留：

1. 不同 Resource 的并列编辑必须做在 `FlowTab` 之上，不能把 CSS 塞进某个 XHTML tab。
2. 同一 Resource 在一个 MainWindow / 一本书里只允许一个 `ContentTab`。
3. Developer Tools 应属于 Preview 工作单元，而不是第二个可自由拆走的顶层 Dock。

但第 3–4、11–12、21、30–35、56–65、95 节对当前代码的描述过时或不完整。按原文开工会做重复建设，并漏掉真正决定复杂度的耦合点。

当前代码里这两件事已经存在，PDR 几乎没有写到：

| 已存在能力 | 位置 | 与 PDR 的关系 |
|---|---|---|
| 单 Resource 单 Tab + 统一 `OpenResource` | `TabManager`（`QTabWidget` 子类） | PDR 当作尚待建立的 Registry |
| Preview DevTools | `Dialogs/Inspector` + `PreviewWindow::InspectPreviewPage` | PDR 当作从零新建 `DeveloperToolsPane` |

**综合判断：需求值得做；原文实现映射不能开工。** 2026-08-16 已按本审计改写 PDR，并写入 D1–D5。后续实现以改写后的 PDR 为准，不要按原 §95 的绿场 Patch A–G 开工。

风险等级：**实现映射高风险，产品方向可控**。

---

## 2. 评分卡

| 维度 | 评分 | 判断 |
|---|---:|---|
| 产品问题是否真实 | 9/10 | XHTML+CSS 并行编辑、Preview 旁看 DevTools，是 EPUB 排版高频痛点 |
| 架构原则 | 8/10 | 单实例、组在 Tab 外、DevTools 跟 Preview，三条都对 |
| 对当前代码的事实准确性 | 3/10 | 最大短板。Book View、WINDOW 枚举、MainWindow 文件拆分、Tab 体系均与仓库不符 |
| 可实施性 | 4/10 | UX 故事清楚，但漏了 `TabManager`、action 重连、PluginSession、中央栏 Find/Replace |
| 与上游/Enhanced 维护策略 | 7/10 | 小 patch、不改 Resource/EPUB、不引入 IDE workbench，方向正确 |
| 验收与测试 | 7/10 | DEV/SPL 清单覆盖主路径；缺现有 Inspector/TabManager 的回归锚点 |
| 非目标与反方案 | 8/10 | 拒绝 clone tab、拒绝改 FlowTab、拒绝双 MainWindow、拒绝 remote Chrome，都对 |

---

## 3. 事实核对

| PDR 断言 | 当前代码 | 结果 |
|---|---|---|
| `MainWindow.h` 有 `WINDOW_PREVIEW` 等枚举 | 无此枚举。Dock 用 objectName：`bookbrowser` / `previewwindow` / `tableofcontents` / `validationresultsname` / `clipswindow` | 不通过 |
| Preview 已纳入 Dock 管理体系 | `PreviewWindow : public QDockWidget`，`addDockWidget(Right)`，并与 TOC `tabifyDockWidget` | 通过（机制对，枚举描述错） |
| `FlowTab` 有 `m_CodeView` / `m_BookView` / `m_Splitter` | 只有 `m_wCodeView`；构造里 `m_Layout->addWidget(m_wCodeView)`，无 Book View、无内部 QSplitter | 不通过 |
| `enum ViewState { BookView, CodeView, SplitView }` | 不存在。`MainWindow.cpp:6821` 写明 “Now that Book View is gone” | 不通过 |
| 编辑区是 MainWindow 直接管的一组 Tab | `setCentralWidget` 的是 `QFrame`：`TabManager` + `FindReplace` + `FindReplacePlus` | 部分通过 |
| 需要新建 Resource → Tab 索引 | `TabManager::ResourceTabIndex()` 已按 `Resource::GetIdentifier()` 线性查找；`OpenResource` 先 `SwitchedToExistingTab` | 不通过（已有，且 key 比 `Resource*` 更稳） |
| `OpenResource` 应成为唯一入口 | `MainWindow::OpenResource` 只转发 `m_TabManager->OpenResource`；Book Browser / TOC / Validation / Find 都汇入这里 | 通过 |
| `GetCurrentTab()` 作兼容 facade | 现名是 `GetCurrentContentTab()`，已是 facade | 名称不符，职责已存在 |
| 应新增 `src/Preview/DeveloperToolsPane` | Preview 在 `src/MainUI/PreviewWindow.*`；DevTools 在 `src/Dialogs/Inspector.*` | 不通过（模块路径与类都已有） |
| 首次 Inspect 才 lazy-create WebEngine | `PreviewWindow` 构造里 `m_Inspector(new Inspector(this))`；`Inspector` 立刻 `new QWebEngineView(GetInspectorProfile())` | 不通过（当前是启动即创建） |
| 应用内 `setInspectedPage`，不用 remote debugging | `Inspector::InspectPageofView` 调用 `m_inspectView->page()->setInspectedPage(m_view->page())`；profile 来自 `WebProfileMgr::GetInspectorProfile()` | 通过（已实现） |
| Inspect Element 右键应复用 Qt action | `ViewPreview` 设了 `Qt::CustomContextMenu`，但没有菜单槽；`m_InspectElement` 被注释。右键在 `PreviewWindow::eventFilter` 里只复制 hover URL | 不通过（右键 Inspect 不存在） |
| 改 `MainWindowSlots/Menus/Updates/Resources.cpp` | 这些文件不存在。逻辑集中在 `MainWindow.cpp`（7775 行）和 `MainWindowExt.cpp`（2235 行） | 不通过 |
| Rename 可能替换 `Resource` 对象 | `Resource::RenameTo` 只改路径/短名并 `emit Renamed(this, old_path)`；UUID `m_Identifier` 在构造时 `CreateUUID()`，对象不换 | 通过（PDR 的“若替换再改 key”分支当前不需要） |
| Session 已管理 tab 归属，可加 `editorGroups/*` | `ReadSettings` 存 geometry / `saveState()`（工具栏+dock）。**正常开关书不持久化打开的 Tab**。Tab 列表只在 checkpoint checkout 时临时记下 bookpath | 不通过（PDR 高估了现有 session） |
| Console / Checkpoints / Index / Search 都是 Dock | 实际 Dock：Book Browser、Clips、Preview、TOC、Validation。Find/Replace 在中央栏。Checkpoint / Index / Reports 是对话框 | 不通过 |
| FlowTab 现有 SplitView 与 Editor Groups 会嵌套 | Book View/Code View 分屏已删除，不存在这层嵌套 | 不通过（过时风险） |
| JetBrains New UI 是既定视觉方向 | 仓库文档/源码中搜不到该约定 | 证据不足 |
| 默认第一次启动无 Split、无 DevTools | 默认只有一个 `TabManager`；Inspector 对象已建但窗口隐藏 | 部分通过 |
| Preview 点 CSS 不应改渲染文档 | `UpdatePreview()` 对非 HTML 回退 `m_PreviousHTMLResource`；CSS/SVG tab 的 `TabUpdated` 会先 `SaveTabContent` 再刷新 | 通过（已有语义，不必新模型） |

---

## 4. 当前真实结构（PDR 应改写成这样）

```text
MainWindow : QMainWindow
├── central QFrame
│   ├── TabManager : QTabWidget          ← 唯一编辑 Tab 容器
│   │   └── TabBar
│   │       └── ContentTab*
│   │           ├── FlowTab      (HTML，仅 CodeViewEditor)
│   │           ├── CSSTab       (TextTab)
│   │           ├── SVGTab / OPFTab / NCXTab / XMLTab / ...
│   │           └── ImageTab / FontTab / AVTab / PdfTab
│   ├── FindReplace
│   └── FindReplacePlus
├── BookBrowser          (Left dock)
├── ClipsWindow          (Left dock, 默认隐藏)
├── TableOfContents      (Right dock, 与 Preview tabify)
├── PreviewWindow        (Right dock)
│   ├── ViewPreview : QWebEngineView
│   └── Inspector : QDialog              ← 已有 DevTools，浮动窗口
└── ValidationResultsView (Bottom dock)
```

资源身份：

```text
Resource
  m_Identifier = UUID          ← TabManager 查重用这个
  m_FullFilePath / ShortName   ← RenameTo 只改这些
        ↓ 至多一个
ContentTab::m_Resource
        ↓ 恰好在一个
TabManager (今天只有一组)
```

打开路径（已经是 PDR 想要的“唯一入口”）：

```text
BookBrowser::activated
TOC / Validation / FindReplace / Reports / GoToLinkedStyle
        ↓
MainWindow::OpenResource / OpenFile
        ↓
TabManager::OpenResource
        ├── ResourceTabIndex(identifier) 命中 → focus + scroll
        └── 未命中 → CreateTabForResource + AddNewContentTab
```

Preview 的“最近可渲染 XHTML”已经存在：`m_PreviousHTMLResource` / `m_PreviousHTMLText` / `m_PreviousHTMLLocation`。

---

## 5. 必须先改的问题

### C1. 整份 PDR 没有 `TabManager`

- 位置：§3、§6、§9、§12、§56–62、§95
- 问题：编辑区的真实中枢是 `class TabManager : public QTabWidget`（`src/Tabs/TabManager.h:41`）。它已经拥有 OpenResource、查重、建 tab、关 tab、SaveTabData、well-formed 扫描、主题刷新、TabChanged、以及 `CreateTabForResource` 的类型分发。
- 后果：若按 PDR 再做一套 `EditorArea` + `EditorGroup` + `m_OpenResources`，会与 `TabManager` 双轨，所有 `m_TabManager->…` 调用点（MainWindow、PluginSession、PluginRunner）都要对两次。
- 建议：不要平行引入新框架。把 `TabManager` 从“自己就是那个 QTabWidget”提升为“拥有 1–2 个组，每组一个 QTabWidget”。`ResourceTabIndex` / `GetContentTabs` / `OpenResource` 改为跨组。`MainWindow::OpenResource` 继续只做转发。

### C2. `FlowTab` 内部双视图已经不存在

- 位置：§4、§5、§23、§61、§101
- 问题：PDR 用大量篇幅防止“把 CSS 塞进 FlowTab 的 QSplitter”。这个风险在当前树里不存在。`FlowTab` 只剩 Code View。
- 后果：§23 的“三重 splitter 空间不足”是过时场景；实现者若按 §4 去改 `m_BookView` 会找不到符号。
- 建议：§4/§23 改写成历史说明 + 一句“不要复活 Book View 来做双文件”。原则“一组 Tab = 一个 Resource”仍然成立，论据换成 `GetLoadedResource()` / `SaveTabContent()` 即可。

### C3. DevTools 不是新功能，是停靠现有 `Inspector`

- 位置：§30–43、§64–65、§79、§95 Patch A
- 问题：`PreviewWindow` 已有 Inspect 按钮；`Inspector` 已是非模态 `QDialog`，使用独立 Inspector profile，并已 `setInspectedPage`。关闭语义已是 hide + `StopInspection()`，不是销毁（`WA_DeleteOnClose = false`）。
- 额外事实：
  - 启动即创建 Inspector 和它的 `QWebEngineView`，与 PDR “lazy” 相反。
  - `PreviewWindow::hideEvent` 会 `StopInspection()` 并 `close()`。Preview 与 TOC tabify 时，切到 TOC 就会拆掉 inspected 绑定。
  - `MainWindow::InspectHTML()` 只 `show/raise` Preview 并 `UpdatePreview()`，**不打开 Inspector**，也没有任何 QAction 连接，是死代码。
  - Preview 没有可用的右键 Inspect Element。
- 建议：Patch A 的目标改成：把 `Inspector` 从 `QDialog` 变成可嵌入 widget（或薄包装），放进 `PreviewWindow` 内部 `QSplitter`。保留 `WebProfileMgr` profile 与 `setInspectedPage`。不要新建 `src/Preview/` 和第二套 page 生命周期。同时规定：Preview dock 被 tabify/hide 时，嵌入的 DevTools 只随父控件隐藏，不再主动 `StopInspection()`。

### C4. Active Group 不能只改 `GetCurrentContentTab()`

- 位置：§19–21、§54–55
- 问题：菜单/工具栏不是每次点击都去问当前 tab。`TabManager::TabChanged` → `MainWindow::ChangeSignalsWhenTabChanges` → `BreakTabConnections` / `MakeTabConnections`，把 Undo/Cut/SplitSection/GoToLinkOrStyle 等**直接连到那个 tab 的槽**。`GetCurrentContentTab()` 只覆盖查询型路径。
- 后果：用户焦点点到另一组里“已经是 current”的 tab 时，`QTabWidget::currentChanged` 不发火。若只更新 `m_ActiveGroup` 而不重连 action，Ctrl+Z / 加粗 / 分节会作用到上一组。
- 建议：把 “ActiveGroup 变化” 提升为与 `TabChanged` 同级的事件。Active 变化时必须走现有 `ChangeSignalsWhenTabChanges`。`UpdateMWState` / `UpdateUIOnTabChanges` / `UpdatePreview` 也要跟 ActiveGroup，而不是跟“某个 QTabWidget 的 currentIndex”。

### C5. 不能改现有 `OpenResource` 槽签名

- 位置：§12–13
- 问题：现签名是  
  `OpenResource(Resource*, int line, int pos, QString caret, QUrl fragment, bool precede_current_tab)`  
  并作为槽接到 `BookBrowser::ResourceActivated`、`TOC::OpenResourceRequest`、`ValidationResultsView::OpenResourceRequest`。
- 后果：改成 `OpenResource(Resource*, OpenDisposition)` 会弄断这些连接。`precede_current_tab` 已被 Split At Cursor（`CreateSectionBreakOldTab`）使用：新半段在当前 tab **前面**打开且不抢焦。
- 建议：保留原槽。新增重载或默认参数 `OpenDisposition`，默认 `ActiveGroup`。`OtherGroup` 作为新路径，不得破坏 `precede_current_tab`。

---

## 6. 重要但不阻塞原则的缺口

### M1. Session 模型写过了

正常退出只存窗口 geometry 和 `QMainWindow::saveState()`（dock/toolbar）。打开 EPUB 时 `BookBrowser::SetBook` 只激活**第一个 HTML**。PDR 的 `editorGroups/tabAssignments` 等于新增“记住打开了哪些文件”，范围大于本功能。

V1 应只持久化：

- `preview/devToolsVisible` + DevTools splitter
- `editorGroups/enabled` + EditorArea splitter
- `editorGroups/activeGroup`

不要在 V1 持久化两个 group 里分别有哪些 Resource。打开书的行为保持“只开第一个 HTML”；若上次是 split，可以恢复空的第二组。

### M2. 中央栏已经是垂直栈

```text
TabManager
FindReplace
FindReplacePlus
```

再在 `TabManager` 里做上下两组，最坏是四段垂直空间。PDR 讨论了 FlowTab 内部分屏（已不存在），没讨论 Find/Replace Plus。需要写明：Find 面板仍挂在两组**下面**；窄窗口时 splitter 最小高度；不要把 Find 搬进某一个 EditorGroup。

### M3. `PluginSession` 假定单个 `TabManager`

`PluginSession.cpp` 用 `GetContentTabs()`、`GetCurrentContentTab()`、`TabChanged`、`CloseTabForResource`。Live Python / MCP 的 `editor.activeChanged` 也挂在这条上。`GetContentTabs()` 必须返回两组并集；`TabChanged` 只在**真正的活动 tab**变化时发，避免插件把“另一组切了一下未聚焦 tab”当成编辑器切换。

### M4. “至少保留一个 Tab” 与空组冲突

`TabManager::CloseTab` 在 `count() <= 1` 且非 force 时拒绝关闭。V1 若允许空的第二组，这个不变量要按组重写：主组是否仍禁止关到零，需要写死。建议：主组保持“至少一页”（与今天一致）；第二组允许空；`CloseAllTabs(true)` 仍用于换书。

### M5. Close Others 语义

今天 `Close Other Tabs` 关的是**整个窗口**里除当前以外的全部 tab（`TabBar` 右键唯一项）。PDR 改成“只关本组其他 tab”是行为变化，不是兼容封装。V1 要么明确宣布兼容性变化，要么新增 “Close Others in This Group”，保留旧动作。

### M6. 命名会和 “Split At Cursor” 撞车

`actionSplitSection` 文案是 “Split At Cursor”，快捷键名 `MainWindow.SplitSection`。再加 “Split Editor Down” 必须在菜单层级和快捷键 id 上分开，避免用户以为会切开 XHTML。

### M7. “打开关联 CSS” 已有一半

`GoToLinkedStyleDefinition`（Code View → 样式定义）已经会 `OpenResource(css, pos)`。PDR §93 的 P1 应写成：在已有跳转上加 `OpenDisposition::OtherGroup`，并在未 split 时先 `SplitDown`。不要再做第二套 CSS 解析。

### M8. Registry 的 key

PDR 倾向 `QHash<Resource*, EditorLocation>`。现实现用 UUID 字符串。Rename 不换对象，两种都能用；但换书/删除后必须摘掉条目。建议继续用 `GetIdentifier()`，与 `ResourceTabIndex`、插件 `resource_id` 一致。不必先上 `Resource*`，更不必引入新的 `ResourceId` 类型。

### M9. Preview page 重建频率

`ViewPreview` 构造时固定 `m_ViewWebPage`，`CustomSetDocument` 是在同一 page 上加载。清缓存会 `setUrl("")` 再加载，page 指针通常不变。因此“每次刷新 Preview 都 rebind DevTools”不是主路径；主路径是：换书、Preview 析构、以及今天 hideEvent 里的 `StopInspection()`。PDR §41 方向对，触发条件写重了。

### M10. 视觉原则缺少仓库依据

“JetBrains New UI / 低视觉噪声”在本仓库没有对应文档。实现时跟现有 dock/tab 样式即可，不要单独做一套 group 主题色。

### M11. 国际化

新文案必须是英文 source + `zh_CN`/`zh_TW` 翻译，不能把 PDR 示例里的中文直接写进 `tr()`。Enhanced 已有三语 coverage 测试。

### M12. 现有 Tab 拖动

`TabManager` 已 `setMovable(true)`，组内拖动已有。PDR 把 cross-group drag 放 P1 合理；但要写明组内拖动不得回退。

---

## 7. 仍然成立、应保留的决定

这些不因代码事实错误而失效：

1. **不要 clone 同一 Resource 的第二个 ContentTab。** 现有 `SwitchedToExistingTab` 已执行该不变量；split 后必须继续。
2. **Split Down 不要复制当前文件。** 只创建空的第二组。
3. **Open in Other Group 与 Move to Other Group 分开。** 已打开则 focus，不自动搬家。
4. **Move 是 `removeTab` + `insertTab`，不重新加载。** Qt 支持；dirty / undo / cursor 会跟着 QWidget 走。
5. **不要自动 Join。** 关光第二组最后一个 tab 后保留空组。
6. **DevTools 改动不写回 EPUB CSS。**
7. **不要 `QTWEBENGINE_REMOTE_DEBUGGING`。**
8. **不要两个 MainWindow 编同一本书。**
9. **不要递归 IDE grid。** V1 两组、垂直即可。
10. **Find/Replace、书签、Validation 只走 `OpenResource`。** 已基本如此。
11. **默认外观与现在一致。** Split / DevTools 面板都是显式打开。
12. **拆成可独立回滚的小 patch。** 方向对，但具体切片要按第 9 节重排。

验收清单 DEV-01–10、SPL-01–20 作为产品验收仍然可用，但要补：

- DEV-00：现有浮动 Inspector 的按钮、zoom、profile、关闭后再次 Inspect 仍是同一实例。
- DEV-11：Preview 与 TOC tabify 时，嵌入 DevTools 不再被 hideEvent 拆绑定（相对今天是行为变化，必须测）。
- SPL-00：`TabManager::OpenResource` 在未 split 时行为与现在逐位一致，包括 `precede_current_tab`。
- SPL-21：焦点切组后，Undo/Cut/格式化/Split At Cursor 作用在新活动 tab。
- SPL-22：`PluginSession::GetContentTabs` 看见两组；`editor.activeChanged` 不误报。
- SPL-23：换书 `SetNewBook` → `CloseAllTabs(true)` 清掉两组和 registry。

---

## 8. 建议的实现映射（替换 PDR §9 / §56 / §64）

### 8.1 DevTools

不要新模块。改 `PreviewWindow` 布局：

```text
PreviewWindow (QDockWidget)
└── PreviewPane / 现有 m_MainWidget
    └── QSplitter(Vertical)
        ├── 现有 wrapper + ViewPreview + 工具按钮
        └── Inspector 改为可嵌入 QWidget
```

- 继续用 `WebProfileMgr::GetInspectorProfile()`。
- 继续 `setInspectedPage(m_Preview->page())`。
- 显示/隐藏 splitter 第二项，而不是 `QDialog::show()`。
- 可选保留 “Open as Floating Window”（P1），内部回到今天的 `QDialog` 行为。
- `InspectHTML` 要么删掉，要么改成 `View → Developer Tools` 的真正入口（打开 Preview + 显示 Inspector）。

### 8.2 Editor Groups

不要在 `TabManager` 旁边再做 `EditorArea`。

推荐：

```text
TabManager : public QWidget          // 不再自己就是 QTabWidget
├── QSplitter(Vertical)
│   ├── TabGroup 0   (抽出今天的 QTabWidget + TabBar + 大部分方法)
│   └── TabGroup 1   (按需创建)
└── 跨组索引：identifier → (group, ContentTab*)
```

若希望减少一次大改名：让 `TabManager` 暂时仍派生 `QTabWidget` 当主组，第二组是成员；但这会让 `GetContentTabs` / `ResourceTabIndex` 更别扭。一次拆干净更便宜。

`MainWindow` 继续只拿 `m_TabManager`。不要让业务代码遍历 group。

### 8.3 活动组

```text
focus / 点击 TabBar / 点击编辑器
    → TabManager::SetActiveGroup
    → 若活动 ContentTab 变了
        → 发 TabChanged(old, new)
        → MainWindow::ChangeSignalsWhenTabChanges
        → UpdateMWState / UpdatePreview
```

未变化的组里切换 current tab，不应重连 MainWindow actions，也不应改 Preview 的 HTML 上下文（除非新活动 tab 自己是 HTML）。

---

## 9. 重排后的 Patch 顺序（替换 §95）

原文 Patch B“先做 Registry、行为不变”在今天是空操作：Registry 已在 `TabManager`。

| 顺序 | 内容 | 用户可见变化 | 依赖 |
|---|---|---|---|
| A | 把 `Inspector` 嵌入 Preview splitter；修正 hideEvent；View 菜单真正打开 DevTools | 有（DevTools 停靠） | 无 |
| B | 抽出 `TabGroup`，`TabManager` 改为拥有一组；所有调用点行为不变 | 无 | 无（可与 A 并行） |
| C | 第二组 + Split Down / Join；空组；最小高度 | 有 | B |
| D | Open in Other Group / Move；Book Browser 与 Tab 右键 | 有 | C |
| E | ActiveGroup 驱动 `TabChanged` / action 重连 / Preview / Find | 有（正确性） | C |
| F | 只存 splitter + enabled + DevTools 可见性 | 有（重启恢复布局） | A、C |
| G | 单测（registry/跨组 Open/Move/Join）、英文 source、zh 翻译、用户文档 | 无功能 | D–F |

A 与 B 仍然符合“可单独回滚、便于对上游讲清楚”的维护策略。

不建议 V1 做：tab 列表持久化、左右分屏、Detach DevTools、DevTools → CSS 源跳转、自动打开关联 stylesheet（可作为 D 的增量，但不是关门条件）。

---

## 10. 测试建议补丁

PDR §85 的纯逻辑用例成立，应直接打在抽出后的 `TabManager` / 索引上，而不是虚构的 `m_OpenResources`。

仓库已有 `tests/tab_bar_middle_click_test.cpp`，证明 TabBar 可在无整窗 Sigil 的情况下测。优先加：

1. 同一 identifier 第二次 `OpenResource` 不增加 tab 数。
2. Split 后 `Open(A)` 聚焦已有组，不在活动组新建。
3. Move 后指针相同，索引只改 group。
4. Join 后 tab 顺序 = 主组原序 + 第二组原序。
5. `GetContentTabs()` 返回并集；`CloseTabForResource` 两组都能关。
6. `precede_current_tab` 在主组仍把新 tab 插到当前左侧。

DevTools 以手工清单为主（WebEngine 嵌入测不稳定）。至少保留：Inspect 按钮、停靠、浮动 Preview 带着 DevTools、换书不断开残留 page。

---

## 11. 对 PDR 各块的处置

| 章节 | 处置 |
|---|---|
| §1–2 目标与背景 | 保留 |
| §3 MainWindow / WINDOW_* | 重写为真实 Dock 列表 + objectName |
| §4–5 FlowTab 结构 | 重写：仅 CodeView；原则保留 |
| §6–10 EditorArea 模型 | 改写成 TabManager 演化，而不是新顶级类型 |
| §11–13 Registry / OpenResource | 改成扩展 `ResourceTabIndex`；保留旧槽 |
| §14–18 UX（Split/Open/Move/重复打开） | 保留 |
| §19–21 CurrentTab facade | 保留目标，补 `ChangeSignalsWhenTabChanges` |
| §22 Preview 跟最近 HTML | 保留，并指向已有 `m_PreviousHTMLResource` |
| §23 嵌套 SplitView | 降为历史注释 |
| §24–26 Session | 缩到布局状态；去掉 tabAssignments 或标为 P2 |
| §27–29 Join / 空组 / 视觉 | 保留 |
| §30–43 DevTools 绿场设计 | 改写成改造 `Inspector` |
| §44–45 菜单快捷键 | 保留；避开 Split At Cursor；先不抢默认快捷键 |
| §46–48 工作流与不写回 CSS | 保留 |
| §49–55 Find / Browser / Rename / Delete / Save / Undo | 保留原则；补 PluginSession 与 action 重连 |
| §56–65 文件级改造清单 | 作废，换第 8 节 |
| §66–78 偏好、无障碍、性能、崩溃恢复 | 大体保留 |
| §79–86 验收与测试 | 保留并加上第 7 节补项 |
| §87–91 反方案 | 保留 |
| §92–94 P0/P1 | 保留；§93 接到现有 GoToLinkedStyle |
| §95–97 Patch / 上游 | 用第 9 节替换 |
| §98 UI 原则 | 弱化为“跟现有 Qt dock/tab，不加装饰” |
| §99–101 终局结构 | 按第 4、8 节重画 |

---

## 12. 产品裁决（2026-08-16 已拍板）

已写入改写后的 PDR §2。实现按此执行。

| 原问题 | 裁决 | PDR 落点 |
|---|---|---|
| DevTools 嵌入现有 Inspector，还是新建？ | **嵌入现有 `Inspector`** | D1，§6 |
| 重启恢复布局，还是连打开的文件一起恢复？ | **只恢复布局** | D2，§9 |
| Close Others 全窗口还是只关本组？ | **维持全窗口语义** | D3，§7.7 |
| 主组能否关到零个 tab？ | **不能** | D4，§7.7 / SPL-24 |
| `InspectHTML` 删除还是变成入口？ | 由 D1 导出：**改成 `View → Developer Tools`** | §6.5 / DEV-12 |
| Other Group 开到哪？ | **默认开到非活动组**；Preferences 可改为始终下组 / 始终上组 | D5，§7.5 / §10 |

---

## 13. 总评

PDR 把产品问题说清楚了，也选对了不该做的事。它失败在“把一份偏旧的 Sigil 心智模型当成了 2.8.1.E8 的代码结构”：Book View 已删除，Tab 中枢叫 `TabManager`，DevTools 已经以浮动 `Inspector` 的形式活着。

改写重点不是再论证要不要做，而是：

> 停靠已有 Inspector；把已有 TabManager 扩成最多两个组；用已有 identifier 查重；用已有 TabChanged 管 action。

做到这四句，PDR 的 UX 目标和维护策略都可以保住，并且比原文更小、更不容易和上游打架。
