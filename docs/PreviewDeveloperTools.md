# Preview 开发者工具

停靠在 Preview 下方的 WebEngine Inspector。这是对原浮动“检查页面”窗口的嵌入，不是第二套 DOM/CSS 检查器。

## 打开

- Preview 工具栏的检查按钮
- `查看 → 开发者工具`
- Preview 上右键 **Inspect Element**（若当前 Qt WebEngine 支持该动作）

再次点检查按钮或关掉 `查看 → 开发者工具` 会**隐藏** DevTools，不销毁 Chromium 页面。再次打开仍是同一实例。停靠时面板本身没有标题栏或关闭按钮。

`查看 → 将开发者工具拆成独立窗口`（Preview 工具栏同一动作）把 Inspector 拆成独立窗口。关掉该窗口只是隐藏，再打开仍是同一实例。`将开发者工具停靠回预览` 把它放回 Preview 下方。

Preview Dock 浮动或重新停靠时，已停靠的 DevTools 跟着 Preview 走；已拆出的独立窗口保持独立。检查/复制/刷新按钮始终在 Preview 窗口底部。Preview 与目录叠在同一 dock 区域时，切走 Preview 不会拆掉 inspected 绑定。

## 重启后恢复什么

只恢复：

- DevTools 是否可见
- 是停靠还是独立窗口，以及对应的几何/splitter 比例

不恢复打开过的 EPUB 文件或检查中的节点。打开书仍只激活第一个 HTML。

## 不会做的事

- 不把 DevTools 里改过的 CSS 写回 EPUB
- 不开启 `QTWEBENGINE_REMOTE_DEBUGGING` 或外部 Chrome
- 不把独立窗口做成可停到主窗口任意边的第二个 Dock
