# Windows E14 反馈修复

反馈来自 Windows E14；开发与本地运行环境为 macOS、Qt 6.7.3。

## 同名字体覆盖闪退

`BookBrowser::AddExisting` 接受字体、SVG、音视频覆盖，却在覆盖后无条件使用 `ImageResource` 转型结果。替换字体时该指针为空。现在通知实际被覆盖的资源，不删除或重建资源；路径、资源 ID、OPF 清单及资源属性保持不变。

实际文件选择框回归覆盖取消覆盖、确认覆盖、字体标签页已打开、文件内容确实改变、书籍修改状态以及清单和资源身份保持。旧代码通过此用例确认收到 `SIGSEGV`，修复后通过。

## 搜索展开及重开后的面板宽度

搜索栏保持在编辑器下方，宽度跟随编辑区。搜索模式先初始化，再恢复对应面板的可见性，避免启动时读到未初始化模式或同时展开两种面板；首次显示时，先完成中央区域和主窗口的布局计算，再恢复保存的 Dock 布局。

真实 MainWindow 回归检查两种搜索始终位于编辑器下方，并在容得下搜索控件的布局中反复开关、拉宽预览、保存及从独立进程重新打开，核对图书浏览器与预览的宽度。还覆盖旧设置中两种搜索均标记可见的情况。编辑区过窄时仍沿用原有的 Qt 最小宽度约束。

## 面板标题重复绘制

图书浏览器、目录、验证结果、片段及预览此前自行绘制整个 Dock 标题，绕过 Qt 对原生装饰和自定义标题栏的处理。现在仅通过 `initStyleOption` 提供标题与 macOS 配色，绘制交回 Qt；预览独立于标签页的标题仍然保留。回归检查默认标题、预览自定义标题以及自定义或隐藏标题栏不被再次绘制。

## XHTML 添加副本后重命名无响应

重命名会在后台更新全书链接，再通过 `TextResource::DelayedUpdateToTextDocument` 将缓存文本应用到 GUI 文档。该函数持有缓存互斥锁时，文档会同步发出变化通知；修改状态检测等读取者可能再次调用 `GetText()`，尝试取得同一把不可重入锁，使界面线程永久等待。

缓存锁改为 `QRecursiveMutex`，允许同一界面线程在通知中读取当前缓存，同时继续排除其他线程的并发写入。复制、重命名、链接更新和保存流程保持原有行为。修改状态检测在发现首个已变更资源时会提前返回；文档状态、资源遍历顺序和通知时序可能影响是否重入该锁，因而可能表现为概率性无响应。

本地实际复制／重命名流程出现过卡住；另用同步文本回读观察者稳定复现，并抓到 `DelayedUpdateToTextDocument → setPlainText → contentsChanged → GetText → QMutex::lock` 的等待栈。回归通过真实的“添加副本”和行内文件名编辑器，覆盖立即重命名、编辑器加载完成后重命名、编辑后重命名及通知内同步回读；每种情况连续复制、重命名三次，并检查导出 EPUB 的正文、资源身份、清单和阅读顺序。

## 验证范围

- Sigil Debug 构建成功；缓存锁修复后的 14 项相关 CTest 全部通过（247.66 秒），覆盖复制重命名、正文和 OPF 保存、替换撤销、Code View、搜索历史、布局、Agent 与翻译窗口，以及字体覆盖。
- macOS Cocoa 原生窗口下，四种复制重命名场景再次全部通过，导出 EPUB 校验通过。每种场景连续执行三次，共 12 次复制重命名。
- Windows 轻量 CI 保持原有的三档缩放启动检查。本次未推送或运行远程 CI。
- 字体测试使用仓库字体和临时 EPUB、隔离设置；未修改用户书籍或应用偏好。离屏模式下原生文件框配置会回退到 Qt，不能据此宣称已测试 Windows 系统文件框。
- 本次未运行 Win10 实机；Windows E14 截图中的原生绘制现象也仍需 Windows 实机复验。本地 Cocoa 的 Qt 文件框可自动选择并覆盖；Cocoa 系统文件框未能通过 Qt 接口完成自动选择，未计为通过。

复跑主要回归：

```sh
cmake --build cmake-build-debug --target Sigil
ctest --test-dir cmake-build-debug --output-on-failure \
  -R '^(book_browser_copy_rename_integration|book_browser_replace_integration|main_window_layout_integration|search_memory_integration)$'
SIGIL_NATIVE_TEST_PLATFORM=cocoa python3 tests/run_book_browser_replace_integration.py \
  cmake-build-debug --style qt
SIGIL_NATIVE_TEST_PLATFORM=cocoa python3 tests/run_book_browser_copy_rename_integration.py \
  cmake-build-debug
```
