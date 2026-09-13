# Sigil-Enhanced 2.8.5E10 更新说明

发布日期：2026 年 8 月 22 日。

这一版是 `2.8.5E9` 之后的更新。比较基线为 `v2.8.5E9`（`6a257840e`）。

## 这一版多了什么

### 把 Kindle KFX 转成 EPUB

没有 DRM 的 `.kfx` / `.kfx-zip` 可以直接转成 EPUB，不用再找外部工具。

- 「增强」菜单里有入口，也可以把文件拖进窗口。
- 转完可以另存为 EPUB，或在新窗口打开继续改。
- 想顺便整理目录结构，转换时勾选即可，默认不勾。

转换核心来自 [kfx2epub](https://github.com/2778995958/kfx2epub)，感谢原作者。用法见 [KFX 转 EPUB](KfxImport.md)。

### 预览里看网格

Preview 上可以叠一层参考网格，用来看行距、栏宽齐不齐。网格只显示在软件里，**不会写进 EPUB**。

- 横线、竖线可以分开开关，间距也可以分开设。
- 改设置马上能在预览里看到；取消会回到打开设置前的样子。
- Preview 右键菜单也能开关网格。

详见 [预览网格](VisualTypesettingAids.md)。

### 没改过的书，保存不再动文件

打开一本 EPUB，什么都没改就点保存：原文件一个字节都不会变。

- 「另存为」和「保存副本」是直接复制原来的文件。
- 默认不再写入「Sigil version」。
- EPUB 3 的修改时间，也只在书真的改过之后才更新。

### 代码视图双击选整段内容

XHTML 代码视图现在默认把双击解释为“选择当前段落/标签的内部内容”。Ruby、实体、
span 等内部源码会完整保留，最外层标签不进入选区。正文右键菜单也能选择内部内容
或完整元素。

需要逐词编辑时，可在“偏好设置 → 编辑器 → 代码视图 → 双击选择”切回“字或词
（原有行为）”。标签、属性、Shift/Alt 双击和非 XHTML 编辑器仍保持原有行为。
完整规则见[代码视图文本选择](CodeViewTextSelection.md)。

### Clips 按钮直接显示实际快捷键

Clips 工具栏前十个可用按钮现在显示快捷键角标：默认依次为 1–9、0。用户改写
快捷键后，角标和 tooltip 会立即同步；非数字组合显示平台原生按键文本，空间不足
时使用简短“按键”标识，清空绑定则隐藏角标并明确提示未分配。

可在“偏好设置 → 外观 → 主界面”关闭该提示。片段 HTML 会在 tooltip 中安全转义，
工具按钮的可访问名称也包含片段名、槽位和完整快捷键。详见
[Clips 快捷键角标](ClipShortcutBadges.md)。

### 调目录层级时保持阅读顺序

“编辑目录”的左箭头现在会把提升条目后面的同级条目接到它下面；多选时按操作前的
稳定位置分段处理，因此不会因为升一级意外改变目录先序阅读次序。右箭头按连续区段
移入前一个同级条目，已有子目录保持不变。

- 默认开启“提升时接管后续同级条目”，也可关闭以使用旧行为。
- 对话框内的升降级、同级移动、增删、标题和目标编辑都能独立撤销/重做；输入标题时
  文字撤销优先。
- 取消不写 Book；确定但没有变化时不重写资源，也不再把书标脏。
- EPUB 3 同时含 NCX 时会提示同步状态，并提供默认关闭的显式同步选项。
- 纯层级写回保留 Nav/NCX 原节点的 id、属性与内联标记，并保留其他 nav、NCX 元数据
  和 page-list。

详见[目录层级编辑](TocHierarchyEditing.md)。

Native Agent 现在也能用 `toc.inspect_hierarchy`、`toc.plan_transform` 和
`toc.apply_transform` 调用同一套原生规则。计划绑定当前会话、书籍修订和精确
Nav/NCX 源码；apply 只创建独占暂存事务，预览后才可提交。层级写回保留原节点属性、
内联标记、标题、目标和非 TOC 区域，不通过改写 XHTML 标题来制造层级。详见
[Native Agent 原生目录层级工具](AgentNativeTocTools.md)。

### 安全整理 DIV 伪段落

“增强”菜单中的旧 BookLive 段落入口已扩展为通用 DIV 段落结构工具。它可以分析
当前文件、Book Browser 选中的 XHTML 或全书，并在写入前显示每个资源的分类、
状态、CSS 风险、源码差异和前后预览。

- 默认只把通过内容模型和 CSS 风险门的正文叶子 `div` 改为 `p`；空行、场景分隔、
  图片包装和单层嵌套视觉块分别由设置控制，默认保持不变。
- 标题包装、Ruby、锚点、链接、图片和复杂布局有顺序保真检查；默认只改起止标签名，
  不重新序列化整份 XHTML。
- 页面关联的内联 CSS、链接样式表及递归 `@import` 会进入分析；标签选择器或无法
  证明 `div`/`p` 默认 margin 等价时，文件只供人工检查。
- 预览绑定 XHTML 和 CSS 内容指纹；确认时重新分析选中子集。批量提交创建恢复
  Checkpoint 和逐资源撤销，失败时回滚。
- 原 `AnalyzeBookLiveParagraphs` / `NormalizeBookLiveParagraphs` Automate 命令名保留；
  无界面规范化使用明确的旧版兼容预设。

详见[DIV 段落结构规范化](DivParagraphNormalization.md)。

Native Agent 也可通过同一 C++ 引擎执行 `paragraphs.analyze`、
`paragraphs.plan` 和 `paragraphs.apply`。计划绑定书籍修订、XHTML/CSS 哈希和审批摘要；
apply 只创建独占暂存事务，仍须预览与提交，且明确报告未运行完整 EPUBCheck。该入口
目前属于内置 Agent，不是公共 MCP 工具。详见
[Native Agent 原生段落计划工具](AgentNativeParagraphTools.md)。

### Agent 结果不再混淆“已应用”和“已保存”

Native Agent 的事务状态卡现在会把暂存、写入当前 Book 和保存 EPUB 分开显示。
Preview 按文本、新增、重命名、删除、元数据、阅读顺序和 TOC 层级列出变更，并明确
活书未变；Applied 显示应用项数和 Book revision，同时说明 EPUB 尚未保存、完整
EPUBCheck 未运行。

提交前 rollback 会显示“已丢弃暂存变更，活书未变”，不再被误解成提交后撤销。
仅修改既有文本资源的提交现在会自动建立任务恢复点，Applied 卡提供
**Restore this task**。恢复前会一次性核对该任务写入后的每个目标文件；任一目标后来被
人工或其他任务改过，就会显示冲突并保持整本书不变，不会先恢复一半。未被该任务修改的
后续编辑会原样保留。新增、删除、重命名、metadata、阅读顺序或 TOC 等结构提交会明确
说明没有任务恢复点，仍需使用 Sigil Undo 或事先建立的宿主 Checkpoint。恢复点仅在当前
打开书籍的内存会话中有效，不能代替保存、EPUBCheck 或崩溃恢复。Conversation Markdown
导出保留相同状态说明。详见 [Native Agent](NativeAgent.md#预览提交与恢复状态)。

### Agent 会明确显示当前书籍和实时选区

Agent 停靠栏新增当前书籍状态行，同时显示 EPUB 文件名、`dc:title`、资源数、
Saved / Unsaved 和 Agent revision。保存、重新变脏、资源增删移动、另存为和切换书籍时
都会刷新，便于多窗口工作时确认实际目标。

Selection 芯片不再永久禁用：代码编辑器出现非空选区时，它会显示真实 UTF-16 范围并
默认选中。发送后，Agent 上下文从当前内存资源读取该范围的精确源码，保留 Ruby/标签；
自动附加上限为 4096 code units，超长会明确提示继续分段读取。只选择 File 或 Selection
时不再暗中附加全书资源表和 Spine 样本。

范围现在是互斥的 Selection、Current file、Selected files 和 Whole book。默认优先
明确选区，否则当前文件；Book Browser 多选会实时进入 Selected files，保持顺序并去重，
最多自动附加 60 份片段，超出时报告省略数量。Whole book 必须显式选择，不再默认与
当前文件叠加。详见
[Native Agent](NativeAgent.md#当前书籍与上下文范围)。

### Agent 运行不会串到后来打开的书籍

每次 Agent 运行现在绑定到发送时的书籍会话 ID；模型返回和工具执行边界都会复核。
若同一窗口切换到另一本书，旧请求会取消，旧响应不会对新书执行工具。状态行显示书籍
会话短 ID，换书和防御性目标不匹配也有独立结果卡。

请求或审批尚未返回时关闭窗口，会先取消并等 Runner 退栈，再安全关闭窗口。运行期间
New Session 与模式选择会锁定；Provider / 模型设置若在嵌套事件循环中发生变化，则延后
到当前运行结束后应用，避免销毁仍在调用栈上的 Runner 或 Provider。详见
[Native Agent](NativeAgent.md#运行与书籍会话绑定)。

### Agent 会区分“已配置”和“已连通”

Agent 停靠栏新增提供商状态行，只显示提供商、模型和安全的 endpoint 主机。缺少
endpoint、API Key 或模型时会明确提示 **Setup required**；设置齐全时显示
**Configured · not tested**，不会在尚未请求服务器时声称已经连接。

真实模型请求开始后，状态会更新为正在连接，并根据显式请求事件记录最近一次成功、
失败或取消。401、403、404、408、429、5xx 与常见网络问题会显示可读摘要；完整错误仍
保留在 Error 卡片。服务端即使在错误正文中回显 API Key，也会在进入会话和 HTTP trace
前脱敏。详见 [Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

最近请求终态现在还显示耗时和完成时间。默认折叠的 **Technical details** 提供完整
session/request/book-session ID、目标 revision、模式和发送时冻结的范围句柄，同时继续只
展示安全的 endpoint 主机。

首个 Provider 请求失败时可点 **Retry**，它用原始文本和同一范围重新建立新请求；换书、
New Session 或本轮已经执行过工具时不会启用，避免旧范围串书或重复提交。这里没有后台
自动重试。详见 [Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

偏好设置现在另有 **Test Chat Completions**，不再需要拿“刷新模型列表”间接猜测聊天接口
是否可用。它用当前尚未保存的 URL、API Key 和模型发送一个不含书籍内容和工具的小请求，
关闭 thinking、最多生成 8 token，并在 15 秒内给出成功、鉴权/模型错误或明确超时；状态
只显示安全主机名和脱敏错误。修改连接字段会立即使旧测试结果过期，测试本身不会保存设置，
也不会伪造 Agent 停靠栏的最近书籍请求状态。测试成功后关闭偏好设置，会为这组完全相同
的 provider、URL、API Key 和模型保存一份指纹绑定的历史结果；Dock 在尚无真实请求时显示
上次测试时间。任一连接字段变化都会自动失效，界面也明确不把历史测试冒充实时在线。
探测现在线程池中运行，不会让偏好设置窗口在最长 15 秒的等待期间失去响应；若关闭设置
窗口，仍在等待的探测会被取消，后台完成信号不会访问已销毁界面。

**Refresh models** 也改为后台请求，最长等待 30 秒，并与连接测试互斥。关闭设置窗口会
取消尚未完成的刷新；服务端模型目录错误若回显当前 API Key，显示前会脱敏。

### Agent 可显示服务端 Token 用量

Native Agent 的正式流式请求现在默认向兼容服务请求 usage。展开 **Technical details**，
可以看到最近一次模型步骤的输入、输出、总计，以及服务端提供时的缓存输入和推理 Token。
这些数字也进入脱敏调试事件；不会用字符数自行估算，服务端未返回时会明确显示“未报告”，
而不是错误地显示为 0。

若某个 OpenAI-compatible 端点拒绝 `stream_options.include_usage`，可在“偏好设置 →
Native Agent”关闭 **Request token usage when supported**。独立的 Chat Completions 连接
测试仍保持最小请求，不发送这个可选字段，因此历史“测试成功”不代表端点支持 usage。

## 修了什么

- **EPUB 2 转 EPUB 3**：以前「Epub3 Tools」在 EPUB 2 下是灰的，转不了。现在菜单能打开。
- 转换时碰到某些少见的 HTML 字符实体会闪退，已经修好。
- 转成 EPUB 3 后，原来的章节目录会写进 Nav，不再是空目录。NCX 按 EPUB 3 惯例去掉，这是正常的。
- 关掉代码视图时，偶尔会因为语法高亮器拆卸而崩溃，已经修好。
- KFX 其实转成功了，界面却显示「已取消」，已经修好。
- 正则工作台应用规则时，不再每次强制做 Checkpoint，改成可选。

## 兼容性

- Automate 命令、快捷键、插件接口没有改。
- 当前 C++、头文件和 Qt Designer 源码中的 5,655 条活跃界面文案，已全部进入简体中文、
  繁体中文和日文目录且没有 unfinished 条目；严格目录覆盖测试通过。详见
  [PRD 实施审计](PRD-2026-09-05-Implementation.md)。

## 验证

- Debug 版 Sigil 能完整编过、链过，内置 Python 包也校验过。
- CTest 覆盖了 KFX 导入、无损保存、代码视图关闭、EPUB 2→3 实体与导航、预览网格、
  代码视图选择、Clips 快捷键角标、目录层级编辑、DIV 段落结构规范化和 Native Agent
  请求/用量状态。四语文案均可生成 `.qm` 且 0 unfinished；严格简中、繁中、日文目录
  覆盖检查通过。

本说明对应发布标签 `v2.8.5E10`。Windows / macOS 安装包、签名和校验和按[发布清单](ReleaseChecklist.md)在打标签后生成。
