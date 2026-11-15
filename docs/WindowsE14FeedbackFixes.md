# Windows E14 字体覆盖与搜索布局修复

反馈来自 Windows E14；开发与本地运行环境为 macOS、Qt 6.7.3。

## 同名字体覆盖闪退

`BookBrowser::AddExisting` 接受字体、SVG、音视频覆盖，却在覆盖后无条件使用 `ImageResource` 转型结果。替换字体时该指针为空。现在通知实际被覆盖的资源，不删除或重建资源；路径、资源 ID、OPF 清单及资源属性保持不变。

实际文件选择框回归覆盖取消覆盖、确认覆盖、字体标签页已打开、文件内容确实改变、书籍修改状态以及清单和资源身份保持。旧代码通过此用例确认收到 `SIGSEGV`，修复后通过。

## 搜索展开及重开后的面板宽度

搜索面板原先与编辑器放在同一列；展开时，其最小宽度会挤压图书浏览器或预览。现在两种搜索面板共用窗口底部整行，隐藏后不留空白；两侧面板宽度不因搜索开关改变。搜索模式先初始化，再恢复对应面板的可见性，避免启动时读到未初始化模式或同时展开两种面板。

真实 MainWindow 回归分别展开、反复收起普通与增强搜索，将预览拉宽，并在独立进程重新打开。还覆盖旧设置中两种搜索均标记可见的情况。旧代码在“展开搜索不得改变侧栏宽度”断言上失败。

## 面板标题重复绘制

图书浏览器、目录、验证结果、片段及预览此前自行绘制整个 Dock 标题，绕过 Qt 对原生装饰和自定义标题栏的处理。现在仅通过 `initStyleOption` 提供标题与 macOS 配色，绘制交回 Qt；预览独立于标签页的标题仍然保留。回归检查默认标题、预览自定义标题以及自定义或隐藏标题栏不被再次绘制。

## 验证范围

- Sigil Debug 构建成功；25 项相关 CTest 分组通过，包含文件选择、面板布局、搜索历史、替换撤销、Code View、Agent 窗口隔离与翻译窗口流程。
- 布局单测在本地 100%、150%、200% 缩放通过。Windows 轻量 CI 已加入相同的三档缩放检查；本次未推送或运行远程 CI。
- 字体测试使用仓库字体和临时 EPUB、隔离设置；未修改用户书籍或应用偏好。离屏模式下原生文件框配置会回退到 Qt，不能据此宣称已测试 Windows 系统文件框。
- Windows E14 截图中的原生绘制现象仍需 Windows 实机复验。本地 Cocoa 的 Qt 文件框可自动选择并覆盖；Cocoa 系统文件框未能通过 Qt 接口完成自动选择，未计为通过。

复跑主要回归：

```sh
cmake --build cmake-build-debug --target Sigil search_panel_layout_test
ctest --test-dir cmake-build-debug --output-on-failure \
  -R '^(book_browser_replace_integration|main_window_layout_integration|search_panel_layout)$'
SIGIL_NATIVE_TEST_PLATFORM=cocoa python3 tests/run_book_browser_replace_integration.py \
  cmake-build-debug --style qt
```
