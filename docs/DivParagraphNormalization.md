# DIV 段落结构规范化

Sigil-Enhanced 可以把确实承担正文段落作用的 XHTML `div` 原位改成 `p`。
它面向 BookLive、Cmoa、EBPAJ/Kadokawa 等转换链中常见的伪段落结构，但判断
不依赖书店名、随机 class 或固定的包装层数。合法的布局 `div` 不是错误，默认
不会被批量替换。

## 使用入口

“增强”菜单提供三个入口：

- “分析 DIV 段落结构…”：选择当前文件、Book Browser 中选中的 XHTML，或全书；
  只生成分析和预览，不写入资源。
- “规范化 DIV 段落结构（当前文件）…”：默认选中当前 XHTML，仍可在对话框中
  改为选中文件或全书。
- “规范化 DIV 段落结构…”：默认使用 Book Browser 选区；没有选区时默认全书。

先选择范围和类别，再点“分析并预览”。结果表把“分类”和“状态”分开显示，
并列出正文候选、空行/分隔、受保护块及 CSS 风险。选择一行可在“源码差异”、
“转换前预览”和“转换后预览”之间切换。只有状态为“可应用”的文件可以勾选；
“需检查”“已跳过”和“解析/验证错误”不会被交互流程强制写入。

## 默认规则与设置

通用交互模式只默认转换“正文段落 DIV”：候选必须处于连续正文流，且后代全部
属于允许出现在 `p` 中的 phrasing content。以下类别默认保持不变，可在运行
对话框或“偏好设置 → 修改版 → 段落结构”中显式开启：

- 只含 `br` 的空行 DIV；
- 场景分隔 DIV；
- 只含图片的 DIV 包装；
- 单层嵌套视觉块。该兼容类别会把外层 `div` 改成 `p`、内层直接 `div` 改成
  `span`，应只用于已知旧 BookLive 工作流。

“转换后格式化 XHTML 源码”也默认关闭。关闭时，正常转换只修改已选元素的
起止标签名，元素内部源码、属性顺序、引号、实体和无关缩进保持原样。开启格式化
是用户独立选择；格式化器可能改变所选标签外的空白和排版。

正文类别的保守检查不能关闭，也没有“强制全部转换”。表格、列表、SVG、MathML、
脚本、未知混合块、固定版式指标、图片/扉页及复杂布局默认保留。标题包装允许由
一个 `h1`–`h6` 加空锚点、注释和结构空白组成；它作为受保护岛保持原样，不再让
相邻的安全正文全部被拒绝。

## CSS 风险门

分析器读取页面内 `<style>`、关联样式表和递归 `@import`，同时支持
`xml-stylesheet` 处理指令、查询参数/片段、循环依赖及注释中的伪 import。
外部、缺失、越界或无法解析的样式表会使文件进入人工检查。

以下情况不能自动证明安全，例如：

- `div.para`、`div > div` 等依赖标签名或关系的选择器；
- `:first-of-type`、`:nth-of-type()` 等同类型位置选择器；
- `div` 与 `p` 的规则并不成对；
- 作者样式没有明确证明 `div` 与 `p` 的段落 margin 等价。

浏览器的默认样式通常会给 `p` 增加上下 margin。因此“保留原 class”本身不足以
证明视觉等价；保守模式要求作者 CSS 提供 `div,p` 的成对 margin 证据，否则转为
“需检查”。当前版本不会猜测计算样式，也不会自动重写共享 CSS。CSS 风险检查是
静态保守门，不等同于在所有阅读器中的像素级视觉证明。

## 保真和事务保证

转换前后会检查：

- 可见字符的文档顺序，包括全角空格、NBSP 和零宽字符；
- Ruby 基字、`rt`、`rp` 的有序结构签名；
- `id`/`name` 和 `href`/`src` 的有序节点映射及重复值；
- 标题层级、标题子树和包装属性；
- 旧版展示属性映射及转换后 XHTML 的 XML 良构性。

每项计划记录资源路径、内容修订指纹、XHTML 与 CSS 的 SHA-256、规则版本、预设、
源码范围和输出哈希。用户在预览后修改目标 XHTML 或相关 CSS 时，旧计划会被拒绝，
必须重新分析。只勾选部分可应用文件时，会以该子集重新生成独立计划，避免从原计划
静默摘取一部分写入。

所有结果先在内存中生成并验证，再经统一批量协调器提交。提交会创建恢复
Checkpoint，每个变更资源形成一个 Code View 撤销步骤；任一写入失败时，已应用的
资源会在进程内回滚。取消范围对话框、分析或预览不会修改 Book。该保证不是断电或
强制终止进程时的跨文件持久化日志，异常退出后的整书恢复仍应使用 Checkpoint。

该功能只改 XHTML，不创建 nav、不重排 TOC、不修改 OPF/manifest/spine，也不删除
空行、厂商 class 或纵排样式。

## Native Agent

内置 Agent 通过 `paragraphs.analyze`、`paragraphs.plan` 和 `paragraphs.apply` 复用
同一分类、样式依赖和计划实现。分析/计划只返回有界摘要与源码差异；apply 重新验证
计划绑定后只创建暂存事务，仍需 `transaction.preview` 和 `transaction.commit`。
不要预先调用 `transaction.begin`。这组工具目前不属于公共 MCP catalog，详见
[Native Agent 原生段落计划工具](AgentNativeParagraphTools.md)。

## Automate 兼容性

既有动作对象、快捷键 ID 和 Automate 命令保持不变：

- `AnalyzeBookLiveParagraphs`；
- `NormalizeBookLiveParagraphs`。

无界面的 `NormalizeBookLiveParagraphs` 明确使用 `booklive-compat-v1` 预设，保留旧版
空行、场景分隔、图片包装、单层嵌套块、样式补偿和格式化行为，但仍采用完整批次计划、
验证、Checkpoint、回滚及每资源撤销。新交互入口使用 `conservative-v1`，两者不要
视为同一默认策略。

## 开发与验证

核心模块分工如下：

- `BookLiveParagraphNormalizer`：DOM 分类、源码范围补丁和语义不变量；
- `DivParagraphCssAnalyzer`：标签选择器与 margin 等价风险；
- `DivParagraphStylesheetResolver`：页面到内联/链接/import 样式依赖；
- `DivParagraphNormalizationPlan`：批量身份、哈希、状态、取消和冲突检测；
- `DivParagraphNormalizationDialog` / `PreviewDialog`：范围、设置、分析表和三种预览；
- `SearchBatchCoordinator`：Checkpoint、逐资源撤销和失败补偿。

定向验证命令：

```sh
cmake --build build --target Sigil -j2
ctest --test-dir build --output-on-failure \
  -R '^(booklive_paragraph_normalizer|div_paragraph_normalization_contract|div_paragraph_performance|div_paragraph_dialog|search_undo_contract)$'
```

性能测试生成 200 个 XHTML、合计约 20.20 MiB 和 66,000 个转换候选。在 macOS
15.7.7、Qt 6.7.3、AppleClang 17、Ninja Debug 的实施基线上，复用单次分析后的批量
计划约为 16.25 秒，测试门槛为 30 秒；回调可以取消并报告进度。该合成数据不是
真实书籍或低配置机器的性能承诺。

当前自动测试覆盖源码精确保留、Ruby/标题/引用顺序、CSS 风险、递归样式解析、
幂等、新增候选、取消、XHTML/CSS 修订冲突、批量身份、20 MiB 性能和真实 Qt
对话框状态。尚未完成随公开仓库分发的原 Cmoa/BookLive 附件回归、Windows/Linux
原生 GUI、人工屏幕阅读器、完整 EPUBCheck、独立阅读器及固定字体/视口的视觉对比；
因此不能把静态 CSS 门描述为跨阅读器视觉验收。
