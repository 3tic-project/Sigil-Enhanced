# Clips 快捷键角标

Sigil-Enhanced 会在 Clips 工具栏的前十个可用按钮右上角显示当前快捷键角标。
默认绑定对应 `1`–`9`，第十项对应键盘上的 `0`，因此显示 **0** 而不是 10。
角标表达实际按键，不是片段在屏幕上的视觉序号。

## 显示规则

| 当前绑定 | 按钮角标 |
|---|---|
| 默认修饰键族加单个数字 | 实际数字，例如 Clip 10 显示 `0` |
| 同一修饰键族改为另一个数字 | 新的实际数字 |
| 其他修饰键、字母键或多段组合 | 空间足够时显示 Qt 的平台原生按键文本；空间不足时显示“Key/按键” |
| 未分配快捷键 | 不显示角标 |

完整组合始终以 `QKeySequence::NativeText` 出现在 tooltip 中，所以 macOS、Windows
和 Linux 使用各自的平台表示，不在界面中写死 `Ctrl+Alt`。修改“键盘快捷键”中的
`MainWindow.ClipN` 后，角标、tooltip 和标准溢出菜单中的快捷键会随同一 QAction
立即更新。

片段被编辑、移动或删除时，按钮继续用 QAction 的固定 `data()` 槽位查询 Clips
模型；空槽位沿用原来的隐藏行为，其他按钮不会因视觉位置而改用别的快捷键。

## 设置与提示

入口为“偏好设置 → 外观 → 主界面 → Clip 工具栏 → 显示 Clips 快捷键角标”。
该设置默认开启，保存后立即作用于当前主窗口；重置外观设置会恢复默认值。

tooltip 第一行是“片段名 · 当前快捷键”，后面是最多 160 个字素簇的片段预览。
HTML、Ruby、`&` 和引号按文字转义，不会被当成 tooltip 标记执行。无绑定时第一行
明确写明“未分配快捷键”。工具按钮的可访问名称包含片段名、固定槽位与完整快捷键，
屏幕阅读器不需要从角标数字推断动作含义。

## 绘制与兼容性

角标是现有 QToolButton 上方的鼠标穿透绘制层，不替换 QAction，不改变点击、Clip
插入、快捷键冲突处理或存储顺序。按钮为角标预留右侧 padding；自定义局部样式、
主题 palette、字体、按钮尺寸、按下和禁用状态变化都会触发安全重绘。字体下限为
9 个逻辑像素，Qt 负责按设备像素比输出。

工具栏过窄时，Qt 的标准溢出菜单继续使用 QAction 的完整快捷键列，不在菜单中重复
绘制角标。

## 测试

```sh
cmake --build build --target Sigil shortcut_badge_model_test action_shortcut_badge_test -j 4
ctest --test-dir build --output-on-failure \
  -R '^(shortcut_badge_model|action_shortcut_badge|clip_shortcut_badge_integration)$'
```

纯模型测试覆盖 1–9/0、同修饰键改数字、其他修饰键、字母、多段组合、无绑定、
原生按键文本、HTML 转义与 emoji 字素簇截断。组件测试使用真实 QAction/QToolButton，
覆盖即时刷新、鼠标穿透、保留区、窄按钮降级、主题/局部样式和设备独立渲染。

macOS Ninja Debug 下还会注册主窗口集成测试。它在隔离偏好设置和合成 EPUB 中覆盖
真实快捷键管理器、前十个工具按钮、设置页即时开关、可访问名称、标准菜单动作、
Clips 模型更新以及触发 QAction 后向真实代码编辑器插入片段。可用下列命令保存工具栏
截图：

```sh
SIGIL_CLIP_BADGE_SCREENSHOT=/tmp/sigil-clip-badges.png \
  python3 tests/run_clip_shortcut_badge_integration.py build
```

当前自动化证据包括 macOS offscreen 下的 100%、125%、150%、200% 缩放，不等同于
Windows/Linux 的原生主题、高对比度或真实屏幕阅读器人工验收。
