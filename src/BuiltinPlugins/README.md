# BuiltinPlugins 内置原生插件模块设计

## 模块定位

`src/BuiltinPlugins` 用于放置 Sigil-Enhanced 自带的“原生插件式”功能。这里的“插件”不是外部 Python 插件，也不是运行时动态加载模块，而是项目内置、随主程序编译、通过稳定接口挂接到菜单/校验/批处理流程中的功能模块。

这样划分的目的:

- 将增强功能从 `MainUI`、`ValidationResultsView` 等 UI 类中拆出来。
- 保留类似插件的独立边界，便于后续禁用、迁移、测试和文档化。
- 复用当前 C++ 资源模型、OPF 解析器、FolderKeeper、UniversalUpdates，而不是照搬外部脚本。
- 将“分析/计划/执行/报告”拆开，避免大范围自动修改时缺少可见结果。

## 当前首个内置插件: EPUB Structure Normalizer

目标是吸收“重构 epub 为规范格式”插件的思路，将 EPUB 目录结构、OPF Manifest、资源链接规范化能力内置到 Sigil-Enhanced。

外部插件的价值点:

- 自动修复 OPF Manifest 中重复 ID/href。
- 删除 Manifest 中指向不存在文件的 href。
- 将未登记的有效资源补入 Manifest。
- 修复 Manifest href 与实际路径大小写不一致。
- 将资源整理到 Sigil 标准目录。
- 检查 XHTML/NCX/CSS 中链接大小写不一致和无效链接。

不照搬的点:

- 不直接读取 `.epub` zip 并生成一个新 EPUB。
- 不用正则整体重写 OPF/XHTML/CSS。
- 不硬编码替换当前 Book 的所有文件。
- 不强行给 XHTML 添加 XHTML 1.1 doctype，这部分继续交给 Sigil 的 Mend/CleanSource。

## 设计原则

1. 显式执行
   - 默认不在打开 EPUB 时静默重构目录。
   - 先通过菜单或现有 `Normalized OPF` 入口执行。
   - 后续可增加打开时检测提示，但不自动修改。

2. 分阶段实现
   - Phase 1: OPF/Manifest 修复服务化。
   - Phase 2: XHTML/NCX/CSS 链接大小写修复和无效链接报告。
   - Phase 3: 新增统一 `Normalize EPUB Structure...` 入口和选项对话框。
   - Phase 4: 打开 EPUB 后给出可规范化提示。

3. 复用当前模型
   - 资源增删改通过 `FolderKeeper`。
   - 标准目录移动通过现有 `BookBrowser` / `OPFModel` / `UniversalUpdates` 路径。
   - OPF 解析继续使用 `OPFParser`。
   - 结果展示继续复用 `ValidationResult`。

4. 可报告
   - 自动修复和需要人工处理的问题都进入结果列表。
   - 结果结构应能被 Validation Results 直接展示。

## 当前实现范围

当前实现 `EpubStructureNormalizer` 已包含 Phase 1 和 Phase 2 的第一版能力；`Normalize EPUB Structure...` 入口已接入 Phase 3 的标准目录归档流程。

Phase 1:

- 检查 OPF package/metadata namespace。
- 删除重复 Manifest ID。
- 删除重复 Manifest href。
- 删除 Manifest 中指向不存在文件的 href。
- 修复 Manifest href 与实际资源路径大小写不一致。
- 将未登记到 Manifest 的有效资源补入 Manifest。
- 检查 cover metadata 无效 idref。
- 检查 spine 无效 idref。
- 保持现有 `Normalized OPF` 菜单行为可用。

Phase 2:

- 扫描 XHTML/HTML 中的 `href`、`src`、`poster`、`data`、`altimg` 属性。
- 扫描 XHTML/HTML 内联 `style` 属性中的 `url(...)`。
- 扫描 CSS 中的 `url(...)` 和 `@import`。
- 扫描 NCX 中的 `src`。
- 跳过外部链接、data/file/http 等带 scheme 的链接、纯 fragment、空链接和带 query 的链接。
- 对能唯一匹配实际资源、但大小写不一致的链接，生成 `旧大小写路径 -> 实际路径` 映射。
- 使用现有 `UniversalUpdates` 执行最终更新，避免直接正则替换源文件。
- 对无法定位的 XHTML/NCX 书内链接生成 warning；CSS 无效链接暂不报告，避免备用 font-face 等场景产生噪音。

新增显式入口:

- Tools 菜单: `Normalize EPUB Structure...`
- 自动化命令: `NormalizeEpubStructure`

标准目录归档:

- 同一个入口会先执行 OPF/Manifest 修复和链接大小写修复，再执行 Sigil 标准目录整理。
- 标准目录整理复用主线已有 `StandardizeEpub()` 相关逻辑，包括 OPF/NCX 改名、重复文件名处理、资源移动和引用更新。
- 最终目标结构为 `OEBPS/content.opf`、`OEBPS/toc.ncx`、`OEBPS/Text`、`OEBPS/Styles`、`OEBPS/Images`、`OEBPS/Fonts`、`OEBPS/Audio`、`OEBPS/Video`、`OEBPS/Misc`。
- 归档完成后会将 Validation Results 中的资源路径同步到移动后的新路径，避免结果列表双击定位到旧路径。

相比旧实现的改进:

- OPF 修复逻辑从 `ValidationResultsView` 迁到内置插件模块。
- 核心实现不依赖 UI 类。
- 返回结构化结果，调用方决定是否刷新 Book Browser。
- 仅在确实修改 Manifest/Spine 时改写 OPF，避免只有 warning 时无意义重写 OPF。
- 链接大小写修正只负责发现映射，实际写回复用现有跨资源更新系统。

## 后续接口

当前接口:

```cpp
struct Options {
    bool repairOpfManifest = true;
    bool repairLinkCase = true;
    bool dryRun = false;
};
```

当前代码中的 `Options` 先实现了:

- `repairOpfManifest`
- `repairLinkCase`
- `dryRun`

后续建议继续扩展:

- `useStandardFileExtensions`
- `rebaseManifestIds`

`repairLinkCase` 当前策略:

- 建立 `lowerBookPath -> actualBookPath` 索引。
- 扫描 XHTML 的 `href`、`src`、`url(...)`。
- 扫描 CSS 的 `@import`、`url(...)`。
- 扫描 NCX 的 `content src`。
- 跳过 `http:`、`https:`、`mailto:`、`data:`、`file:`、`res:`、纯 fragment 等非书内链接。
- 只在大小写可唯一纠正时自动修改。
- 找不到目标时仅报告，不误改。
- CSS 无效链接默认弱提示，避免备用 font-face 等场景产生过多噪音。

后续优化点:

- 用 Gumbo/现有 parser 暴露结构化链接 walker，替换第一版 HTML 属性正则扫描，减少代码示例、脚本字符串中的误报。
- 为 CSS 增加更完整的 token 级扫描，保留注释、引号、转义边界。
- 增加 dry-run 预览对话框，让用户确认删除/补齐 manifest 项和链接大小写修正。
- 增加标准目录归档的 dry-run 结果预览，包括将要移动的资源列表和可能失败的路径。

## 测试重点

Phase 1 需要覆盖:

- Manifest 重复 ID。
- Manifest 重复 href。
- 重复项中 spine/cover 引用项优先保留。
- Manifest href 指向不存在文件时删除。
- Manifest href 大小写与实际资源路径不一致时修复。
- 未登记 Manifest 的图片/CSS/HTML/字体等资源被补入。
- XML/OPF 文件不被错误补入。
- cover metadata 无效 idref 只报告。
- spine 无效 idref 只报告。
- `Normalized OPF` 执行后 Book Browser 在新增资源时刷新。

Phase 2 需要覆盖:

- XHTML、NCX、CSS 链接大小写自动纠正。
- fragment 保留。
- 外部链接不处理。
- 大小写冲突不自动修复。
- 找不到目标的链接只报告。

Phase 3 需要覆盖:

- 执行 `Normalize EPUB Structure...` 后 OPF 归档到 `OEBPS/content.opf`。
- 存在 NCX 时 NCX 归档到 `OEBPS/toc.ncx`。
- XHTML、CSS、图片、字体、音频、视频、Misc 资源移动到对应标准目录。
- 移动后 XHTML/CSS/NCX/OPF 中的相对链接仍然正确。
- 原本已经是标准目录的 EPUB 执行后不产生多余修改。
- 非良构 XHTML/OPF/NCX 会中止归档并给出提示。
