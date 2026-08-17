# Preview 开发者工具

停靠在 Preview 下方的 WebEngine Inspector。这是对原浮动“检查页面”窗口的嵌入，不是第二套 DOM/CSS 检查器。

## 打开

- Preview 工具栏的检查按钮
- `查看 → 开发者工具`
- Preview 上右键 **Inspect Element**（若当前 Qt WebEngine 支持该动作）

关闭面板上的 × 或再次点检查按钮会**隐藏** DevTools，不销毁 Chromium 页面。再次打开仍是同一实例。

Preview Dock 浮动或重新停靠时，DevTools 跟着 Preview 走。检查/复制/刷新按钮始终在 Preview 窗口底部，不夹在预览和 DevTools 中间。Preview 与目录叠在同一 dock 区域时，切走 Preview 不会拆掉 inspected 绑定。

## 重启后恢复什么

只恢复：

- DevTools 是否可见
- Preview / DevTools 之间的 splitter 比例

不恢复打开过的 EPUB 文件或检查中的节点。打开书仍只激活第一个 HTML。

## 不会做的事

- 不把 DevTools 里改过的 CSS 写回 EPUB
- 不开启 `QTWEBENGINE_REMOTE_DEBUGGING` 或外部 Chrome
- 不单独做一个可拖到窗口任意边的 DevTools Dock（后续可选）
