# 导出 TXT 文件

在 **增强 → 导出 TXT 文件…** 中选择保存位置，即可把当前书籍导出为 UTF-8
纯文本。默认使用书籍同名的 `.txt` 文件，之后记住上次导出目录。

导出包含当前编辑器中尚未保存的内容。它不保存或改写 EPUB，不改变书籍文件名、
未保存状态、当前标签页、光标位置或撤销/重做历史。取消保存对话框不会创建文件。

## 内容与排版

- 优先按照当前 OPF 的 spine 顺序导出，包括非线性章节；未在 spine 中登记的
  XHTML 按书内路径排序后附在末尾，保留目录、注释和附录等补充内容。
- 提取正文，保留标题、父容器中的直接文字和嵌套段落，避免只选“叶子段落”造成漏文。
  原文和译文都保留，不按照翻译工具的 class 删除译文。
- 段落、标题、列表项和表格行各自换行；表格单元格以制表符分隔。`br` 保留显式换行，
  包括连续换行；章节之间空一行，文件末尾保留一个换行。
- 普通 HTML 排版空白折叠为空格，保留全角缩进、不换行空格及 Unicode 字符。
  `pre` 保留空格、制表符和换行。
- ruby 保留基底文字，排除 `rt`、`rp`、`rtc` 注音。排除页头、脚本、样式、模板、
  注释、`hidden` 元素及行内样式明确隐藏的内容。
- 保留图片的 `alt` 替代文字和 SVG 中的文字；支持 spine 中的独立 SVG。
  图片没有替代文字时不会生成占位文字，也不执行 OCR。
- 输出使用 UTF-8，无 BOM，换行统一为 LF。

这是基于文档结构的文本导出，不运行浏览器布局：外部 CSS 中的隐藏规则、伪元素、
CSS 计数器和 SVG 的视觉坐标不会转换为文本排版。纯图片书籍没有可提取文字时会提示。

导出使用原子文件替换。没有可导出的文本、解析器达到嵌套限制或写入失败时，
不会交付空文件或不完整文件；已有目标文件保留。源 EPUB 不能作为 TXT 的输出路径。

## 实现与测试

`BuiltinPlugins::PlainTextExporter` 是随 Sigil 编译的内置原生插件，不需要安装
外部 Python/Rust 插件或转换工具。MainWindow 从当前 Book/FolderKeeper 读取文档快照，
导出器使用 Sigil 自带、支持 XHTML 自闭合标签的 Gumbo 解析器。

行为参考了以下项目，提取和写入代码基于 Qt/Sigil 独立实现：

- TextAlignTool 的 `src/cli/input.rs`：将 EPUB 章节内容转换为文本行。
- OrionTranslator 的 `betelgeuse/src/epub.rs`：阅读顺序、正文块和 ruby 基底文字提取。
- epub_scan 的 `src/main.rs::render_book_text`：按章节顺序组织纯文本。
- epub_writer 的 `src/parser/epub.rs` 和 `src/exporter/txt.rs`：正文、Unicode 与文本换行保留。

测试包括独立的 `plain_text_exporter`，以及使用真实 MainWindow、Book、Code View
和“增强”菜单的 `plain_text_export_epub2_integration`、
`plain_text_export_epub3_integration`。集成测试验证阅读顺序、未保存编辑、文件字节、
取消操作、源文件保护和导出后的撤销/重做。
