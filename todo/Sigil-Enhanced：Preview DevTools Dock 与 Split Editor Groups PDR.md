# Sigil-Enhanced：Preview DevTools Dock 与 Split Editor Groups PDR

## 1. 文档信息

- 项目：Sigil-Enhanced
- 仓库：`3tic-project/Sigil-Enhanced`
- 基准：`master` / `08cee5fcf`（版本 `2.8.1.E8`）
- 文档类型：Product / Design Requirements（按 2026-08-16 审计改写）
- 配套审计：`todo/Sigil-Enhanced：Preview DevTools Dock 与 Split Editor Groups PDR-审计.md`
- 目标功能：
  1. 把现有 Preview Developer Tools（`Dialogs/Inspector`）固定停靠在 Preview 区域下方。
  2. 编辑区支持上下两个 Editor Group，可同时编辑两个不同 Resource（典型：上 XHTML、下 CSS）。
  3. 同一个 EPUB Resource 在整个窗口中只存在一个 `ContentTab`。
- 设计原则：
  - 不替换现有 Qt 编辑框架。
  - 不改变 EPUB Resource 数据模型、QTextDocument、plugin API、EPUB 序列化。
  - 演化现有 `TabManager` / `Inspector` / `PreviewWindow`，不平行新建第二套框架。
  - 默认外观与行为与现在一致；新功能可关闭、可回退。
  - 修改拆成小型、独立 patch，降低与上游 Sigil 的同步成本。

---

## 2. 已锁定的产品裁决

2026-08-16 拍板，后续实现按此执行，不再当作开放问题。

| ID | 裁决 |
|---|---|
| D1 | **嵌入现有 Inspector**。不新建 `DeveloperToolsPane` / `src/Preview/`。把 `Dialogs/Inspector` 从浮动 `QDialog` 改成可嵌入 widget，放进 `PreviewWindow` 内部垂直 `QSplitter`。继续使用 `WebProfileMgr::GetInspectorProfile()` 与 `QWebEnginePage::setInspectedPage`。 |
| D2 | **重启只恢复布局**。持久化：DevTools 是否可见、Preview/DevTools splitter、是否处于 split、EditorArea splitter、活动组。**不**持久化打开了哪些 Resource、tab 顺序、光标。打开书仍只激活第一个 HTML。 |
| D3 | **Close Other Tabs 维持全窗口语义**。关闭两个 group 里除当前活动 tab 以外的全部 tab。不改为“只关本组”。V1 不另做 “Close Others in This Group”。 |
| D4 | **主组不能关光**。Primary group（组 0）在非 force 路径下至少保留一个 tab，与今天 `TabManager::CloseTab` 的 `count() <= 1` 一致。Secondary group 允许空。换书走现有 `CloseAllTabs(true)`，两组都清掉。 |
| D5 | **Open in Other Group 默认开到非活动组**；并提供一项设置，可改为始终开到下组或上组。见 §10。 |

由 D1 导出的实现决定（不再单独拍板）：

- `MainWindow::InspectHTML()` 改为 `View → Developer Tools` 的真正入口：显示 Preview，并显示已嵌入的 Inspector。不再是只 raise Preview 的死代码。

---

## 3. 背景与目标

当前主编辑模式对单文件顺序编辑足够，但 EPUB 排版有两个高频摩擦：

1. Preview 与 WebEngine DevTools 关系松。DevTools 已经存在，但是独立浮动窗口，和 Preview 的 dock / tabify / float 不同步。
2. XHTML 与 CSS 必须在同一个 Tab 栏里来回切。需要的是两个不同 Resource 同时可见，而不是同一个 Resource 的双视图。

本 PDR 吸收两个能力：

> Docked Developer Tools + Two Editor Groups

不把 Sigil 改造成 VS Code，不引入递归 editor grid。

---

## 4. 当前代码（实现必须以这里为准）

```text
MainWindow : QMainWindow
├── central QFrame
│   ├── TabManager : QTabWidget          // 唯一编辑 Tab 容器
│   │   └── TabBar
│   │       └── ContentTab*
│   │           ├── FlowTab      (HTML，仅 CodeViewEditor / m_wCodeView)
│   │           ├── CSSTab : TextTab
│   │           ├── SVGTab / OPFTab / NCXTab / XMLTab / ...
│   │           └── ImageTab / FontTab / AVTab / PdfTab
│   ├── FindReplace
│   └── FindReplacePlus
├── BookBrowser          (Left dock, objectName=bookbrowser)
├── ClipsWindow          (Left dock, 默认隐藏)
├── TableOfContents      (Right dock, 与 Preview tabify)
├── PreviewWindow        (Right dock, objectName=previewwindow)
│   ├── ViewPreview : QWebEngineView     // 固定 m_ViewWebPage
│   └── Inspector : QDialog              // 已有 DevTools，浮动
└── ValidationResultsView (Bottom dock)
```

已存在、必须复用的能力：

| 能力 | 位置 | 含义 |
|---|---|---|
| 单 Resource 单 Tab | `TabManager::ResourceTabIndex` 用 `Resource::GetIdentifier()`（构造时 UUID） | Rename 不换对象、不换 UUID |
| 统一打开入口 | `MainWindow::OpenResource` → `TabManager::OpenResource` → 先 `SwitchedToExistingTab` | Book Browser / TOC / Validation / Find / Reports / `GoToLinkedStyleDefinition` 都汇入这里 |
| 当前 tab facade | `MainWindow::GetCurrentContentTab()` | 没有 `GetCurrentTab()` |
| Action 接线 | `TabChanged` → `ChangeSignalsWhenTabChanges` → `BreakTabConnections` / `MakeTabConnections` | 菜单槽直接连到某个 tab，不是每次点击再查询 |
| Preview 跟最近 HTML | `m_PreviousHTMLResource`；非 HTML 前台时回退；CSS/SVG `TabUpdated` 先 `SaveTabContent` 再刷新 | Split 后不得改这套语义 |
| DevTools | `Inspector::InspectPageofView` → `setInspectedPage`；独立 Inspector profile | 启动时已创建，非 lazy |
| 组内拖动 | `TabManager` 已 `setMovable(true)` | 不得回退 |
| 跳到关联 CSS | `GoToLinkedStyleDefinition` 已 `OpenResource(css, pos)` | P1 只加 OtherGroup，不重做解析 |
| 插件看见 tab | `PluginSession` 用 `GetContentTabs` / `GetCurrentContentTab` / `TabChanged` / `CloseTabForResource` | 两组必须并集；只有真正活动 tab 变化才发 `TabChanged` |

不存在、不要再按旧假设写代码：

- `WINDOW_PREVIEW` 等枚举
- `MainWindowSlots.cpp` / `MainWindowMenus.cpp` / `MainWindowUpdates.cpp` / `MainWindowResources.cpp`
- `FlowTab` 的 `m_BookView`、内部 `QSplitter`、`enum ViewState`
- 正常退出时持久化打开的 Tab 列表
- Preview 右键 Inspect Element（`ViewPreview` 设了 `CustomContextMenu`，但没有菜单；右键只复制 hover URL）
- Console / Checkpoints / Index 作为 Dock（它们是对话框）

`FlowTab == 一个 Resource` 仍然成立，依据是 `GetLoadedResource()` / `SaveTabContent()`，不是内部 Book/Code 分屏。Book View 已删除（`MainWindow.cpp`：“Now that Book View is gone”），不要复活它来做双文件。

---

## 5. Key Decisions

1. **演化 `TabManager`，不平行新建 EditorArea 框架。** `TabManager` 今天自己就是那个 `QTabWidget`。V1 把它提升为“拥有 1–2 个组”。`MainWindow` 继续只拿 `m_TabManager`。
2. **查重 key 继续用 `GetIdentifier()`。** 不要新的 `ResourceId` 类型，也不优先改成 `Resource*`。
3. **保留现有 `OpenResource` 槽签名。** 现签名是  
   `(Resource*, int line, int pos, QString caret, QUrl fragment, bool precede_current_tab)`。  
   Book Browser / TOC / Validation 连着这条槽。`precede_current_tab` 已被 Split At Cursor（`CreateSectionBreakOldTab`）使用。`OpenDisposition` 只能新增重载或默认参数。
4. **ActiveGroup 变化必须走现有 `TabChanged`。** 只改 `GetCurrentContentTab()` 不够。焦点点到另一组里已经是 current 的 tab 时，`QTabWidget::currentChanged` 不发火，必须由 focus 路径补发，并重连 action。
5. **D1–D5** 见 §2。
6. **DevTools 改动不写回 EPUB CSS。** 只当作临时 inspection override。
7. **Find/Replace 留在两组下面。** 中央栏保持 `TabManager + FindReplace + FindReplacePlus`。不要把 Find 搬进某一个 group。
8. **V1 最多两组、仅垂直。** 不做左右分屏、不递归 grid、不双 MainWindow、不用 `QTWEBENGINE_REMOTE_DEBUGGING`。
9. **不 clone tab。** Split Down 只创建空的第二组。Move 是 `removeTab` + `insertTab`，不重新加载。
10. **新文案英文 source + 翻译。** 走现有 `tr()` 与 zh_CN / zh_TW；覆盖测试必须过。

---

## 6. Developer Tools：嵌入现有 Inspector

### 6.1 目标布局

```text
PreviewWindow (QDockWidget)
└── 现有 m_MainWidget
    └── QSplitter(Qt::Vertical)     childrenCollapsible = false
        ├── 现有 wrapper + ViewPreview + 工具按钮行
        └── Inspector（改为可嵌入 QWidget，不再作为独立 QDialog 弹出）
```

浮动 Preview Dock 时，DevTools 跟着走。这是选内部 splitter、而不是第二个 `QDockWidget` 的原因。

### 6.2 对 `Inspector` 的改动

- 基类从 `QDialog` 改为 `QWidget`（或内部保留 view，外层用 `QWidget` 包装）。几何记忆从独立 `inspect_dialog/geometry` 改为 Preview splitter state。
- 保留：`GetInspectorProfile()`、`setInspectedPage` / `setInspectedPage(nullptr)`、zoom 快捷键、`zoomInspector` 设置。
- 顶部最多一条轻量栏：标题 + Close。Chrome DevTools 自己的 Elements / Console / Styles 不要再做一套按钮。
- Close = 隐藏 splitter 第二项，**不**销毁 `QWebEnginePage`。再次 Inspect 仍是同一实例。
- `WA_DeleteOnClose` 本来就是 false，保持。

### 6.3 生命周期

```text
MainWindow 拥有 PreviewWindow
PreviewWindow 拥有 ViewPreview 与 Inspector
换书 / 关窗口：
    Inspector::StopInspection()     // setInspectedPage(nullptr)
    再销毁 Preview / Inspector
```

`ViewPreview` 在构造时固定 `m_ViewWebPage`。`CustomSetDocument` 是在同一 page 上加载；清缓存会 `setUrl("")` 再加载，page 指针通常不变。因此**不必每次 Preview 刷新都 rebind**。需要 rebind 的时机：

- 第一次显示 DevTools
- 确认 page 指针变了
- 换书后重新 `InspectPageofView(m_Preview)`

### 6.4 hideEvent（相对今天是行为变化）

今天 `PreviewWindow::hideEvent` 会 `StopInspection()` 并 `close()` Inspector。Preview 与 TOC tabify 时，切到 TOC 就会拆掉绑定。

嵌入之后：

- Preview dock 被 hide / tabify：DevTools 只随父控件隐藏，**不** `StopInspection()`。
- 用户点 DevTools 的 Close，或 View 菜单关掉 Developer Tools：只藏 splitter 第二项，不断开 inspected page。
- 关 EPUB / 关 MainWindow：才 `StopInspection()` 并销毁。

### 6.5 入口

| 入口 | 行为 |
|---|---|
| Preview 工具栏现有 Inspect 按钮 | 切换嵌入 Inspector 的显示 |
| `View → Developer Tools`（由死代码 `InspectHTML` 改成） | 显示 Preview dock，并显示 Inspector |
| Preview 右键 Inspect Element | V1 若 Qt `QWebEnginePage::InspectElement` 可用则接上，显示面板并尽量选中节点；不可用则只打开面板。不要自己 `elementFromPoint` + CDP |

没有第二套 DOM 检查实现。

### 6.6 默认与持久化

- 默认：DevTools 面板隐藏。对象可以继续在 `PreviewWindow` 构造时创建（与今天一致）；若嵌入后启动成本可测偏高，再改为首次打开时创建，对外行为不变。
- 第一次展开比例：Preview 约 60–70%，DevTools 约 30–40%。之后用 `QSplitter::saveState` / `restoreState`。
- 存：`preview/devToolsVisible`、`preview/devToolsSplitterState`。缺省键 = 隐藏。

### 6.7 非目标

- remote Chrome / CDP server / 外部浏览器
- 自定义 DOM 树、自定义 CSS inspector、JS debugger UI
- DevTools 改样式写回 EPUB
- V1 Detach 成独立 Dock（P1）

---

## 7. Editor Groups：演化 TabManager

### 7.1 结构

```text
TabManager : public QWidget          // 不再自己就是 QTabWidget
├── QSplitter(Qt::Vertical)          childrenCollapsible = false
│   ├── TabGroup 0  (Primary)        // 抽出今天的 QTabWidget + TabBar + 大部分方法
│   └── TabGroup 1  (Secondary)      // 按需创建；未 split 时不存在或隐藏
└── 跨组索引：identifier → (TabGroup*, ContentTab*)
```

每个 `TabGroup` 只负责：tab 展示、本组 current、empty state、把 tab 交给另一组。不负责 Resource 保存、OPF、Book Browser、Preview、EPUB。

`TabManager` 继续对外提供今天这组 API，语义扩展为跨组：

- `OpenResource` / `GetCurrentContentTab` / `GetContentTabs` / `GetTabResources`
- `CloseTab` / `CloseOtherTabs` / `CloseAllTabs` / `CloseTabForResource`
- `ResourceTabIndex` / `SaveTabData` / `ReopenTabs` / `TabChanged`

`GetContentTabs()` 返回两组并集。`PluginSession` 不改调用方式。

V1：最多两组，仅垂直。内部可留 `enum class EditorGroupDirection { Up, Down }`，左右分屏不进 V1。

### 7.2 单实例不变量

一个 MainWindow / 一本书生命周期内：

```text
Resource.GetIdentifier()
    → 至多一个 ContentTab
    → 恰好在一个 TabGroup
```

禁止两组同时打开同一文件。允许把唯一实例从一组 `take` 到另一组。Debug 构建在注册时：

```cpp
Q_ASSERT(!m_OpenById.contains(resource->GetIdentifier()));
```

### 7.3 为什么必须禁止多实例

多个 UI owner 绑同一 Resource 会把 save / undo / focus / Preview / Find / rename / delete / plugin / session 全部变成“谁说了算”。底层即使共享 `QTextDocument`，生命周期更乱。不值得做。

### 7.4 OpenResource（唯一入口，扩展而不是替换）

伪代码（保持旧槽可用）：

```cpp
void TabManager::OpenResource(Resource *resource,
                              int line, int pos,
                              const QString &caret, const QUrl &fragment,
                              bool precede_current_tab,
                              OpenDisposition disposition = OpenDisposition::ActiveGroup)
{
    if (SwitchedToExistingTab(...))  // 跨组查找 identifier
        return;                      // focus 所在组 + 应用滚动位置

    TabGroup *target = ResolveTargetGroup(disposition);
    ContentTab *tab = CreateTabForResource(...);
    target->AddTab(tab, precede_current_tab);
    Register(resource->GetIdentifier(), target, tab);
}
```

`OpenDisposition`：

```cpp
enum class OpenDisposition {
    ActiveGroup,   // 默认；所有现有调用点
    OtherGroup     // 新入口；目标由 §10 解析
};
```

已打开则**永远 focus 现有实例**，忽略 disposition，不移动、不复制。状态栏可提示：

```text
chapter.xhtml is already open in the other editor group.
```

不要用 `QMessageBox`。这是导航，不是错误。

`MainWindow::OpenResource` 继续转发。需要 OtherGroup 的动作走新重载，例如 `OpenResourceInOtherGroup`。

### 7.5 目标组解析（D5）

```text
ResolveTargetGroup(ActiveGroup) = 当前活动组（未 split 则主组）

ResolveTargetGroup(OtherGroup):
    若未 split → SplitDown()，再按设置选目标
    设置 InactiveGroup（默认）：
        活动在上 → 下组；活动在下 → 上组
    设置 AlwaysLower：
        下组
    设置 AlwaysUpper：
        上组
```

设置只影响**尚未打开**的 Resource。已打开的文件仍 focus 原处。

### 7.6 Split / Join / Move

**Split Editor Down**

- 只创建空的第二组。不复制当前文件。
- 菜单：`View → Editor Layout → Split Editor Down` / `Join Editor Groups`。
- 与 `Edit → Split At Cursor`（`actionSplitSection`，快捷键 id `MainWindow.SplitSection`）必须在文案和 shortcut id 上分开。

**Open in Other Editor Group**

- Book Browser 右键新增此项。
- 未 split 时先 `SplitDown()` 再打开。
- 已打开则 focus，不移动。

**Move Editor to Other Group**

- Tab 右键。未 split 时先 `SplitDown()`。
- `source->removeTab(tab)` + `target->insertTab(tab)`。同一 `QWidget`。
- dirty / undo / cursor / ownership 不变，只改索引里的 group。
- 主组最后一个 tab 禁止 Move（D4）。此时动作 disabled。

**Join Editor Groups**

- 把第二组的 tab **按现有顺序** 接到主组后面。不要按路径重排。
- 不得 save → close → reload。
- Join ≠ Close Editors。

### 7.7 关闭规则

| 动作 | 语义 |
|---|---|
| 关一个 tab | 只关那个 Resource 的 editor，摘掉索引 |
| 关第二组最后一个 | 留下空的第二组，不自动 Join |
| 关主组最后一个（非 force） | 拒绝，与今天一致（D4） |
| Close Other Tabs | 两个 group 里除活动 tab 以外全部关掉（D3）。若第二组被关空，留下空组 |
| Close All（现有窗口级） | 保持现有菜单含义；仍受 D4 约束，除非 force / 换书 |
| 换书 `SetNewBook` | `CloseAllTabs(true)`，两组和索引都清掉 |

空的第二组显示一行轻量提示：`Open a file from Book Browser`。不要 Welcome Page，不要快捷键教程。

### 7.8 拖动

- 组内：现有 `setMovable(true)`。
- 跨组拖动：P1。若做，只能是 MOVE，并原子更新索引。

### 7.9 Active Group

下列交互把该组标为活动：本组 TabBar、CodeView、CSS editor、scroll area 获得 focus，或点击 tab。

不要只听 `QTabWidget::currentChanged`。

```text
focus / 点击
    → TabManager::SetActiveGroup
    → 若活动 ContentTab 变了
        → emit TabChanged(old, new)
        → ChangeSignalsWhenTabChanges
        → UpdateMWState / UpdateUIOnTabChanges / UpdatePreview
```

未活动组里切换它自己的 current tab：不重连 MainWindow actions，也不改 Preview 的 HTML 上下文（除非随后该组成为活动组且新 tab 是 HTML）。

`GetCurrentContentTab()` 的实现改为 `ActiveGroup()->CurrentTab()`。调用点不用改名。

### 7.10 Preview 与两组

继续用 `m_PreviousHTMLResource`。活动 tab 是 CSS 时，Preview 仍显示最近那个可渲染 XHTML，并在 CSS 保存后刷新。Resource 在哪一组不得改变 Preview invalidation。

### 7.11 Undo / 编辑命令

Undo/Redo/Cut/Copy/格式化/Split At Cursor 等全部作用于活动组的当前 tab。实现靠 §7.9 的重连，不靠各命令自己判断 group。

### 7.12 Save All / Rename / Delete

- Save All：继续走现有 Book / `SaveTabData()` 遍历**所有**打开 tab（并集），不要按组保存。
- Rename：对象与 UUID 不变；现有 `TabRenamed` 更新标题即可，不必重注册。
- Delete：走现有关闭/保存询问，再从索引和所在组移除。EditorGroup 不得自己删 Resource。

### 7.13 视觉与无障碍

- 活动组：沿用现有 tab 强调。
- 非活动组：略弱。不要渐变、整块高饱和、两组不同主题色、厚边框。
- 活动状态不能只靠颜色；键盘焦点必须能表达当前编辑对象。
- 只用 `QSplitter`，不要自绘 drag handle。

---

## 8. 中央栏与空间

```text
QFrame
├── TabManager          // 未 split：一组；split：上下两组
├── FindReplace
└── FindReplacePlus
```

Find 面板始终在两组下面。窄窗口时两组与 Find 会挤。对策：

- Editor splitter 与 Preview/DevTools splitter 都 `childrenCollapsible = false`，并设合理 `minimumHeight`，避免拖成 0px 看起来像文件消失。
- 不把 Find 拆进 group。
- FlowTab 已无内部 Book/Code 分屏，不存在三重编辑分屏。

---

## 9. 状态保存（D2）

只存布局，不存打开的文件。

```text
preview/devToolsVisible
preview/devToolsSplitterState
editorGroups/enabled
editorGroups/splitterState
editorGroups/activeGroup
editorGroups/otherGroupTarget     // 见 §10，这是偏好不是 session
```

用 `QSplitter::saveState` / `restoreState`，不要手写像素。

缺省（第一次升级、键不存在）：

- 单组，无 DevTools 面板
- 外观与现在一致

恢复规则：

- splitter state 非法 → 50/50
- `enabled = true` 但没有书 / 刚打开书 → 恢复空的第二组，然后 `SetBook` 仍只开第一个 HTML 到主组
- **不要**根据 tab 数量猜测是否该 split
- 不写 OPF、不写 EPUB metadata、不往出版内容里塞布局

换书不恢复“上一本书打开过哪些文件”。Checkpoint checkout 仍用今天临时记下的 bookpath 列表，全部开进主组（或当时的活动组），不发明第二套 checkpoint tab 归属。

---

## 10. 设置（D5）

Preferences 只加一项，放 Enhanced 偏好页（`ModifiedVerPrefsWidget`），英文 source：

```text
Open in Other Editor Group:
    ( ) Inactive group          // 默认
    ( ) Always lower group
    ( ) Always upper group
```

键名建议：`editorGroups/otherGroupTarget` = `inactive` | `lower` | `upper`。

V1 **不加**：

- Enable Multiple Editors（用 View 菜单现场开关）
- Maximum Groups
- Automatically Open CSS Below（被上面三项覆盖）
- Developer Tools Position

布局结果靠 session，不靠更多 checkbox。

---

## 11. 菜单、快捷键、键盘

```text
View
├── Preview                         // 现有 dock toggle
├── Developer Tools                 // 新；接改造后的 InspectHTML
└── Editor Layout
    ├── Split Editor Down
    └── Join Editor Groups

Book Browser 右键
├── Open in Other Editor Group      // 新
└── Open With ...                   // 现有，含义不变

Tab 右键
├── Close Other Tabs                // 现有，全窗口（D3）
└── Move Editor to Other Group      // 新；主组仅剩一个时 disabled
```

快捷键：动作进现有 `KeyboardShortcutManager`，**V1 不抢默认组合**。先审计冲突再考虑默认。不要直接加 `Ctrl+K Ctrl+…`。

可选动作（V1 注册、不设默认键）：

- Focus Upper Editor Group
- Focus Lower Editor Group / Focus Other Editor Group

`Next Tab` / `Previous Tab` 只在**活动组**内循环。跨组用 Focus Other。

---

## 12. 工作流

典型：

1. 打开 `Text/chapter01.xhtml`（主组，与现在一样）
2. Book Browser 右键 `Styles/style.css` → Open in Other Editor Group
3. 默认设置下，CSS 开到非活动组（此时即下组）
4. Preview → Inspect / View → Developer Tools

```text
Book Browser │ chapter.xhtml        │ Preview
             │                      │
             ├──────────────────────┼──────────
             │ style.css            │ Inspector
```

若用户先点了下组再 “Open in Other”，默认设置会把新文件开到**上组**。若希望 CSS 总在下面，把设置改成 Always lower group。

`GoToLinkedStyleDefinition` 在 V1 仍开到活动组（行为不变）。P1 可加 “Open Linked Stylesheet in Other Editor Group”，复用现有 CSS 解析，只改 disposition。

---

## 13. 性能

- Split 不得复制 Resource、QTextDocument、BookView（已无）、CSS 文档模型。成本就是两个本来就能打开的 `ContentTab` 同时绘制。
- DevTools：Toggle 不销毁 Chromium page。若隐藏时仍明显吃资源，再考虑 “Destroy on Preview close”；普通 Toggle 不要反复创建销毁。
- 今天 Inspector 已在 Preview 构造时创建。嵌入后启动成本不应比现在差。若要改 lazy，作为 Patch A 的可选收尾，不作为产品开关。

---

## 14. 明确不做

- 复制 Tab / 把 CSS 塞进 `FlowTab` / 重写成 VS Code workbench / 两个 MainWindow 编同一本书
- remote debugging 端口
- DevTools → 自动写回 CSS
- V1：跨组 tab 拖动、左右分屏、Detach DevTools、自动打开关联 stylesheet、持久化打开的文件列表
- 改 plugin API 形状（只保证现有 `GetContentTabs` / `TabChanged` 在两组下仍正确）

---

## 15. 验收

### Developer Tools

| ID | 条件 |
|---|---|
| DEV-00 | 现有 Inspect 按钮、zoom、Inspector profile、关闭后再开仍是同一实例 |
| DEV-01 | Inspect 后 DevTools 出现在 Preview 下方 |
| DEV-02 | Preview 与 DevTools 可拖 splitter |
| DEV-03 | 关闭 DevTools 后 Preview 占满 |
| DEV-04 | 再次打开不创建第二个 DevTools 实例 |
| DEV-05 | Preview Dock 浮动后 DevTools 跟着走 |
| DEV-06 | 重新 dock 后布局正常 |
| DEV-07 | 重启恢复 splitter 比例与可见性（不恢复 EPUB 内容状态） |
| DEV-08 | 默认第一次 DevTools 隐藏 |
| DEV-09 | 换书后 DevTools 正确 rebind 或安全停用 |
| DEV-10 | 关 EPUB / MainWindow 无 WebEngine dangling-page crash |
| DEV-11 | Preview 与 TOC tabify 时，嵌入 DevTools **不再**被 hideEvent 拆绑定 |
| DEV-12 | `View → Developer Tools` 会显示 Preview 并显示 Inspector |

### Split Editor

| ID | 条件 |
|---|---|
| SPL-00 | 未 split 时 `OpenResource` 与现在逐位一致，包括 `precede_current_tab` |
| SPL-01 | Split Editor Down 生成第二组 |
| SPL-02–04 | 上可开 XHTML，下可开 CSS，两边独立编辑 |
| SPL-05–06 | 同一 Resource 不能两个 ContentTab；再开则 focus 原处 |
| SPL-07–10 | Move 移动原 QWidget；undo / dirty / cursor 保持 |
| SPL-11 | 活动组 Ctrl+Z 只作用当前 editor |
| SPL-12–13 | Find/Replace 与 Book Browser 双击已打开文件时跳到所在组 |
| SPL-14 | FlowTab 仅 Code View 的现有行为不变 |
| SPL-15 | Join 不关闭、不重新加载 |
| SPL-16 | 第二组空时不自动消失 |
| SPL-17 | 关 EPUB 时两组都按现有规则保存/关闭 |
| SPL-18 | 无 tab 归属可恢复，故不产生 duplicate editor；`enabled` 只恢复空第二组 |
| SPL-19–20 | Rename 后 ownership 仍对；Delete 后索引无悬空 |
| SPL-21 | 焦点切组后 Undo/Cut/格式化/Split At Cursor 作用在新活动 tab |
| SPL-22 | `PluginSession::GetContentTabs` 看见并集；`editor.activeChanged` 不把未聚焦组的切 tab 当活动变化 |
| SPL-23 | `SetNewBook` 清掉两组和索引 |
| SPL-24 | 主组最后一个 tab 不能关、不能 Move（D4） |
| SPL-25 | Close Other Tabs 关掉另一组里的 tab，只留下活动那一个（D3） |
| SPL-26 | OtherGroup 默认开到非活动组；三项设置分别生效 |

综合回归：HTML+CSS、HTML+HTML、CSS+CSS、SVG+CSS；Find All / Replace All / Saved Search / Spellcheck / Undo / Redo / Save All / Close / Close All / rename / delete / 外部修改 / Checkpoint / Preview 刷新 / Validation / 插件。

Plus Replace All、Saved Search、batch rollback、Automate 不得被 group-local save pipeline 绕开。Editor Group 只是 UI layout。

---

## 16. 自动化测试

打在抽出后的 `TabManager` / 索引上。已有 `tests/tab_bar_middle_click_test.cpp` 证明 TabBar 可单测。

1. 开 A 再开 A → 仍一个 tab
2. Split 后从另一组开 A → focus 原组，不新建
3. Move 后指针相同，索引只改 group
4. Join 后顺序 = 主组原序 + 第二组原序
5. `GetContentTabs()` 并集；`CloseTabForResource` 两组都能关
6. `precede_current_tab` 在主组仍把新 tab 插到当前左侧
7. OtherGroup + `inactive` / `lower` / `upper` 选对目标组
8. 主组最后一个 tab 拒绝 Close 与 Move
9. Close Others 清掉非活动组里的 tab

DevTools 以手工清单为主（WebEngine 嵌入测不稳定）。

---

## 17. PR Plan

| PR | 标题 | 主要文件 | 依赖 | 用户可见 | 说明 |
|---|---|---|---|---|---|
| A | Embed Inspector in Preview splitter | `PreviewWindow.*`, `Inspector.*`, `MainWindow.cpp`（InspectHTML / View 菜单）, 翻译 | 无 | 有 | 停靠现有 DevTools；修正 hideEvent；`InspectHTML` 变成真正入口 |
| B | Extract TabGroup from TabManager | `TabManager.*`, 新 `TabGroup.*`, 调用点编译通过 | 无（可与 A 并行） | 无 | 仍只有一组；`OpenResource` / `GetContentTabs` 行为不变 |
| C | Split Down / Join / empty secondary | `TabManager.*`, `MainWindow.cpp` 菜单, `main.ui`, ShortcutManager | B | 有 | 空第二组；主组至少一 tab；splitter 最小高度 |
| D | Open in Other Group / Move + target setting | `BookBrowser.*`, `TabBar.*`, `ModifiedVerPrefsWidget` / SettingsStore | C | 有 | D5 三项设置；已打开则 focus |
| E | ActiveGroup drives TabChanged | `TabManager.*`, `MainWindow.cpp` 的 focus / ChangeSignals | C | 正确性 | SPL-21 / 插件事件 |
| F | Persist layout only | `MainWindow` Read/WriteSettings, PreviewWindow, TabManager | A, C | 有 | 只存 D2 列出的键 |
| G | Tests, i18n, user docs | `tests/*`, `.ts`, 用户文档 | D–F | 无功能 | 英文 source；zh_CN / zh_TW |

每个 PR 必须独立可回滚。不要在一个 patch 里同时改名 MainWindow API、重写 tab、嵌入 DevTools、改设置。

P1（本轮关门后）：

- 跨组 tab 拖动（MOVE）
- Focus Other Group 默认快捷键
- Detach DevTools 回浮动窗口
- Open Linked Stylesheet in Other Group（接现有 `GoToLinkedStyleDefinition`）
- Split Left / Right
- DevTools 规则 → 源 CSS 行（先解决 local scheme / 行映射）

---

## 18. 终局结构

```text
MainWindow
├── BookBrowser
├── TabManager
│   └── QSplitter [Vertical]
│       ├── TabGroup 0
│       │   └── FlowTab / 其他 ContentTab
│       └── TabGroup 1
│           └── CSSTab / 其他 ContentTab
├── FindReplace / FindReplacePlus
├── PreviewWindow
│   └── QSplitter [Vertical]
│       ├── ViewPreview
│       └── Inspector (embedded)
├── TOC / Clips / Validation
└── ...
```

```text
Book
└── Resource (stable UUID)
      ↓ 至多一个
ContentTab
      ↓ 恰好一个
TabGroup
```

打开 EPUB：与现在几乎一致（单组、无 DevTools 面板）。需要并排改 CSS 时右键 Open in Other Editor Group。需要看计算样式时 Inspect。全过程遵守单 Resource 单 editor。
