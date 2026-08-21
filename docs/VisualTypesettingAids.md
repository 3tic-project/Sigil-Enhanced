# 预览网格

预览网格是在 Preview 内容上方绘制的只读参考层，用于检查页面的横向、纵向间距。
网格只属于 Sigil-Enhanced 界面，不会写入 XHTML、CSS、OPF 或 EPUB，也不会产生
undo/redo 项或改变书籍的 modified 状态。

## 当前交付范围

当前界面刻意收敛为纯网格工具：

- “增强 → 预览区网格”只包含“显示网格”和“网格设置…”；
- Preview 右键菜单提供“启用网格”或“禁用网格”；
- 横向线与纵向线可以独立启用，并使用不同间距；
- 设置对话框中的有效变动会立即反映到 Preview；
- 两个方向共同支持 `px` 或固定参考字号的 `em` 单位；
- 主／次网格线、颜色、不透明度和低缩放阈值仍可调整；
- 网格跟随 Preview 的水平、垂直滚动和缩放；
- 设置持久化，并提供简体中文、繁体中文和日文界面。

以下入口暂时隐藏并禁用：

- 检查排版；
- 使用元素字号作为网格参考；
- 显示排版度量；
- 纯净预览。

对应的底层测量代码暂时保留，方便以后重新评估，但历史首选项中的排版度量开关
不会在当前版本恢复运行。

## 使用方法

主菜单入口：

```text
增强
└─ 预览区网格
   ├─ 显示网格
   └─ 网格设置…
```

“显示网格”和“网格设置…”仍可在“首选项 → 键盘快捷键”中配置快捷键。Preview
右键菜单的网格命令会根据当前状态显示为“启用网格”或“禁用网格”，并与增强菜单
中的勾选状态同步。

设置对话框采用实时预览：总开关、方向、间距、单位、原点、颜色和透明度等有效
变动会立即更新 Preview，但此时不会写入首选项。选择“取消”或关闭对话框会恢复
打开前的完整设置；选择“确定”后才持久化当前值。

## 网格设置

默认只启用横向线，间距为 `8 px`；纵向线默认关闭。打开“网格设置…”后可调整：

| 设置 | 行为 |
|---|---|
| Show grid | 总开关，与“显示网格”菜单动作同步 |
| Horizontal grid | 是否绘制横向线 |
| Horizontal spacing | 相邻横向线之间的距离 |
| Vertical grid | 是否绘制纵向线 |
| Vertical spacing | 相邻纵向线之间的距离 |
| Unit | `px` 直接使用 CSS px；`em` 乘固定 Reference font size |
| Reference font size | 两个方向共用的固定 `em` 换算基准 |
| Grid origin | 横向线从文档顶部或 body 内容顶部开始 |
| Grid offset | 横向线在原点基础上的 CSS px 偏移 |
| Major line every | 每 N 条线绘制一条主线 |
| Minor/Major color | 默认颜色跟随 Preview 实际深浅色外观 |
| Minor/Major opacity | 独立控制主线和次线透明度 |
| Minimum zoom | 低于阈值时只显示主线，避免摩尔纹 |

横向、纵向换算后的间距都必须位于 `0.25–1000 CSS px`。损坏或越界的首选项会
恢复为有效默认值。两个方向都关闭时，即使总开关处于开启状态也不会显示 Overlay。

## 坐标与绘制规则

横向线锚定文档纵向坐标，纵向线锚定文档横向坐标：

```text
horizontal phase = (origin + offset - scrollY) × Preview zoom
vertical phase   = (0 - scrollX) × Preview zoom
paint spacing    = resolved CSS spacing × Preview zoom
```

QPainter 负责 logical px 到 device pixel 的映射，代码不额外乘 DPR。线条使用
cosmetic pen，因此 Retina/HiDPI 下不会重复放大线宽。滚动位置直接来自
`QWebEnginePage::scrollPositionChanged`，不使用高频 JavaScript 轮询。

低于 Minimum zoom 时只保留主线；如果主线仍密到不足一个 logical pixel，则暂时
不绘制，避免形成高频纹理。

## 非侵入保证

实现只依赖 Preview、QSettings 和 Qt 绘制组件：

- 不注入 `<style>`、class 或额外 DOM；
- 不修改内存中的 XHTML、CSS 或 OPF；
- 不产生 undo/redo 项；
- 不改变 book modified state；
- 不进入打印 HTML 或 EPUB 导出；
- 切换、滚动和缩放不会触发 Preview reload；
- Overlay 对鼠标事件透明，不影响选择、链接、右键或 Developer Tools。

## 工程结构

| 组件 | 职责 |
|---|---|
| `BaselineGridModel` | 横竖轴纯几何、固定 em 换算、相位、主次线和阈值 |
| `BaselineGridOverlay` | 输入透明的 QPainter 横竖网格层 |
| `BaselineGridSettingsStore` | `preview/visual_aids` 用户设置及旧设置兼容 |
| `VisualTypesettingController` | 增强菜单、设置、Preview 状态和网格协调 |
| `BaselineGridSettingsDialog` | 横竖网格、几何和外观设置 |

Preview 与 `BaselineGridOverlay` 是 `OverlayHelperWidget` 中的兄弟层，尺寸始终相同。

## 测试

```sh
ctest --test-dir cmake-build-debug \
  -R '^(baseline_grid_model|baseline_grid_overlay|visual_typesetting_aids_contract)$' \
  --output-on-failure
```

覆盖内容包括：

- 横向与纵向独立开关、间距和滚动相位；
- zoom 只乘一次、主线索引和低缩放阈值；
- 固定 em 换算、设置往返和损坏设置恢复；
- Overlay 尺寸同步、鼠标透明和双方向关闭时隐藏；
- 增强菜单只保留两个入口，Preview 右键开关正确同步；
- 设置变动实时预览、取消恢复和确定后持久化；
- 排版度量、元素校准和纯净预览入口保持禁用；
- 简体中文、繁体中文、日文翻译目录完整性。

跨平台发布前仍应执行 Windows/macOS/Linux、DPI、Preview Zoom 与横排／竖排 XHTML
的人工视觉检查；自动测试验证坐标公式和组件合同，不能替代全部 GPU/WebEngine
组合的目视确认。
