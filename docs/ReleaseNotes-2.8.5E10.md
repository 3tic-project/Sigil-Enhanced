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
提交卡只建议在可用处使用 Sigil Undo，并明确本次 commit 没有创建整任务恢复点；
任务级恢复按钮和后续人工编辑冲突处理仍未实现。Conversation Markdown 导出保留
相同状态说明。详见 [Native Agent](NativeAgent.md#预览提交与恢复状态)。

### Agent 会明确显示当前书籍和实时选区

Agent 停靠栏新增当前书籍状态行，同时显示 EPUB 文件名、`dc:title`、资源数、
Saved / Unsaved 和 Agent revision。保存、重新变脏、资源增删移动、另存为和切换书籍时
都会刷新，便于多窗口工作时确认实际目标。

Selection 芯片不再永久禁用：代码编辑器出现非空选区时，它会显示真实 UTF-16 范围并
默认选中。发送后，Agent 上下文从当前内存资源读取该范围的精确源码，保留 Ruby/标签；
自动附加上限为 4096 code units，超长会明确提示继续分段读取。只选择 File 或 Selection
时不再暗中附加全书资源表和 Spine 样本。详见
[Native Agent](NativeAgent.md#当前书籍与上下文范围)。

## 修了什么

- **EPUB 2 转 EPUB 3**：以前「Epub3 Tools」在 EPUB 2 下是灰的，转不了。现在菜单能打开。
- 转换时碰到某些少见的 HTML 字符实体会闪退，已经修好。
- 转成 EPUB 3 后，原来的章节目录会写进 Nav，不再是空目录。NCX 按 EPUB 3 惯例去掉，这是正常的。
- 关掉代码视图时，偶尔会因为语法高亮器拆卸而崩溃，已经修好。
- KFX 其实转成功了，界面却显示「已取消」，已经修好。
- 正则工作台应用规则时，不再每次强制做 Checkpoint，改成可选。

## 兼容性

- Automate 命令、快捷键、插件接口没有改。
- 本版新增的简体中文、繁体中文、日文文案已经补齐；全目录仍有历史欠账，详见
  [PRD 实施审计](PRD-2026-09-05-Implementation.md)。

## 验证

- Debug 版 Sigil 能完整编过、链过，内置 Python 包也校验过。
- CTest 覆盖了 KFX 导入、无损保存、代码视图关闭、EPUB 2→3 实体与导航、预览网格、
  代码视图选择、Clips 快捷键角标、目录层级编辑和 DIV 段落结构规范化。新增三语文案可生成 `.qm`；覆盖检查仍报告
  既有的原生 Agent/KFX 目录欠账，未新增本功能漏译。

本说明对应发布标签 `v2.8.5E10`。Windows / macOS 安装包、签名和校验和按[发布清单](ReleaseChecklist.md)在打标签后生成。
