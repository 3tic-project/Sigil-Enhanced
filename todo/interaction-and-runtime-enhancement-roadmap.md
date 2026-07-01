# 图片编辑、资源插入与内置 Python 依赖优化 Roadmap

日期: 2026-07-01

## 背景

本轮要评估并规划以下优化点:

1. 图片编辑页面允许缩放到更小。
2. 图片编辑页面支持 Ctrl+滚轮缩放。
3. 拖入图片、CSS 等文件时, 在当前光标位置插入对应标签或引用, 并遵循右键 Insert 的判断和检查。
4. 从浏览器或其他应用复制/拖入没有本地文件路径的图片时, 也能添加到书内并插入当前位置; 无可用文件名或文件名非法时使用 `image-*` 序号命名。
5. 鼠标悬停在 Book Browser 的图片文件上时显示浮动预览图。
6. 批量替换等操作报 `ModuleNotFoundError: No module named 'dulwich'`, 需要让构建产物的内置 Python 包含程序实际需要的第三方库。

总体判断: 第 6 项是当前阻断性问题, 应优先处理。第 3、4 项共享同一条资源插入链路, 应先抽公共逻辑再扩展拖放/粘贴。第 1、2、5 项是低风险体验增强, 可穿插推进, 但不要和资源导入重构混在同一个提交里。

## 进度记录

### 2026-07-01: Step 1 已完成

已完成“修复 Python 依赖打包”的第一阶段实现:

- 新增核心依赖清单 `src/Resource_Files/python_pkg/requirements-core.txt`, 固定 Sigil 内部功能需要的 Python 包版本。
- 新增 `src/Resource_Files/python_pkg/sync_python_packages.py`, 负责把核心依赖安装到可复用缓存目录, 再同步到 macOS app 的 `Contents/python3lib`。
- 新增 `SIGIL_PYTHON_CACHE_DIR` CMake 配置项:
  - macOS/Linux 默认 `~/.cache/sigil-enhanced/python`。
  - Windows 默认 `%LOCALAPPDATA%/Sigil-Enhanced/python-cache`。
  - 可通过环境变量或 CMake 参数覆盖。
- 修复 `USE_VIRT_PY` 默认值拼写问题, Windows 默认启用缓存 venv。
- Windows Python venv 从 build 目录迁移到缓存目录, requirements hash 不变时复用已有环境, 避免每个 build 目录重复下载。
- macOS Debug 构建会同步核心依赖到 `Sigil.app/Contents/python3lib`; 正式打包完整 Python.framework 仍保留 `-DPKG_SYSTEM_PYTHON=1` 路径。
- 补齐 `dulwich==1.0.0` 的传递依赖 `urllib3` 和 `typing_extensions`。

已验证:

- `cmake -S . -B cmake-build-debug` 通过。
- `cmake --build cmake-build-debug --target Sigil -j 4` 通过, 再次构建为 `ninja: no work to do`。
- 隔离导入 `lxml`, `dulwich`, `regex`, `css_parser`, `cssselect`, `html5lib`, `webencodings`, `urllib3`, `typing_extensions`, `chardet`, `certifi`, `PIL`, `six` 成功。
- 直接导入 `repomanager` 成功, 覆盖之前 `ModuleNotFoundError: No module named 'dulwich'` 的路径。
- `git diff --check` 通过。

### 2026-07-01: Step 2 已完成

已完成“图片编辑缩放体验”的代码实现:

- 图片编辑页最小缩放从约 33.3% 降到 10%, 最大缩放保持 300%。
- 将缩放上下限和缩放步进统一成常量, 避免继续散落硬编码。
- 支持在图片编辑区使用 `Ctrl+滚轮` 放大/缩小。
- 普通滚轮不被拦截, 仍交给滚动区域做上下滚动。
- 修正按绝对比例恢复缩放时滚动条调整倍率的计算, 避免把目标缩放比例误当作滚动倍率。
- `Zoom To Fit` 增加空尺寸保护, 避免异常图片或未初始化视口触发无效计算。

已验证:

- `cmake --build cmake-build-debug --target Sigil -j 4` 通过。
- `git diff --check` 通过。
- 人工验证功能正常。

### 2026-07-01: Step 3 已完成

已完成“抽公共资源插入逻辑”的第一阶段实现:

- 新增 `src/Misc/ResourceInsertion.cpp` / `src/Misc/ResourceInsertion.h`, 统一判断资源是否可插入 HTML/CSS 上下文。
- 统一生成图片/SVG、音频、视频的 HTML 插入片段, 以及 CSS `url("...")` 引用。
- 菜单 `Insert File`、Book Browser 右键 `Insert Into HTML/CSS File`、粘贴本地/剪贴板图片均改为复用同一套插入文本生成逻辑。
- 资源相对路径统一使用 `Resource::GetRelativePathFromResource()` 后再 URL 编码。
- 文件名生成的 alt/文本标签统一做 XML 转义, 避免文件名含 `&`, `<`, `"` 等字符时生成非法片段。
- Book Browser 向编辑器拖入资源已接入同一套插入逻辑; 外部本地文件直接拖入编辑器已取消。

已验证:

- `cmake --build cmake-build-debug --target Sigil -j 4` 通过。
- `git diff --check` 通过。
- 人工验证 Book Browser 拖入编辑器功能正常。

### 2026-07-01: Book Browser 拖入编辑器已完成

已完成:

- Book Browser 拖出资源时额外携带 Sigil-Enhanced 自定义资源 identifier mime 数据。
- CodeViewEditor 允许接收 Book Browser 资源拖拽, 但仍默认拒绝普通外部文本/文件拖入, 保持原有误插入保护。
- 拖入 XHTML 正文时按 HTML 插入逻辑生成图片/SVG、音频、视频标签。
- 拖入 CSS 文件或 XHTML 内嵌 CSS 区域时按 CSS 插入逻辑生成 `url("...")`。
- 拖入编辑器时使用鼠标释放位置设置插入光标, 插入位置跟随 drop 点。
- 拖入编辑器采用 Copy 语义, 只插入引用, 不移动 Book Browser 中的资源。

已验证:

- `cmake --build cmake-build-debug --target Sigil -j 4` 通过。
- `git diff --check` 通过。
- 人工验证拖放功能正常。

### 2026-07-01: 外部本地文件直接拖入编辑器已取消

已按维护决策撤销“从 Finder 等外部来源直接拖文件到编辑器并插入”的未提交实现。

保留策略:

- Book Browser 内已有资源可以拖入编辑器并在释放位置插入引用。
- 外部本地文件拖到主窗口空白区域或 File Drop Zone 时仍只添加文件, 不直接向编辑器插入引用。
- 下一步只处理非本地图片“粘贴”导入, 不做外部直接拖入。

### 2026-07-01: 非本地图片粘贴已完成

已完成:

- 浏览器复制图片时, 如果剪贴板同时带非本地 URL 和图片数据, 不再错误地按本地文件 URL 导入。
- 非本地图片粘贴改为直接读取 `QMimeData::imageData()`, 转为 PNG 后加入书内并插入引用。
- 没有可靠文件名时使用 `image-0001.png`, 后续重复粘贴由 `FolderKeeper::GetUniqueFilenameVersion()` 递增命名。
- 如果远程 URL 路径里能取到文件名, 取净化后的 basename 并保存为 `.png`, 以匹配实际写入的 PNG 数据。
- XHTML 正文粘贴生成 `<img .../>`; CSS 文件或 XHTML 内嵌 CSS 区域粘贴生成 `url("...")`。
- 不做远程 URL 下载, 不做网页图片拖入。

已验证:

- `cmake --build cmake-build-debug --target Sigil -j 4` 通过。
- `git diff --check` 通过。
- 人工验证行为正确。

## 当前代码现状

### 图片编辑缩放

相关文件:

- `src/Widgets/AdjustImage.cpp`
- `src/Widgets/AdjustImage.h`
- `src/Tabs/ImageTab.cpp`

当前状态:

- `AdjustImage::scaleImageBy()` 和 `AdjustImage::scaleImageUsing()` 已统一通过缩放常量限制比例。
- `AdjustImage::eventFilter()` 已处理 `QEvent::Wheel`, 仅在按下 Ctrl 时拦截并执行图片缩放。
- `m_imageLabel` 和 `QScrollArea` viewport 都安装了 event filter, 避免鼠标位于图片或空白滚动区时行为不一致。
- `ImageTab` 仍通过 `SettingsStore::zoomImage()` 保存图片缩放比例, 并通过 `InternalZoomFactorChanged` 同步设置。

已处理:

- 最小缩放采用 `MIN_IMAGE_ZOOM = 0.10`, 最大缩放保持 `MAX_IMAGE_ZOOM = 3.0`。
- Ctrl+滚轮使用原有缩放步进: 放大 `1.25`, 缩小 `0.80`。
- 普通滚轮不拦截, 继续由 `QScrollArea` 处理。
- `scaleImageUsing()` 已改为使用“旧比例 -> 新比例”的实际倍率调整滚动条。
- `ZoomToFit` 结果会被 clamp 到最小/最大缩放范围内。若后续需要超大图完整 fit 到 3%-5%, 再单独讨论是否把 `MIN_IMAGE_ZOOM` 降到 `0.05`。

验收项:

- 打开大图, 可以连续缩小到 10%。
- Ctrl+滚轮在图片编辑页缩放, 普通滚轮仍然滚动。
- 缩放比例能被菜单和状态同步, 关闭重开后设置生效。
- 裁剪模式下 Ctrl+滚轮不触发裁剪误操作。

## 资源插入与拖放/粘贴

### 现有插入路径

相关文件:

- `src/MainUI/MainWindow.cpp`
- `src/MainUI/MainWindowExt.cpp`
- `src/MainUI/BookBrowser.cpp`
- `src/MainUI/BookBrowserExt.cpp`
- `src/MainUI/BookBrowserTreeView.cpp`
- `src/ViewEditors/CodeViewEditorExt.cpp`
- `src/Tabs/FlowTab.cpp`
- `src/Tabs/CSSTab.cpp`

当前状态:

- 菜单 `Insert File` 路径在 `MainWindow::InsertFileDialog()` / `MainWindow::InsertFiles()`:
  - 仍只要求当前 `FlowTab` 可插入。
  - 插入片段改由 `ResourceInsertion::TextForResource()` 生成。
- Book Browser 右键 `Insert Into HTML/CSS File` 路径在 `MainWindow::InsertFileFromBookBrowser()`:
  - HTML/CSS 目标判断改由 `ResourceInsertion::ContextFromTargetResource()` 和 `ResourceInsertion::CanInsertResource()` 处理。
  - 插入片段改由 `ResourceInsertion::TextForResource()` 生成。
- `CodeViewEditorExt.cpp` 已经支持粘贴图片:
  - `QMimeData::hasImage()` 时保存成 PNG。
  - `hasUrls()` 时仅处理本地图片路径。
  - 当前默认名固定为 `Images0001.png`, 依赖 `FolderKeeper::GetUniqueFilenameVersion()` 间接去重。
  - 导入后的 HTML/CSS 引用文本改由 `ResourceInsertion::TextForResource()` 生成。
- 主窗口拖放 `MainWindow::dropEvent()` 只接受本地文件路径, 然后 `AddDroppedFiles()` 把文件加入书内, 不会插入当前位置。
- Book Browser 的 `BookBrowserTreeView::dropEvent()` 会处理本地文件:
  - 单个 HTML/TXT 拖到 Text 分组时按阅读顺序插入。
  - 其他文件通过 `BookBrowser::AddFiles()` 添加。
- `BookBrowserExt.cpp::AddFiles()` 和主线 `BookBrowser::AddExisting()` 已经出现逻辑分叉。后续不宜继续在 `AddFiles()` 上叠新规则。

判断:

- 第 3、4 项不应直接在 `MainWindow::dropEvent()`、`BookBrowserTreeView::dropEvent()`、`CodeViewEditor::insertFromMimeData()` 中分别拼接标签。否则会形成第三、第四套插入规则。
- 应先抽出“资源加入书内 + 根据当前目标生成插入文本 + 在当前编辑器插入”的公共逻辑。
- “遵循右键 Insert 的判断和检查”意味着要复用或等价复刻 `MainWindow::InsertFileFromBookBrowser()` 的判断:
  - HTML 目标: 需要当前 `FlowTab::InsertFileEnabled()` 为真。
  - CSS 目标: 图片/SVG/字体可插入 `url("...")`。
  - HTML 内联 `<style>` 场景: 目前 `CodeViewEditor::HtmlViewPasteEvent()` 已经能通过 `CodeCompleterParser` 判断 CSS 上下文。重构时可以保留这个能力, 但公共插入服务要能接收“强制按 CSS 插入”的参数。

### 推荐架构

新增一个轻量公共入口, 二选一:

方案 A: 在 `MainWindow` 中增加私有方法

- `bool CanInsertResourceIntoCurrentEditor(Resource *resource, QString *reason = nullptr) const`
- `QString BuildInsertionText(Resource *resource, Resource *target_resource, InsertContext context) const`
- `bool InsertResourceReferencesIntoCurrentEditor(const QList<Resource *> &resources, InsertContext context)`
- `QStringList AddExternalFilesForInsertion(const QStringList &filepaths)`
- `QString AddImageBytesForInsertion(const QByteArray &data, const QString &suggested_name, const QString &mime_type)`

方案 B: 新增 `src/MainUI/ResourceInsertion.*`

- 优点: 逻辑独立, 后续拖放、粘贴、Book Browser 右键都能调用。
- 缺点: 需要传入 `BookBrowser`、`Book`、当前 tab 等上下文, 初次改动略大。

推荐先用方案 A, 因为现有插入动作都已经在 `MainWindow` 附近汇合, 改动范围更小。等稳定后再考虑拆类。

### 插入文本规则

应以现有规则为基准:

HTML 目标:

- 图片/SVG: `<img alt="filename" src="relative/path"/>`
- 视频: `<video controls="controls" src="relative/path">filename</video>`
- 音频: `<audio controls="controls" src="relative/path">filename</audio>`
- CSS: 建议新增为 `<link href="relative/path" type="text/css" rel="stylesheet"/>`

CSS 目标:

- 图片/SVG/字体: `url("relative/path")`
- CSS: 暂不建议在任意位置自动插入; 如果要支持, 应仅在 CSS 顶层插入 `@import url("relative/path");`, 并用 parser 判断当前位置是否合适。

注意:

- CSS 文件拖入 HTML 是超出现有右键 Insert 的语义, 当前维护决策为暂不支持。
- 多文件插入时, HTML 标签可以连续插入; CSS `url(...)` 多项可用逗号分隔, 延续现有 `CodeViewEditorExt.cpp` 的行为。
- 插入动作必须使用 `InsertTextAsSingleUndoStep()` 或等价路径, 使“插入引用”可撤销。导入到书内的资源不随撤销删除, 状态栏要延续“Undo removes only the inserted reference”提示。

### 拖入文件

当前决策:

- 已支持 Book Browser 内已有资源拖入编辑器, 在释放位置按 HTML/CSS 上下文插入引用。
- 不支持 Finder 等外部本地文件直接拖入编辑器并插入引用。
- 对拖入主窗口但不是编辑器区域的外部本地文件, 保持当前 `AddDroppedFiles()` 行为, 避免用户只是想加文件时被意外插入。

实现位置:

- Book Browser 拖入编辑器已在 `BookBrowserTreeView` 和 `CodeViewEditor` 层处理。
- `MainWindow::dropEvent()` 仍负责窗口空白区域或非编辑器区域的添加文件。

### 非本地图片粘贴

支持来源:

- `QMimeData::hasImage()`: 直接将 `imageData()` 写入 PNG。
- `text/html` 中的 `data:image/...;base64,...`: 可解析出原始 bytes 和扩展名。
- `text/uri-list` 里非本地 URL: 不自动联网下载, 仅在同时存在 `hasImage()` 时处理。后续如要支持远程 URL 下载, 应加确认和错误提示。

命名规则:

- 从 MIME/HTML 能取到文件名时, 先净化文件名:
  - 去掉路径分隔符、控制字符和 `<>:"/\\|?*`。
  - 空名、隐藏文件名、无扩展名或扩展名不匹配时 fallback。
- 无可用文件名时:
  - 默认 `image-0001.png`。
  - 传给 `FolderKeeper::GetUniqueFilenameVersion()` 后得到 `image-0002.png` 等唯一名。
- 扩展名:
  - `image/png` -> `.png`
  - `image/jpeg` -> `.jpg`
  - `image/gif` -> `.gif`
  - `image/webp` -> `.webp`
  - 未知或 `QImage` 来源 -> `.png`

需要补强的现有函数:

- `BookBrowser::AddImageFromClipboard(const QByteArray &data, QString defaultFilename)`
  - 改名或新增更通用方法, 如 `AddImageBytes(const QByteArray &data, const QString &suggested_filename, const QString &mime_type)`。
  - 负责合法文件名、扩展名和唯一名。
  - 成功返回 book path, 失败返回空并给出清晰错误。

验收项:

- 从浏览器复制图片后在 HTML 粘贴, 自动生成 `image-0001.png` 或基于远程 URL basename 的 `.png` 文件名, 并插入 `<img>`。
- 第二次粘贴同类无名图片生成递增文件名, 不覆盖已有资源。
- 在 HTML 标签属性内部、body 外等非法位置尝试插入, 应拒绝并提示。
- 撤销只移除插入文本, 不删除已导入资源。

## Book Browser 图片悬停预览

相关文件:

- `src/MainUI/BookBrowser.cpp`
- `src/MainUI/BookBrowser.h`
- `src/MainUI/BookBrowserTreeView.cpp`
- `src/MainUI/BookBrowserTreeView.h`
- `src/MainUI/OPFModel.cpp`

现状:

- `OPFModel::InitializeModel()` 为每个资源项设置了文本 tooltip, 内容主要是相对路径、语义和 manifest properties。
- `BookBrowserTreeView` 当前只处理拖放绘制, 没有 hover preview。
- 模型项 `item->data()` 存的是 resource identifier, 可以通过 `FolderKeeper` 找回资源。

判断:

- 不建议把图片直接塞进 Qt 的普通 tooltip HTML。原因:
  - 尺寸和缩放不可控。
  - 大图加载会卡 UI。
  - 后续要隐藏、延迟、缓存、适配 SVG 会比较别扭。
- 推荐 BookBrowser 拥有一个轻量 preview popup:
  - `QFrame` + `QLabel`。
  - 通过 `m_TreeView->viewport()->installEventFilter(this)` 或在 `BookBrowserTreeView::viewportEvent()` 中发 signal 实现。

推荐实现:

- 在 `BookBrowser` 侧加:
  - hover timer, 例如 250ms。
  - preview widget, 最大尺寸 320x240 或 360x260。
  - thumbnail cache, key = full path + lastModified + file size。
- 仅对 `ImageResourceType` 和 `SVGResourceType` 生效。
- raster 图片用 `QImageReader` 限制解码尺寸, 避免超大图完整加载。
- SVG 用现有 `Utility::RenderSvgToImage()` 后缩放。
- 鼠标离开、滚动、展开/折叠、右键菜单、资源刷新时隐藏 popup。

验收项:

- 悬停普通图片显示预览。
- 悬停 SVG 显示预览。
- 悬停非图片仍显示原文本 tooltip 或不显示预览。
- 快速扫过大量图片不会明显卡顿。
- 资源改名/替换后预览缓存刷新。

## 内置 Python 与第三方依赖

相关文件:

- `src/qt6sigil.cmake`
- `src/EmbedPython/EmbeddedPython.cpp`
- `src/sigil_constants.cpp`
- `src/Resource_Files/python_pkg/copy_python_package.py`
- `src/Resource_Files/python_pkg/osx_add_python_framework6.py`
- `.github/workflows/requirements.txt`
- `docs/plugin-reqs.txt`
- `src/Resource_Files/python3lib/repomanager.py`

现状:

- 应用启动时:
  - `main.cpp` 创建 `EmbeddedPython`。
  - `embeddedRoot()` 加入 `sys.path`, macOS 指向 `Sigil.app/Contents/python3lib`。
  - plugin launcher 路径也加入 `sys.path`。
- macOS post-build 当前复制:
  - `src/Resource_Files/python3lib/*` 到 `Contents/python3lib`。
  - `src/Resource_Files/plugin_launchers/python/*`。
  - 如果 `Python3_EXECUTABLE` 存在, 只额外复制 `lxml` 到 `Contents/python3lib`。
- `osx_add_python_framework6.py` 的完整框架打包清单里包含 `dulwich`、`regex`、`css_parser`、`certifi` 等, 但 `PKG_SYSTEM_PYTHON` 在非 MSVC 默认是 0, 普通 macOS Debug 构建不会跑这个完整打包脚本。
- 当前 Debug bundle 的 `Contents/python3lib` 里能看到 `lxml`, 但没有 `dulwich`, 因此 `repomanager.py` 报错。

判断:

- 这是 P0。`dulwich` 缺失会影响 repo/checkpoint/批量替换相关功能。
- 不能只临时复制 `dulwich`, 因为后续还会遇到 `regex`、`css_parser`、`cssselect`、`html5lib`、`PIL` 等依赖不一致问题。
- 需要让开发构建和正式打包共享同一份“内置 Python 运行时依赖清单”。

推荐短期修复:

1. 扩展 `copy_python_package.py` 支持一次复制多个包:
   - `copy_python_package.py DESTINATION PACKAGE...`
   - 或保留现有接口, 在 CMake 中 foreach 调用。
2. 在 `src/qt6sigil.cmake` 中定义 CMake list, 例如:
   - `SIGIL_EMBEDDED_PYTHON_PACKAGES`
   - 初始包含: `lxml`, `dulwich`, `regex`, `css_parser`, `cssselect`, `html5lib`, `webencodings`, `chardet`, `certifi`, `PIL`, `six`
   - 如内部功能不需要 PySide6, 不要默认塞进 Debug runtime; plugin GUI 依赖另列为可选。
3. macOS post-build 对 `Contents/python3lib` 复制整个清单。
4. 构建后加入一个轻量验证命令或开发文档命令:

```sh
cmake-build-debug/bin/Sigil.app/Contents/MacOS/Sigil
```

并在应用内触发批量替换/检查点路径确认不再报 `dulwich`。

推荐长期修复:

- 单一依赖清单来源:
  - `.github/workflows/requirements.txt` 负责固定版本。
  - CMake 负责读取或维护同名包清单。
  - `docs/plugin-reqs.txt` 更新为说明性文档, 不作为唯一事实源。
- 启动自检:
  - 增加一个 Python 小模块, 检查 Sigil 内部必需模块是否可 import。
  - 失败时在日志中明确列出缺失模块和 `sys.path`, 而不是等功能触发 traceback。
- 区分“应用内部必需”和“插件 GUI 可选”:
  - 内部必需缺失应视为构建错误。
  - 插件 GUI 依赖可通过打包选项控制。

验收项:

- Debug app 内置 Python 能 import:
  - `lxml`
  - `dulwich`
  - `regex`
  - `css_parser`
  - `cssselect`
  - `html5lib`
  - `PIL`
- 批量替换、checkpoint/repo manager 路径不再出现 `ModuleNotFoundError`。
- 关闭网络和外部 `PYTHONPATH` 后仍可运行。
- Release bundle 与 Debug bundle 的依赖行为一致。

## 推荐实施顺序

### Step 1: 修复 Python 依赖打包

目标:

- 解决当前 `dulwich` traceback。
- 让 Debug bundle 的 `Contents/python3lib` 包含内部功能需要的第三方库。

建议提交:

- `* 补全内置Python运行依赖`

验证:

- 构建。
- 运行缺失模块 import 检查。
- 复测批量替换和 checkpoint 相关功能。

### Step 2: 图片编辑缩放体验

目标:

- 图片编辑页最小缩放降到 10%。
- 支持 Ctrl+滚轮缩放。

建议提交:

- `* 优化图片编辑缩放交互`

验证:

- 大图缩放、滚轮、普通滚动、缩放保存、裁剪模式。

### Step 3: 抽公共资源插入逻辑

目标:

- 消除 `MainWindow::InsertFiles()`、`MainWindow::InsertFileFromBookBrowser()`、`CodeViewEditorExt.cpp` 中重复的标签拼接逻辑。
- 保证右键 Insert、菜单 Insert、粘贴图片行为一致。

建议提交:

- `* 统一资源插入逻辑`

验证:

- 右键 Book Browser 插入图片/SVG/音频/视频/字体。
- 菜单 Insert File 插入图片/音频/视频。
- 粘贴本地图片仍可插入。

### Step 4: 支持 Book Browser 拖入编辑器

目标:

- 从 Book Browser 拖已有资源到编辑器时, 在释放位置按当前编辑器上下文插入引用。
- 主窗口空白区域拖入外部本地文件仍保持“只添加文件”。
- 不做外部本地文件直接拖入编辑器。

建议提交:

- `* 统一资源插入逻辑并支持Book Browser拖入`

验证:

- 从 Book Browser 拖图片/SVG/音频/视频到 HTML 正文。
- 从 Book Browser 拖图片/SVG/字体到 CSS。
- 非法位置拒绝插入。
- 多文件插入。

### Step 5: 支持非本地图片粘贴

目标:

- 浏览器复制图片后可粘贴插入。
- 没有合法文件名时使用 `image-0001.png` 递增命名。
- data URI 图片可解析。
- 不做网页或外部应用直接拖入。

建议提交:

- `* 支持剪贴板图片导入并自动命名`

验证:

- Chrome/Safari/Firefox 复制图片粘贴。
- 重复粘贴命名递增。
- 不能取得图片数据时给出提示, 不误插空引用。

### Step 6: Book Browser 图片悬停预览

目标:

- 悬停图片资源显示缩略预览。
- 不影响普通 tooltip 和 Book Browser 拖放。

建议提交:

- `* 添加Book Browser图片悬停预览`

验证:

- PNG/JPG/WebP/SVG。
- 大图性能。
- 改名/替换后缓存刷新。
- 滚动/右键菜单时 popup 正常隐藏。

## 风险与注意点

- 上游同步风险: `BookBrowserExt.cpp` 和主线 `BookBrowser::AddExisting()` 已经分叉, 应尽量减少继续修改 `AddFiles()`。如果要动拖放添加文件, 优先复用 `AddExisting(..., already_selected)` 或抽公共添加函数。
- 撤销语义: 插入文本可撤销, 但导入资源不撤销。这一点已有用户提示, 新路径必须保持一致。
- CSS 插入语义: CSS 文件拖入 HTML 是新增语义, 需要明确作为 Enhanced 功能。CSS 文件拖入 CSS 是否插入 `@import` 需要谨慎, 第一阶段可不做。
- 非本地 URL: 自动联网下载远程图片有安全和失败处理成本。第一阶段只处理 MIME 已携带的图像数据或 data URI。
- Python 依赖: 不要依赖开发机全局 site-packages。构建产物必须独立运行, 否则用户环境一变就会复现 `ModuleNotFoundError`。

## 下一步建议

外部本地文件直接拖入编辑器已取消。Step 5 非本地图片粘贴已完成代码实现, 建议按上方“待人工验证”复测浏览器复制图片粘贴路径; 验证通过后提交本步, 然后进入 Step 6“Book Browser 图片悬停预览”。
