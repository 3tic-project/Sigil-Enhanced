# Windows E14 字体覆盖与搜索布局修复

反馈来自 Windows E14；开发与本地运行环境为 macOS、Qt 6.7.3。

## 同名字体覆盖闪退

`BookBrowser::AddExisting` 接受字体、SVG、音视频覆盖，却在覆盖后无条件使用 `ImageResource` 转型结果。替换字体时该指针为空。现在通知实际被覆盖的资源，不删除或重建资源；路径、资源 ID、OPF 清单及资源属性保持不变。

实际文件选择框回归覆盖取消覆盖、确认覆盖、字体标签页已打开、文件内容确实改变、书籍修改状态以及清单和资源身份保持。旧代码通过此用例确认收到 `SIGSEGV`，修复后通过。

## 搜索展开及重开后的面板宽度

搜索栏保持在编辑器下方，宽度跟随编辑区。搜索模式先初始化，再恢复对应面板的可见性，避免启动时读到未初始化模式或同时展开两种面板；首次显示时，先完成中央区域和主窗口的布局计算，再恢复保存的 Dock 布局。

真实 MainWindow 回归检查两种搜索始终位于编辑器下方，并在容得下搜索控件的布局中反复开关、拉宽预览、保存及从独立进程重新打开，核对图书浏览器与预览的宽度。还覆盖旧设置中两种搜索均标记可见的情况。编辑区过窄时仍沿用原有的 Qt 最小宽度约束。

## 面板标题重复绘制

图书浏览器、目录、验证结果、片段及预览此前自行绘制整个 Dock 标题，绕过 Qt 对原生装饰和自定义标题栏的处理。现在仅通过 `initStyleOption` 提供标题与 macOS 配色，绘制交回 Qt；预览独立于标签页的标题仍然保留。回归检查默认标题、预览自定义标题以及自定义或隐藏标题栏不被再次绘制。

## 验证范围

- Sigil Debug 构建成功；恢复搜索原位置后的 8 项相关 CTest 全部通过，覆盖布局与启动、搜索历史、替换撤销、Code View、Agent 窗口隔离、翻译窗口和字体覆盖。
- Windows 轻量 CI 保持原有的三档缩放启动检查。本次未推送或运行远程 CI。
- 字体测试使用仓库字体和临时 EPUB、隔离设置；未修改用户书籍或应用偏好。离屏模式下原生文件框配置会回退到 Qt，不能据此宣称已测试 Windows 系统文件框。
- Windows E14 截图中的原生绘制现象仍需 Windows 实机复验。本地 Cocoa 的 Qt 文件框可自动选择并覆盖；Cocoa 系统文件框未能通过 Qt 接口完成自动选择，未计为通过。

复跑主要回归：

```sh
cmake --build cmake-build-debug --target Sigil
ctest --test-dir cmake-build-debug --output-on-failure \
  -R '^(book_browser_replace_integration|main_window_layout_integration|search_memory_integration)$'
SIGIL_NATIVE_TEST_PLATFORM=cocoa python3 tests/run_book_browser_replace_integration.py \
  cmake-build-debug --style qt
```
