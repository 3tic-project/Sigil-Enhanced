# Sigil 翻译维护指南

Sigil 的用户界面支持多语言显示。翻译文件位于
`src/Resource_Files/ts`，文件名采用 `sigil_语言代码.ts` 格式，例如简体
中文为 `sigil_zh_CN.ts`，繁体中文为 `sigil_zh_TW.ts`。

## 更新翻译目录

应使用与构建 Sigil 相同版本的 Qt 工具。修改任何用户可见文本后，在仓库
根目录运行：

```sh
lupdate src -recursive -extensions cpp,h,ui \
    -ts src/Resource_Files/ts/sigil_zh_CN.ts
```

逐项翻译新增的有效条目，不要仅删除 `unfinished` 标记。译文必须保留源文
中的 `%1`、`%2`、`%n` 等占位符以及 HTML 富文本标签结构。

产品名、文件格式、标准、API、键盘快捷键和代码标识符可以保留原文，例如
`Sigil`、`XHTML`、`SVG`、`OPF`、`NCX`、`CSS`、`ARIA`、`OpenCC`、
`Regex` 和 `Emmet`。对话框正文、按钮、标签、设置项、工具提示、状态消息、
报告表头和程序生成的显示名称必须翻译。

## 覆盖率检查

`zh_cn_translation_coverage` 测试会重新从当前 C++、头文件和 Qt Designer
文件提取全部源文本，并与简体中文目录比较。以下情况会导致测试失败：

- 当前源文本在目录中缺失，或目录仍保留不对应源码的有效条目；
- 译文为 `unfinished` 或空字符串；
- 复数译文缺失；
- `%1`、`%n` 等占位符数量不一致；
- 富文本标签结构不一致；
- 高置信度的 Qt 控件文本直接使用英文字符串而未调用 `tr()`。

运行单项检查：

```sh
ctest --test-dir cmake-build-debug \
    -R zh_cn_translation_coverage --output-on-failure
```

生成发布用翻译文件时可额外验证：

```sh
lrelease src/Resource_Files/ts/sigil_zh_CN.ts -qm /tmp/sigil_zh_CN.qm
```

输出必须显示所有有效条目均为 `finished`，且 `unfinished` 为零。
