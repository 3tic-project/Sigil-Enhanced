# Sigil-Enhanced 2.8.1E8 更新说明

发布日期：2026 年 8 月 16 日。

本文记录 `2.8.1E8` 相对上一个发布版本 `v2.8.1E7` 的主要变化。比较基线为
`v2.8.1E7`（`6a2cce9c8`）。

## 重点更新

### 内置纵横排版转换

- 在“增强”菜单加入纵横排分析、竖排转横排、横排转竖排三个入口，并接入 Automate 与快捷键。
- 逐页分析可重排 EPUB 的实际书写方向、关联 CSS、固定版式和转换风险，而不是整书套用一个方向。
- 经验证的 DPFJ/EBPAJ `.vrtl` / `.hltr` 页面只切换页面根 class，不改写共享样式表；其它模板使用带标记的可逆兼容覆盖，原 CSS 与 inline style 保持不变。
- `writing-mode` 与 spine `page-progression-direction` 分开处理。整书转换可以同步翻页方向，并能精确恢复转换前的 `ltr` / `rtl` / `default` 或未设置状态。
- 固定版式、图片页、含文本 SVG、脚本绝对定位、方向信号冲突和解析失败页默认跳过。只转换部分文件或书中含局部固定版时，不会改整书翻页方向。
- 写回前在内存中完成全部计划与不变量校验（XML、可见文本、id/name、href/src、ruby、图片、链接元素）。任一计划文件失败，整批都不写回。
- 确认有修改后先创建恢复 Checkpoint，再对每个变化资源写回一次。当前文件可在代码视图中单步撤销；整书恢复使用转换前的 Checkpoint。
- 缺少合法 UUID 的 EPUB 会在隔离快照中准备 UUID，只有 Checkpoint 成功后才把同一份 OPF 作为一步可撤销编辑提交。
- 反向转换只恢复插件自己改过的页面，不会把书中原本就是目标方向的扉页、版权页或局部混排一起改掉。多次互转不会累积覆盖或改坏局部 `writing-mode`。

详细用法与安全边界见 [纵横排版转换](VerticalLayoutConversion.md)。

### 查找替换与 Book Browser

- 查找替换（含 Python 函数替换）在替换结果与原文相同时不再调用 `SetText()`，未命中的文件不会再被整篇刷新。
- 修复 Book Browser 阅读顺序拖拽，spine 重排不再依赖容易过期的 TreeView 选择状态。
- 恢复从 Finder/资源管理器把文件拖到 Book Browser 以添加资源；阅读顺序拖拽改写后外部 URL 放置被忽略。

### Preview 开发者工具

- 现有 Inspect 不再弹出独立窗口，而是停靠在 Preview 下方，随 Preview Dock 一起浮动或重新停靠。
- `查看 → 开发者工具` 会显示 Preview 并打开嵌入的 Inspector。关闭面板只隐藏，不销毁 WebEngine 页。
- 重启只恢复 DevTools 是否打开和 splitter 比例，不恢复打开过的 EPUB 文件。

### 拆分编辑器

- `查看 → 编辑器布局` 可向下拆成上下两组，同时编辑两个不同资源；同一文件仍只有一个编辑器。
- Book Browser 右键「在另一编辑器组打开」，标签右键「移动到另一编辑器组」。默认开到非活动组，可在增强偏好里改为总是下组或上组。
- 点击某一组后，撤销/剪切等命令跟随该组。重启只恢复是否拆分和分隔条比例，不恢复打开过的文件。

### 上游同步与界面

- 同步上游 Sigil 的 TOC 多选移动：选中项按连续块上移/下移，并修复右移时 `QPersistentIndex` 未更新导致的崩溃。
- 图片调整窗口显示当前文件体积（KB），保存成功后刷新该数值。
- 补齐“调整图片”对话框中缺失的翻译条目。

## 兼容性说明

- 保存搜索、快捷键 action ID 与既有 Automate 命令名称保持不变。
- 新增 Automate 命令：`AnalyzeVerticalLayout`、`ConvertVerticalToHorizontal`、`ConvertHorizontalToVertical`。
- 纵横排转换不会改写共享样式表中的 `text-orientation`、`text-combine` 或 `vert`/`vrt2`；兼容覆盖带专用标记，可被反向转换删除。
- 全书批处理的跨资源恢复仍以 Checkpoint 为准；Code View 撤销栈按资源维护，不提供跨多个文件的一次全局 Undo。
- 转换不处理固定版式 EPUB，也不把“直排”与 “RTL 翻页”写死绑定。

## 验证范围

- Debug Sigil 应用完整编译、链接及内置 Python 包校验。
- CTest 覆盖纵横排分析/模板识别/样式解析/CSS 变换/不变量、转换菜单与 Automate 契约、Checkpoint UUID 准备、Book Browser 阅读顺序拖拽，以及三语翻译完整性。
- 简体中文、繁体中文和日文目录必须保持全部条目已翻译且无 `unfinished`。

本更新说明对应发布标签 `v2.8.1E8`。Windows 与 macOS 正式安装包、签名和校验和由该标签触发的发布流程按[发布清单](ReleaseChecklist.md)生成。
