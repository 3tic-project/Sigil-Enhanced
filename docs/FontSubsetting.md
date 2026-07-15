# HarfBuzz 字体子集化

本文说明 Sigil-Enhanced 内置字体子集化工具的使用方法、实现边界、C++ 接口、JSON 报告和
维护验收流程。功能依据
`todo/Sigil-Enhanced_HarfBuzz_Font_Subsetting_Design_zh-CN.md` 的安全优先方案实现。

## 当前状态

当前版本提供首个可用的 TTF/OTF 原位子集化流程：

- 字体子集化始终编译，固定静态内置 HarfBuzz 14.2.1，不探测或链接系统 HarfBuzz；
- 从编辑器内存同步当前书籍，再扫描 XHTML、SVG、OPF、NCX 等 XML 文本及 CSS
  `content` 字符串；
- 在后台线程中完成字符收集、字体检查、HarfBuzz 子集化和输出验证；
- 显示每个字体的格式、许可、原始/输出体积、字形数和处理状态；
- 可预选 Book Browser 中选中的字体，也可在结果表中逐项勾选；
- 支持保留或删除 hinting，并可导出稳定字段的 JSON 分析报告；
- 应用前创建 Sigil Checkpoint，随后进行输入复查和批量事务写入；
- 保持资源 ID、路径、扩展名、media-type 和字体混淆算法不变；
- 完整提供简体中文、繁体中文和日文界面翻译。

当前不会删除未使用字体，不会更改 CSS `@font-face`，不转换字体格式，也不绕过字体许可。
WOFF/WOFF2、TTC/OTC、SVG 字形、EBDT/EBLC、Graphite 和 AAT 布局不属于当前可强制处理
范围。

## 使用方法

1. 打开包含内嵌字体的 EPUB。
2. 可选：在 Book Browser 中选择一个或多个字体，以此作为结果表的初始勾选范围。
3. 选择 `Tools > Fonts > Subset Embedded Fonts...`。
4. 等待分析完成，检查许可、体积、字形数和状态列。
5. 按需启用 `Remove font hinting`，然后重新选择 `Analyze`。
6. 按需选择 `Save Report...` 导出 JSON 报告。
7. 勾选状态为 `Ready` 的字体，选择 `Apply Subsets`。

只有验证成功且输出确实小于源字体的行可以应用。更改 hinting 选项会使旧结果失效，必须重新
分析。分析期间不能关闭对话框，避免后台任务与书籍生命周期脱节。

应用会创建一个恢复用 Checkpoint。发生冲突、写入失败或回滚失败时会停止操作并显示原因。
多文件批量修改不进入编辑器文本 undo 栈，恢复整个批次应使用 `Checkpoints`。

## 字符收集规则

当前采用全书安全字符集，而不是根据 CSS 级联把字符精确分配给某个 font face。这会保留较多
字形，但不容易因错误推断字体归属而删除必要字符。

收集器规则如下：

- 使用 `QXmlStreamReader` 解析 XML 类型资源，不通过正则解析标记；
- 通过 Sigil 的 XHTML 实体表解析命名实体，并继续扫描实体后的文本；
- 收集文本节点，以及 `alt`、`title`、`aria-label`、`label` 属性；
- 跳过 `script` 内容；
- 将 `style` 内容作为 CSS 扫描；
- 收集 CSS `content:` 中的单/双引号字符串，并解析 CSS Unicode 转义；
- 无法静态解析 `attr()`、counter 等动态 `content` 时记录警告；
- 正确处理 Unicode 辅助平面字符；
- 始终加入 U+0020、U+00A0 和 U+3000 三种常用空格；
- 最多保留 64 个、每个不超过 256 字符的 shaping 验证样本。

全书字符集会与每个源字体的 cmap 取交集。源字体本身没有的字符计入
`unavailableCodepoints`，不会错误地判定为子集化输出缺字；请求保留但输出缺失的字符计入
`missingCodepoints`，并使该字体处理失败。

## 支持与跳过矩阵

| 条件 | 当前行为 | 原因 |
| --- | --- | --- |
| sfnt TrueType (`glyf`) | 支持 | HarfBuzz 正常子集化和闭包 |
| sfnt OpenType (`CFF`/`CFF2`) | 支持 | 保持原容器格式 |
| TTC/OTC 字体集合 | 跳过 | 首版不拆分 face 或重写 OPF |
| WOFF/WOFF2 | 跳过 | 首版不转换容器或 media-type |
| SVG 字形表 | 跳过 | HarfBuzz Subset 不支持该表 |
| EBDT/EBLC | 跳过 | HarfBuzz Subset 不支持该位图组合 |
| Graphite/AAT 布局表 | 跳过 | 不能保证布局语义等价 |
| COLR/CBDT/CBLC/sbix | 允许并警告 | 仍执行覆盖与 shaping 验证 |
| 可变字体 | 允许并警告 | 保留轴；支持时优化 IUP delta |
| 缺少轮廓、空字体或无效字体 | 跳过 | 无法产生可验证输出 |

### 字体许可

工具读取 OpenType `OS/2.fsType`，不提供绕过开关：

| fsType 分类 | 当前行为 |
| --- | --- |
| Installable | 允许 |
| Editable | 允许 |
| Preview and Print | 跳过 |
| Restricted | 跳过 |
| No Subsetting | 跳过 |
| Bitmap Embedding Only | 跳过 |
| 缺失、保留位异常或组合无效 | 跳过 |

该判断是保守的工程保护，不构成法律意见。字体授权合同可能比 `fsType` 更严格，使用者仍需确认
实际许可证。

## 验证与提交

每个字体只有通过以下检查才会进入可选状态：

1. HarfBuzz 可以创建 input 和 subset plan；
2. plan 可以执行并序列化出非空字体；
3. 输出可按源字体的容器格式重新解析；
4. 输出覆盖全部请求码点；
5. 对适用的全书样本进行 shaping，映射后的 GID、cluster、advance 和 offset 等价；
6. 输出体积小于输入体积。

提交阶段使用 `FontSubsetTransaction`：

- 只接受现有常规文件，拒绝符号链接、空输出、重复路径和无变化替换；
- stage 和 commit 时各比较一次完整源字节，防止分析后的外部修改被覆盖；
- 单文件使用 `QSaveFile` 原子替换并保留文件权限；
- 任一写入失败时按逆序恢复此前已写入文件；
- 提交期间暂停 `FolderKeeper` 文件监视，成功后统一刷新 FontResource、打开的字体预览和
  Book Browser；
- 明确设置 Book modified 状态，并保留 FontResource 的混淆算法字段。

`QSaveFile` 保证单个字体不会出现半写文件。跨多个字体的事务能处理常规 I/O 失败，但进程被
强制终止或系统掉电不可能由内存回滚完全覆盖，因此应用前的 Checkpoint 是最终恢复边界。

## JSON 报告

`Save Report...` 输出 UTF-8 JSON，当前 `schemaVersion` 为 `1`。枚举使用稳定英文 key，
不随界面语言变化；错误和警告使用当前界面语言。

顶层字段：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `schemaVersion` | integer | 报告结构版本 |
| `codepointCount` | integer | 全书安全字符集大小 |
| `shapingSampleCount` | integer | shaping 样本数 |
| `dropHinting` | boolean | 本次分析是否删除 hinting |
| `warnings` | string[] | 扫描和快照警告 |
| `fonts` | object[] | 逐字体结果 |

逐字体对象包含：

- 身份：`identifier`、`path`、`mediaType`、`obfuscated`；
- 结果：`success`、`error`、`warnings`、`risks`；
- 检查：`format`、`license`、`harfbuzzVersion`；
- 体积：`oldSize`、`newSize`；
- 字形：`oldGlyphCount`、`newGlyphCount`、`mappedGlyphCount`；
- 字符统计：`inputCodepointCount`、`requestedCodepointCount`、
  `unavailableCodepointCount`、`missingCodepointCount`；
- 详细字符：`requestedCodepoints`、`unavailableCodepoints`、`missingCodepoints`，使用
  `U+XXXX` 字符串并按数值排序。

稳定 `format` 值包括 `sfnt-truetype`、`sfnt-cff`、`collection`、`woff`、`woff2`、
`unknown`。稳定 `license` 值包括 `installable`、`editable`、`preview-print`、
`restricted`、`no-subsetting`、`bitmap-only`、`invalid-or-missing`。

## C++ API

源码位于 `src/BookManipulation/FontSubset/`。

### 纯值对象和分析入口

```cpp
FontSubset::BookSnapshot snapshot;
snapshot.textSources.append({bookPath, mediaType, text});
snapshot.fonts.append({identifier, bookPath, fullPath, mediaType,
                       obfuscationAlgorithm, fontBytes});

FontSubset::Options options;
options.dropHinting = false;
options.keepNotdefOutline = true;
options.validateShaping = true;

const FontSubset::BatchResult batch =
    FontSubset::FontSubsetController::Analyze(snapshot, options);
```

`BookSnapshot`、`FontSnapshot`、`BatchResult` 和 `Result` 都是值对象，可在线程间复制。
worker 不得访问 `Book`、`Resource` 或其他 QObject。

### 核心类职责

| 类 | 职责 |
| --- | --- |
| `GlobalFontUsageCollector` | 从结构化文本和 CSS 收集字符及 shaping 样本 |
| `FontInspector` | 检查容器、face、表、轮廓、风险和 `fsType` |
| `HarfBuzzSubsetEngine` | 创建 plan、执行 subset、序列化并验证输出 |
| `FontSubsetTransaction` | stage、冲突检查、原子写入和逆序回滚 |
| `FontSubsetController` | UI 线程快照、纯分析调度和 Book 提交 |
| `FontSubsetDialog` | 异步 dry-run、选择、选项和 JSON 报告 |

`FontSubsetController::CreateSnapshot(Book*)` 和 `Commit(Book*, ...)` 必须在 UI 线程调用；
`Analyze(...)` 只读取不可变快照，可在 `QtConcurrent` 中运行。

### 单字体入口

```cpp
FontSubset::HarfBuzzSubsetEngine engine;
FontSubset::Result result = engine.Subset(fontBytes, codepoints, options);
if (!result.success) {
    // result.error、inspection.blockingReason 和 warnings 可用于报告。
}
```

调用方不得只检查 `outputBytes`；必须以 `success` 为准。`newSize >= oldSize` 虽可能仍为成功
分析结果，但 UI 和控制器不会提交这种结果。

## 构建和依赖

字体子集化是必需功能，没有关闭选项，也不受 `USE_SYSTEM_LIBS` 影响。标准构建即可包含该功能：

```sh
cmake -S . -B build
cmake --build build --parallel
```

仓库在 `3rdparty/harfbuzz/` 固定 HarfBuzz 14.2.1。源码来自官方
`harfbuzz-14.2.1.tar.xz`，SHA-256 为
`a54a5d8e9380a41fbb762ce367bcbf7704792dfca0d93f1bbca86c5a57902e0e`；许可证、来源和
裁剪范围见 `3rdparty/harfbuzz/README.sigil.md`。

`3rdparty/cmake/harfbuzz.cmake` 调用上游 CMake 工程，只构建静态 `harfbuzz` 和
`harfbuzz-subset`。包装层关闭 CoreText、DirectWrite、FreeType、GLib、ICU、Graphite、Cairo、
raster、vector、GPU、工具程序和共享库，并在配置时断言两个目标均为静态库。主程序与测试只
链接 `Sigil::HarfBuzzSubset`，不查找 pkg-config，不链接 Qt 私有 HarfBuzz，也不使用 Python、
外部命令或本地模型执行子集化。

升级 HarfBuzz 时必须一起完成：

1. 从官方 release 下载源码并独立校验 SHA-256；
2. 替换未修改的 `src` 和上游 CMake 必需文件，保留 `COPYING`；
3. 同步包装层版本、`README.sigil.md`、本文和 smoke test 的精确版本断言；
4. 在 macOS、Windows、Linux 重新执行完整构建、字体测试和动态依赖检查。

官方 API 参考：

- [HarfBuzz Subset API](https://harfbuzz.github.io/harfbuzz-hb-subset.html)
- [HarfBuzz source and license](https://github.com/harfbuzz/harfbuzz)

## 测试和验收

测试字体位于 `tests/fixtures/fonts/OpenSans-Test.ttf`，来源和 OFL 许可见同目录 README。

```sh
ctest --test-dir build --output-on-failure -R '^font_subset_'
ctest --test-dir build --output-on-failure \
  -R '^(zh_cn|zh_tw|ja)_translation_coverage$'
```

测试分层：

| 测试 | 覆盖 |
| --- | --- |
| `font_subset_smoke` | 精确内置版本断言及真实 blob/face/input/plan/execute/serialize |
| `font_subset_core` | 格式、许可、风险、覆盖、实际子集和 shaping |
| `font_subset_workflow` | XML/CSS 字符收集、冲突、提交和故障回滚 |
| `font_subset_batch` | 快照到批量结果的端到端纯分析管线 |
| 三语 coverage | 当前所有 Qt 可见文本、占位符和复数形式 |

发布前还应完成：

1. 在无系统 HarfBuzz、清空 pkg-config 搜索路径的新目录中完成配置和构建；
2. 主程序完整编译链接，`font_subset_smoke` 报告精确的固定版本；
3. 使用 `otool -L`、`ldd` 或 `dumpbin /DEPENDENTS` 确认产物没有 HarfBuzz 动态依赖；
4. `git diff --check` 和全部自动测试通过；
5. 使用含 TTF、OTF、混淆字体和受限字体的人工 EPUB 检查对话框、报告和 Checkpoint 恢复；
6. 至少在一个 macOS、Windows 和 Linux 打包环境确认内置静态库构建成功。

## 安全审计结论

当前实现不接收任意字体输出路径；原位替换路径只来自当前 Book 的 FontResource。唯一可由用户
选择的路径是只读分析报告的保存位置。核心不启动子进程、不执行字体内容、不调用插件或 Python。

已落实的防护包括：许可拒绝、高风险表拒绝、QObject/worker 边界、完整字节冲突检查、符号链接
拒绝、输出重解析、码点覆盖、shaping 抽样、尺寸门槛、原子单文件写入、批量回滚和 Checkpoint。

剩余风险：

- 内置 HarfBuzz 仍属于解析不可信字体的攻击面，应及时跟随上游安全更新并重跑回归测试；
- 全书快照和输入/输出字体同时驻留内存，大型 CJK 字体较多时内存峰值较高；
- shaping 是代表性样本验证，不是所有 OpenType feature、语言和阅读器的穷举证明；
- 跨文件事务不能抵御进程被强制终止或系统掉电，必须保留 Checkpoint 恢复路径；
- 许可判断不能替代人工阅读字体许可证。

## 后续范围

以下仍未实现，不能按现有能力调用：

- CSS-aware 的逐 `@font-face` 字符归属；
- 未使用字体检测、删除以及 CSS/OPF 清理；
- WOFF/WOFF2 输出、TTC/OTC face 拆分和格式转换；
- 可变字体轴固定或静态实例化；
- Graphite/AAT 的保守 passthrough 模式；
- 同一 face 的 preprocess 缓存和多字体并行限流；
- Automation、Agent 或插件公开接口；
- 持久化跨进程事务日志。

扩展这些能力时必须保持当前安全默认，不得把许可绕过或关闭布局闭包作为普通用户选项。
