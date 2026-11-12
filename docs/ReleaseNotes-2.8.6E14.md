# Sigil-Enhanced 2.8.6E14 更新说明

源码发布日期：2026 年 11 月 13 日。安装包发布日期以发布页面为准。

本说明汇总 `v2.8.5E13` 至 `v2.8.6E14` 的改动。

## 同步上游 Sigil 2.8.6

- EPUB 3 更换封面时写错的 OPF manifest 条目已修复（上游 #903），并且只重写 `cover-image` 属性真正发生变化的条目，不再重写全部条目。
- Aria Clips 恢复尾注返回链接，不再输出已废弃的 `epub:type="endnote"`；Fluent 主题的 Aria Clips 图标已更新；MarcRelators 中重复的 Recording Engineer 条目已删除。
- Python 插件 wrapper 不再把 `application/javascript`、`application/pls+xml` 返回成 bytes，改为普通字符串；PluginRunner 与 wrapper 同时识别 `application/oebps-page-map+xml` 和 `application/vnd.adobe-page-map+xml`，修掉了 `oebs` 拼写错误。
- quickparser 重建标签时转义属性值中的双引号，属性用单引号且值里带字面双引号时不再生成坏标记。
- CSS 结构解析给注释单独的缓冲区，选择器里的注释不再破坏后续 token 的位置（上游 #909）。
- 字体资源构造时初始化混淆算法字段，添加字体后立即引用不再崩溃。
- Book Browser 定位改用 `EnsureVisible`，双击条目不再被误判成拖动。
- Lexbor CSS 解析器显式按静态库构建，避免部分构建系统生成动态库（上游 #905）。
- 语言列表补充 Gilaki（glk）和 Mazandarani（mzn）；`special_characters.xml` 支持 `hidden="true"` 的分组。

## 图片与预览

- jxl 图片在预览、查看图片、插入图片/选择文件的缩略图里可用：Qt 能读取 jxl 时，`sigil://` 处理器把它转成 JPEG 返回给 QtWebEngine。Windows 安装包会额外带上 `qjpegxl6.dll`。
- SVG 图片标签同时接受 `href` 和 `xlink:href`，两者写在一起也能识别。
- AdjustImage 按 `QImageReader`/`QImageWriter` 的能力决定可读、可写格式，写入不支持时直接提示并中止，PBM/PGM 有单独文案；图片标签页右键新增「从磁盘重新加载图像」，有未保存修改时先确认。
- 通过 Add Existing 或拖放替换已有图片后，预览会自动刷新，不需要手动重开。
- 保留本项目的写入策略：提取出的文件无法替换时，新图片内容留在内存、保存书籍时再打包，图片内容变化只把书标记为已修改，不会用磁盘上的旧文件覆盖尚未写入的编辑。

## 搜索、字体和界面

- 增强查找替换新增「仅文本」选项，和原版搜索一样跳过标签。
- 修复增强查找替换在行高亮、焦点变化后丢失当前匹配的问题，样式表里的匹配可以正常替换；捕获组在选区稳定后记录，配合单次和批量撤销都有测试覆盖。
- 设置新增「文件选择对话框」：可选系统原生对话框或 Qt 旧对话框，默认系统原生。添加已有文件时在 Qt 旧对话框下仍保留预览。
- 字体子集按 CSS `font-family` 分组，多个家族共用一款中日文字体时不再按整本字符集裁剪；`fsType` 不再阻止子集；预览显示缩放后的文件大小和 maxp 字形数，并提示目标字体缺少的字符。
- Windows 下恢复 Code View 文本拖动且不走 Qt 原生拖放路径，选区在拖动结束后移动，避免在拖动循环里更新光标。
- 查找/替换的草稿和历史会保留，新开的实例不再预填；Windows 启动恢复窗口布局时保留标题栏。

## 插件兼容性说明

- 插件通过 `readotherfile()` 读取 `META-INF` 下的普通 XML（例如 `container.xml`）仍然得到 bytes。上游把 page-map 媒体类型改成文本类型后，`.xml` 扩展名会被误判成 page map 而返回字符串，因此这里把通用 XML 扩展映射回 `application/xml`；清单中的 page map 仍按 OPF 里的媒体类型当作文本读取。

## 验证与发布

- macOS Debug 完整编译通过。
- 完整 CTest：181 项通过，3 项 Cmoa 样例测试按设计跳过。
- 本轮新增测试覆盖文件对话框策略与覆盖检查、设置并发与启动策略、搜索记忆、Code View 文本拖动、带捕获组的 Code View 替换，以及字体子集批处理与核心逻辑。
- 版本号统一为 CMake `2.8.6E14`、更新订阅 `2.8.6.E14` 和标签 `v2.8.6E14`。标签推送后由 CI 构建 Windows x64、macOS x64/arm64 安装包并生成校验和，构建结果以发布页面为准。
