# Sigil-Enhanced 2.8.5E9 更新说明

本文记录 `2.8.5E9` 相对上一个发布版本 `v2.8.1E8` 的主要变化。比较基线为
`v2.8.1E8`（`08cee5fcf`）。E9 基于上游 Sigil `2.8.5` 代码线维护。

## 重点更新

### 上游基础版本与翻译同步

- 基础版本从上游 Sigil `2.8.1` 提升到 `2.8.5`，同步 PR #14 的版本与 wrapper 版本更新。
- 从 Transifex 同步全部语言目录，并新增瑞典语目录 `sigil_sv.ts`。
- 简体中文、繁体中文和日文目录继续由本项目维护，保持全部条目已翻译且无 `unfinished`。

### 修复

- Windows 下打开 Photoshop 导出的 WebP 图片不再失败；CI 会安装 `qtimageformats`，Windows 包必须带上 `qwebp.dll`，缺少插件时打包失败。
- EPUB 3 书籍 alternate-script 精炼缺少 `xml:lang` 时，Metadata Editor 仍能打开，不再因四次 Python 调用失败而显示空编辑器。
- 恢复从 Finder/资源管理器把文件拖到 Book Browser 以添加资源。
- 新建空 XHTML/CSS 文件时把模板内容写入磁盘，自定义模板在后续导入其它文件后仍然保留，不会被空文件覆盖。

### 编辑体验

- 开发者工具停靠在 Preview 下方，可拆成独立窗口并重新停靠；关闭面板只隐藏，不销毁 Inspector。
- 编辑器支持拆分为上下两组，可在另一组打开或移动文件，标签可在组间拖动；重启只恢复布局不恢复文件。
- 标签页中间点击关闭，不影响后台标签或空白区域行为。

## 兼容性说明

- Automate 命令、快捷键与插件接口保持不变。
- 翻译目录为 Qt `.ts` 格式，与 Transifex 工作流兼容。
- 上游 `2.8.5` 带来的行为变化以上游发布说明为准；本项目增强功能语义不变。

## 验证范围

- Debug Sigil 应用完整编译、链接及内置 Python 包校验。
- CTest 覆盖版本元数据一致性（`ci_packaging`）、Book Browser 外部文件拖放、Preview DevTools 停靠与拆分编辑器组契约，以及三语翻译完整性。
- 简体中文、繁体中文和日文目录必须保持全部条目已翻译且无 `unfinished`。

本更新说明对应开发版本 `2.8.5E9`。正式安装包、签名和校验和将在发布时按[发布清单](ReleaseChecklist.md)生成。
