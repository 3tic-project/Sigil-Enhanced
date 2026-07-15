# 中文简繁与地区转换

本文记录 Sigil Enhanced 内置中文转换功能的已实现行为。设计推演见本地
`todo/Sigil-Enhanced-Chinese-Conversion-Design.md`；本文件和测试结果是当前实现依据。

## 实现状态

当前已完成转换核心和 XHTML/SVG 安全转换计划，编辑器和设置界面尚未接入。

- 固定使用 OpenCC 1.3.1，不依赖系统安装或当前工作目录。
- OpenCC 以静态库构建，未包含 Python、Node、测试、benchmark 或实验性 Jieba 插件。
- 仓库固定携带由 1.3.1 官方数据生成的 `.ocd2` 词典，构建时汇总 12 个稳定 JSON 配置和
  Apache-2.0 许可证，不需要 Python 或词典生成工具。
- 业务代码通过 `ChineseConversionProfile` 与 `OpenCCConverter` 使用 OpenCC，不直接暴露
  OpenCC 类型。

## 转换模式

| key | 方向 | OpenCC 配置 | 目标语言标记 |
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

转换 key 用于设置持久化，不能改为依赖翻译文本的值。

## 核心 API

```cpp
const auto profile = ChineseConversionProfile::ForMode(
    ChineseConversionMode::S2TWP);
OpenCCConverter converter(profile, openccDataDirectory);

QString error;
const QString output = converter.Convert(input, &error);
```

构造时会验证配置并加载词典。`IsValid()` 为 `false` 时，`ErrorString()` 提供初始化错误。
`Convert()` 失败时保留原输入并通过 `error` 返回原因，调用方不得在错误非空时提交结果。

## XHTML 与 SVG 安全边界

`ChineseTextConversionPlan::Build()` 使用 Gumbo 解析结构，但不重新序列化文档。计划记录
原始 UTF-8 buffer 中的 byte range，`Apply()` 按倒序应用不重叠 patch，因此未修改区域的
缩进、换行、XML 声明、属性顺序和引号风格保持不变。

XHTML 默认转换：

- `<body>` 内的连续文本节点；
- `alt`、`title`、`aria-label` 和 `aria-description` 属性；
- 链接、ruby 与默认未排除的 `<pre>` 中的可见文字。

XHTML 默认保护：

- `script`、`style`、`code`、`kbd`、`samp` 和 `var` 的内容；
- 继承 `lang="ja"` 或 `xml:lang="ja-*"` 的内容；
- `id`、`class`、`href`、`src`、`style`、`data-*` 及其他非白名单属性；
- `<head>` 内容，后续通过显式 metadata 选项处理。

SVG 只转换 `<text>`、`<tspan>`、`<title>`、`<desc>` 的文本和可访问性属性；`path`、
`metadata`、ID、URL、transform 和几何属性不会进入转换计划。文本中的实体会先按语义转换，
再安全转义回 XML；注释和 CDATA 当前不转换。

## 构建与验证

```sh
cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON
cmake --build cmake-build-debug --target chinese_conversion_core_test chinese_conversion_markup_test -j4
ctest --test-dir cmake-build-debug -R '^chinese_conversion_(core|markup)$' --output-on-failure
```

`chinese_conversion_core_test` 覆盖 12 个 Profile 的唯一性和资源存在性、简繁基础转换、
台湾地区词汇、香港字形及缺失资源错误路径。`chinese_conversion_markup_test` 覆盖 XHTML、
SVG、实体、属性白名单、日文继承、保护元素和结构字段负向断言。
当前 macOS Debug 完整构建通过，CTest 结果为 20/20。

## 后续阶段

1. 主对话框、设置持久化、当前选区和当前文件单步撤销。
2. 多文件预览、单 Checkpoint 提交、NAV/NCX 与显式 OPF 元数据转换。
3. 保护词、自定义词典、报告、自动化和字体覆盖联动。

在结构化收集器完成前，不允许对整份 XML 源码直接调用 OpenCC。
