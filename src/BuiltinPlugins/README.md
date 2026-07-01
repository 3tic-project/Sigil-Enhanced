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

`todo/vendor/EPUB-Checker` 和 `todo/vendor/epubcheck` 的参考价值:

- `EPUB-Checker` 是 W3C EPUBCheck 的桌面包装器，主要提供打包、GUI 和本地化思路；本项目不嵌入它的 Java GUI。
- `epubcheck` 是官方规范验证器，适合借鉴其 URL 解析、OPF Manifest、spine、nav、guide、media-type 等结构性规则。
- 当前实现只吸收可用 C++/Sigil 模型稳定完成的规则，不移植 Java 验证引擎、schema、RelaxNG、Schematron 或完整报错体系。

许可证边界:

- `EPUB-Checker` 的 GUI/wrapper 代码使用 GPL-2.0-only；Sigil-Enhanced 继承 Sigil 的 GPL-3.0-or-later，不能复制或改写合并 GPL-2.0-only 代码。
- `todo/vendor/EPUB-Checker` 只作为本地行为参考，不作为构建输入、不复制源码、不移植实现。
- 如需参考 W3C `epubcheck` 代码，必须确认对应文件许可证并保留必要 attribution；当前策略仍是按规则语义做 clean-room C++ 实现。
- 文档可以记录检查类别和规范行为，但代码必须基于 Qt/Sigil 自有模型重新实现。

不照搬的点:

- 不直接读取 `.epub` zip 并生成一个新 EPUB。
- 不用正则整体重写 OPF/XHTML/CSS。
- 不硬编码替换当前 Book 的所有文件。
- 不强行给 XHTML 添加 XHTML 1.1 doctype，这部分继续交给 Sigil 的 Mend/CleanSource。

## 设计原则

1. 显式执行
   - 默认不在打开 EPUB 时静默重构目录。
   - 先通过 `Enhancement` 菜单或现有 `Normalized OPF` 入口执行。
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
   - 所有自动改动必须有对应 Message/Validation Result 记录，不能静默修改。
   - dry-run 结果使用“需要/将会”措辞；实际执行后的结果使用“已”措辞，避免用户误判当前书籍状态。

## 当前实现范围

当前实现 `EpubStructureNormalizer` 已包含 Phase 1 和 Phase 2 的第一版能力；`Normalize EPUB Structure...` 入口已接入 Phase 3 的标准目录归档流程，并开始吸收 EPUBCheck 的资源级诊断。

Phase 1:

- 检查 OPF package/metadata namespace。
- 删除重复 Manifest ID。
- 删除重复 Manifest href。
- 删除 Manifest 中指向不存在文件的 href。
- 修复 Manifest href 与实际资源路径大小写不一致。
- 读取 Manifest href 时先按 URL/URI 解码，写回时保持 URL 编码。
- 不再将远程 Manifest href 当成本地缺失文件删除；对 `file:`、`data:` 等高风险 URL 给出提示。
- EPUB3 远程 Manifest 资源只提示合法性风险，不自动删除；当前按 EPUBCheck 思路允许音频、视频、字体等常见远程资源类型。
- 发现 Manifest href 含 query 或 fragment 时按资源路径规范化。
- 删除错误登记 OPF package document 自身的 Manifest 项。
- 根据文件扩展名/XML sniff 修正明显不一致的 Manifest media-type。
- 将未登记到 Manifest 的有效资源补入 Manifest。
- EPUB3 中缺少唯一 `nav` 标记时，能唯一识别 `nav.xhtml`/`nav.html`/`id=nav` 时自动补 `properties="nav"`。
- EPUB3 中封面图片由旧 `meta name="cover"` 指向但缺少 `cover-image` 属性时自动补齐。
- 检查 `nav`、`cover-image` 属性与 media-type 是否匹配。
- 检查 cover metadata 无效 idref。
- 检查 spine 无效 idref。
- 检查 spine 项是否为可阅读内容文档；若不是，则检查 Manifest fallback 链是否能回落到内容文档。
- 检查 Manifest fallback / fallback-style 属性是否为空、是否指向不存在 ID、fallback 链是否形成循环。
- 检查 fallback-style 是否指向 CSS 资源，并提示 EPUB3 中继续使用 fallback-style 的兼容性风险。
- 检查 spine 是否引用远程 Manifest 资源、是否至少有一个 linear 项，以及 EPUB2 spine 重复 idref。
- 检查 guide 引用是否能对应到 Manifest 内容项。
- OPF package/metadata/manifest/spine/guide/cover 检查结果尽量写入具体节点或属性的行号和字符 offset。
- 保持现有 `Normalized OPF` 菜单行为可用。

Phase 2:

- 扫描 XHTML/HTML 中的 `href`、`src`、`poster`、`data`、`altimg` 属性。
- 使用轻量 CSS token 扫描 XHTML/HTML 内联 `style` 属性中的 `url(...)`。
- 使用轻量 CSS token 扫描 CSS 中的 `url(...)` 和 `@import`，跳过块注释和普通字符串中的伪链接。
- 扫描 NCX 中的 `src`。
- 跳过外部链接、data/http 等带 scheme 的链接和空链接；对 `file:` URL、带 query 的书内相对链接、根路径、逃出 EPUB 根目录的相对路径生成 warning。
- 对未编码空格、反斜杠和非严格合法 URL 生成 warning。
- 对纯 fragment 和跨文件 fragment 执行基础 id 存在性检查。
- 检查 XHTML/XML/SVG/NCX 中重复 `id` / `xml:id`。
- XHTML/HTML、CSS、NCX 的链接检查结果会写入行号和字符 offset，Validation Results 双击可跳转到对应位置。
- 对能唯一匹配实际资源、但大小写不一致的链接，生成 `旧大小写路径 -> 实际路径` 映射。
- 使用现有 `UniversalUpdates` 执行最终更新，避免直接正则替换源文件。
- 对无法定位的 XHTML/NCX 书内链接生成 warning；CSS 无效链接暂不报告，避免备用 font-face 等场景产生噪音。

资源诊断:

- 检查 `META-INF/container.xml` 是否存在、可解析、rootfile 是否指向当前 OPF；可安全重建时按当前 OPF 路径重写。
- 检查已存在的 `mimetype` 文件内容是否精确为 `application/epub+zip`。
- 确认 Sigil 导出的 EPUB ZIP 外壳保持 OCF 基本要求: `mimetype` 是 ZIP 第一项，内容精确为 `application/epub+zip`，不压缩，无 local/global extra field。
- 导出普通文件时只在未压缩大小达到 ZIP32 边界时启用 ZIP64，避免小文件无意义写入 ZIP64 结构。
- 检查 Manifest 是否错误登记 `mimetype` 或 `META-INF` 内文件。
- 检查文件路径空白字符、尾随点、隐藏文件风格、非 ASCII 路径、Unicode 规范化和大小写折叠冲突。
- 检查 Publication Resource 是否错误放在 `META-INF` 中。
- 检查 XML/XHTML/SVG/NCX/OPF 的 XML 声明 encoding，非 UTF-8 时给 warning。
- 检查外部实体声明，提示 EPUB3 和安全风险。
- 检查 XHTML/NCX/OPF/SVG/XML 的 DOCTYPE 风险与 EPUB2/EPUB3 常见要求。
- 使用 Qt XML 流式解析器做基础 well-formed 检查，解析错误写入行号和字符 offset。
- 检查 CSS `@charset` 是否在首条规则、是否重复、是否声明为非 UTF-8。
- 检查 CSS 未闭合块注释、未闭合字符串和花括号不平衡。
- 检查图片资源是否存在、是否 0 字节。
- 检查 PNG/JPEG/GIF 文件头是否与扩展名或 media-type 匹配。
- 检查图片是否能读取尺寸。
- 对超过 4 MB 或大于等于 `3840x2160` 的图片生成 warning。
- 检查字体资源是否存在、是否 0 字节，以及 TrueType/OpenType/TTC/WOFF/WOFF2 文件头是否与扩展名匹配。
- 检查音频资源是否存在、是否 0 字节，以及 MP3/Ogg/M4A/MP4/AAC 文件头是否与扩展名匹配。
- 检查视频资源是否存在、是否 0 字节，以及 MP4/M4V/MOV/Ogg/WebM/VTT 文件头是否与扩展名匹配。
- 检查 PDF 资源是否存在、是否 0 字节，以及文件头是否为 `%PDF-`。

新增显式入口:

- Enhancement 菜单: `Normalize EPUB Structure...`
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
- 自动修复的结果消息覆盖 Manifest、spine、container.xml、链接大小写和标准目录归档等改动，避免用户看不见插件改了什么。
- 仅在确实修改 Manifest/Spine 时改写 OPF，避免只有 warning 时无意义重写 OPF。
- OPF 结构检查会旁路建立源文本定位索引，用于 Validation Results 跳转；不改变 OPFParser 的解析/写回语义。
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
- 建立 XHTML/XML/SVG/NCX 的 `id` 索引，用于 fragment 检查。
- 扫描 XHTML 的 `href`、`src`、内联样式 `url(...)`。
- 用轻量 CSS token scanner 扫描 CSS 的 `@import`、`url(...)`，避免注释和字符串中的误报。
- 扫描 NCX 的 `content src`。
- 跳过 `http:`、`https:`、`mailto:`、`data:`、`res:` 等非书内链接。
- 对 `file:`、query、根路径、逃出 EPUB 根目录、严格 URL 风险和 fragment 缺失只报告，不自动修改。
- 只在大小写可唯一纠正时自动修改。
- 找不到目标时仅报告，不误改。
- CSS 无效链接默认弱提示，避免备用 font-face 等场景产生过多噪音。

后续优化点:

- 用 Gumbo/现有 parser 暴露结构化链接 walker，替换第一版 HTML 属性正则扫描，减少代码示例、脚本字符串中的误报。
- CSS token scanner 后续可继续扩展到更多语法级诊断；当前已用于链接扫描，保留注释、引号、转义边界。
- 增加 dry-run 预览对话框，让用户确认删除/补齐 manifest 项和链接大小写修正。
- 增加标准目录归档的 dry-run 结果预览，包括将要移动的资源列表和可能失败的路径。

## 测试重点

Phase 1 需要覆盖:

- Manifest 重复 ID。
- Manifest 重复 href。
- 重复项中 spine/cover 引用项优先保留。
- Manifest 远程 href 不被误删。
- Manifest `file:`、`data:` href 只报告。
- EPUB3 远程 Manifest 资源 media-type 合法性提示。
- Manifest href 中 `%20`、中文百分号编码、query、fragment 被正确处理。
- Manifest 自引用 OPF package document 时被删除。
- Manifest media-type 与扩展名不一致时被修正。
- Manifest href 指向不存在文件时删除。
- Manifest href 大小写与实际资源路径不一致时修复。
- 未登记 Manifest 的图片/CSS/HTML/字体等资源被补入。
- XML/OPF 文件不被错误补入。
- EPUB3 nav 缺失、重复、media-type 错误。
- EPUB3 cover-image 缺失或声明在非图片资源上。
- cover metadata 无效 idref 只报告。
- spine 无效 idref 只报告。
- spine 全部为 `linear="no"`、spine 指向非内容文档、spine 非内容文档但有/没有合法 fallback 链、EPUB2 重复 spine idref。
- manifest fallback 指向不存在 ID、fallback 循环、fallback-style 指向非 CSS 资源。
- spine 引用远程 Manifest 资源只报告、不自动改。
- guide 指向未登记资源或非内容文档。
- package/metadata namespace、manifest item、spine itemref、guide reference、cover meta 的结果显示 line/offset 并可双击跳转。
- `Normalized OPF` 执行后 Book Browser 在新增资源时刷新。

Phase 2 需要覆盖:

- XHTML、NCX、CSS 链接大小写自动纠正。
- fragment 保留。
- 纯 fragment 指向当前文件 id 时不报错，指向不存在 id 时可跳转到对应位置。
- 跨文件 fragment 指向目标文件 id 时不报错，目标 id 不存在时可跳转到来源链接位置。
- 重复 `id` / `xml:id` 能在 Validation Results 中跳转到重复处。
- 外部链接不处理。
- `file:` 链接和带 query 的书内相对链接只报告、不自动改。
- 未编码空格、反斜杠、根路径和逃出 EPUB 根目录的链接只报告、不自动改。
- Validation Results 中 XHTML/CSS/NCX 链接问题显示 line 和 offset，双击可跳转。
- CSS 注释或普通字符串中放置 `url(...)`，确认不会触发链接检查。
- 大小写冲突不自动修复。
- 找不到目标的链接只报告。

资源诊断需要覆盖:

- XML declaration 中非 UTF-8 encoding 只报告、不自动改。
- 外部实体声明只报告、不自动改。
- EPUB2 XHTML 缺失或非 XHTML 1.1 DOCTYPE 只报告。
- EPUB3 XHTML 旧式外部 DOCTYPE 只报告。
- EPUB2 NCX 缺失或非 NCX 2005-1 DOCTYPE 只报告。
- OPF 中出现 DOCTYPE 只报告。
- XML well-formed 错误进入 Validation Results，并尽量定位到 line/offset。
- CSS `@charset` 位置、重复、非 UTF-8 和轻量语法外壳问题只报告。
- 图片文件缺失或 0 字节只报告。
- PNG/JPEG/GIF 文件头与扩展名或 media-type 不一致只报告。
- 损坏图片无法读取尺寸只报告。
- 大文件和大尺寸图片只报告。
- 字体、音频、视频、PDF 文件缺失或 0 字节只报告。
- 字体、音频、视频、PDF 文件头与扩展名不一致只报告。
- 导出 EPUB 后确认 ZIP 第一项为未压缩 `mimetype`，且普通资源按需使用 ZIP64。

Phase 3 需要覆盖:

- 执行 `Normalize EPUB Structure...` 后 OPF 归档到 `OEBPS/content.opf`。
- 存在 NCX 时 NCX 归档到 `OEBPS/toc.ncx`。
- XHTML、CSS、图片、字体、音频、视频、Misc 资源移动到对应标准目录。
- 移动后 XHTML/CSS/NCX/OPF 中的相对链接仍然正确。
- 原本已经是标准目录的 EPUB 执行后不产生多余修改。
- 非良构 XHTML/OPF/NCX 会中止归档并给出提示。
