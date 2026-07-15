# 中文简繁与地区转换

本文是 Sigil Enhanced 内置中文转换功能的用户说明、维护接口和当前实现边界。英文同步文档见
[`ChineseConversion.en.md`](ChineseConversion.en.md)，完整设计推演见本地
`todo/Sigil-Enhanced-Chinese-Conversion-Design.md`。

## 当前状态

当前版本已提供可用的编辑器和批量转换流程：

- 固定使用 OpenCC 1.3.1，提供 12 个稳定转换方向；
- 支持当前纯文本选区、当前 XHTML/SVG、Book Browser 选中的 XHTML/SVG，以及全书
  XHTML/SVG；
- 提供首选项页面、转换对话框和逐项勾选的差异预览；
- 当前选区和当前文件各形成一个编辑器 undo step；
- 多文件分析完成前不修改资源，确认后先创建一个 Sigil Checkpoint，再逐资源写入；
- XHTML/SVG 使用结构感知的 byte-range patch，不重新序列化整份文件；
- OpenCC 配置、词典和许可证随 macOS、Windows、Linux 包一起部署，不依赖系统 OpenCC、
  `PATH` 或当前工作目录。

尚未开放 NCX、OPF 元数据、CSS `content`、自定义/保护词典、语言元数据更新、JSON 报告、
自动化/插件 API 和字体覆盖检查。它们仍是后续白名单阶段，不能按已完成功能使用。

## 使用方法

1. 打开 `Enhancement > Chinese Conversion...`。
2. 选择转换模式和范围。
3. 按需调整属性白名单与保护选项。
4. 选择 `Preview`，或勾选 `Preview changes before applying` 后选择 `Convert`。
5. 在预览表中逐条保留或取消修改，确认后应用。

当前选区只允许纯文本。选区包含 `<` 或 `&` 时，Sigil 不会直接改源码，而是询问是否转为
“当前文件”的结构化转换。多文件范围始终显示预览，并且只有成功创建 Checkpoint 后才写入。

首选项的 `Chinese Conversion` 页面保存默认模式、默认范围、属性白名单、保护规则和预览策略。
设置使用稳定 key，而不是翻译后的界面文本或组合框序号。

## 转换模式

| key | 方向 | OpenCC 配置 | 目标语言 |
| --- | --- | --- | --- |
| `s2t` | 简体到标准繁体 | `s2t.json` | `zh-Hant` |
| `t2s` | 标准繁体到简体 | `t2s.json` | `zh-CN` |
| `s2tw` | 简体到台湾正体 | `s2tw.json` | `zh-TW` |
| `tw2s` | 台湾正体到简体 | `tw2s.json` | `zh-CN` |
| `s2hk` | 简体到香港繁体 | `s2hk.json` | `zh-HK` |
| `hk2s` | 香港繁体到简体 | `hk2s.json` | `zh-CN` |
| `s2twp` | 简体到台湾正体及用词 | `s2twp.json` | `zh-TW` |
| `tw2sp` | 台湾正体到简体及大陆用词 | `tw2sp.json` | `zh-CN` |
| `t2tw` | 标准繁体到台湾正体 | `t2tw.json` | `zh-TW` |
| `tw2t` | 台湾正体到标准繁体 | `tw2t.json` | `zh-Hant` |
| `t2hk` | 标准繁体到香港繁体 | `t2hk.json` | `zh-HK` |
| `hk2t` | 香港繁体到标准繁体 | `hk2t.json` | `zh-Hant` |

只有 `s2twp` 和 `tw2sp` 明确包含地区用词替换，其余模式主要进行字形和通用词组转换。

## 结构安全边界

`ChineseTextConversionPlan::Build()` 用 Gumbo 识别可转换内容，但所有补丁都定位到原始 UTF-8
buffer，按 byte offset 倒序应用。未修改区域的 XML 声明、换行、缩进、属性顺序和引号风格
保持不变；转换入口显式关闭 Sigil 全文替换接口原有的 NFC 规范化，避免顺带改变其他字符。

XHTML 默认转换 `<body>` 可见文本，以及 `alt`、`title`、`aria-label`、
`aria-description` 白名单属性。链接文字、ruby 和 `<pre>` 默认可转换。

XHTML 默认保护：

- `script`、`style`、`code`、`kbd`、`samp`、`var`；
- 继承 `lang="ja"` 或 `xml:lang="ja-*"` 的内容；
- `id`、`class`、`href`、`src`、`style`、`data-*` 和其他结构属性；
- `<head>`，其内容留给后续显式元数据白名单。

SVG 只转换 `<text>`、`<tspan>`、`<title>`、`<desc>` 的文本和可访问性属性。
`path`、`metadata`、ID、URL、transform 和几何属性不会进入计划。实体按语义转换后安全转义回
XML；注释和 CDATA 当前不转换。

## 提交与冲突规则

当前选区和当前文件直接使用编辑器内存，不先读磁盘。应用前再次比较全文、选区范围和选区
内容；快照不一致时拒绝覆盖。替换使用一个 `QTextCursor` edit block，转换后内容仍保持选中或
恢复接近原位置的光标。

批量范围先同步打开的 tab，再为所有目标创建只读计划和预览。所有勾选结果必须先计算成功，
并再次匹配源快照；随后创建 Checkpoint，再复查一次，最后每个资源只 `SetText()` 一次。
Checkpoint 失败时恢复调用前的 OPF 内容和书籍 modified 状态。批量写入不进入跨文件 undo，
恢复应使用 `Checkpoints`。

## 核心 C++ API

```cpp
const auto profile = ChineseConversionProfile::ForMode(
    ChineseConversionMode::S2TWP);
const QString dataDir = ChineseConversionData::FindDataDirectory();
OpenCCConverter converter(profile, dataDir);

ChineseConversionOptions options;
const ChineseTextConversionPlan plan = ChineseTextConversionPlan::Build(
    xhtml, ChineseDocumentKind::Xhtml, options, converter);

QString error;
const QString converted = plan.Apply(enabledChangeIndexes, &error);
```

重要约定：

- `ChineseConversionProfile` 负责稳定 key、配置文件和语言映射；
- `ChineseConversionSettings` 负责 `QSettings` 往返和非法值安全回退；
- `ChineseConversionData` 只接受同时含配置和核心词典的目录；
- `OpenCCConverter` 初始化或转换失败时保留原输入并返回错误；
- `ChineseTextConversionPlan` 只产生不重叠补丁，支持应用全部或预览中选中的索引；
- 调用方必须在错误非空时终止提交。

## 设置键

设置组为 `chinese_conversion`，主要键包括：

| key | 默认值 |
| --- | --- |
| `mode` | `s2t` |
| `scope` | `current_file` |
| `include_alt_text` | `true` |
| `include_title_attributes` | `true` |
| `include_aria_labels` | `true` |
| `skip_code_elements` | `true` |
| `skip_pre_elements` | `false` |
| `preserve_japanese_text` | `true` |
| `preview_before_apply` | `true` |

未知模式回退到 `s2t`，未知范围回退到 `current_file`。

## 构建、打包与测试

```sh
cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON
cmake --build cmake-build-debug --target Sigil \
  chinese_conversion_core_test chinese_conversion_markup_test -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

`chinese_conversion_core_test` 覆盖 12 个 Profile、数据目录发现、代表性字词转换、缺失资源、
设置往返和非法值回退。`chinese_conversion_markup_test` 覆盖 XHTML/SVG、实体、属性白名单、
日文继承、保护元素、结构字段负向断言和预览部分应用。当前 macOS Debug 完整应用构建通过，
CTest 为 20/20。

运行时数据位置：

- macOS：`Sigil.app/Contents/opencc`；
- Windows：程序/安装包的 `opencc`；
- Linux：构建树可执行文件旁的 `opencc`，安装后为 `<share>/sigil/opencc`。

固定版本、来源、构建裁剪项和许可证说明见
[`3rdparty/opencc/SIGIL_VENDORING.md`](../3rdparty/opencc/SIGIL_VENDORING.md)。

## 已知限制与下一阶段

1. 大型全书分析当前在 GUI 线程执行并显示等待光标，后台取消任务尚未接入。
2. NAV 作为 XHTML 已覆盖；NCX 和 OPF 必须等待独立节点/字段白名单。
3. 尚无保护词 token、自定义覆盖词典和正则排除。
4. 尚无持久化转换报告、逐资源警告视图和地区词汇诊断。
5. 尚未暴露 Automation、Plugin API v2 或 MCP 接口。
6. 尚未实现字体 cmap/子集缺字检查和语言元数据更新。

在这些阶段完成前，禁止对整份 XML、OPF、NCX 或 CSS 源码直接调用 OpenCC。
