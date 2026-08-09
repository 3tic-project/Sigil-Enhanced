# Sigil-Enhanced Enhancement 开发约束

本文档记录 Sigil-Enhanced 自带增强能力的入口约束和原生插件开发约定。这里的“原生插件”指随主程序编译、直接使用 C++ 书籍模型和 UI 集成点的内置功能，不是 Sigil 外部 Python 插件，也不是运行时动态加载模块。

## UI 入口约束

- 所有 Sigil-Enhanced 自带的原生增强功能入口统一放在顶层 `Enhancement` 菜单。
- 不再把新的原生增强功能散落到 `Tools`、`Plugins` 或其他主线菜单中，除非它本身是对主线既有动作的兼容性替换。
- 菜单 action 命名使用 `actionFeatureName`，槽函数使用清晰的 PascalCase 动词短语，例如 `NormalizeEpubStructure()`。
- 如果功能适合批处理，可以同时加入 Automate 命令；命令名应与槽函数语义一致，例如 `NormalizeEpubStructure`。

## 代码组织

- 原生增强插件核心逻辑放在 `src/BuiltinPlugins/`。
- `MainUI` 只负责菜单、确认对话框、进度光标、结果展示和调用编排。
- 核心模块应尽量只依赖 `Book`、`FolderKeeper`、`Resource`、`OPFResource`、`UniversalUpdates` 等已有模型能力。
- 不要在核心模块里直接依赖具体窗口类；需要 UI 展示的信息通过结果结构返回。
- 新增源码需要同时加入 `src/CMakeLists.txt` 和 Qt6 构建入口中的 source group。

## 行为安全

- 批量修改必须显式触发，不在打开 EPUB 时静默执行。
- 执行前先 `SaveTabData()`，避免编辑器未保存内容和资源模型状态不一致。
- 涉及 XHTML/OPF/NCX 重写或批量移动前，应先做必要的 well-formed 检查。
- 大范围变更需要确认对话框，并用 Validation Results 或等价结构报告自动修复和需要人工处理的问题。
- 能复用现有资源操作链路时，优先使用 `FolderKeeper`、`BookBrowser`、`OPFModel`、`UniversalUpdates`，避免手写文件移动和正则批量替换。

## EPUB 路径规则

- OPF、XHTML、NCX、CSS 中的 href/src/url 路径是 URL/URI 表示，不是裸文件路径。
- 读取和比对 href 时应先 URL decode，再解析为书内路径。
- 写回 OPF、XHTML、NCX、CSS 时应 URL encode，避免空格、中文等字符以非规范形式写入。
- 处理书内链接时要保留 fragment；外部 scheme、纯 fragment、query 等不能误判为本地资源。
- 大小写修复只能在目标路径可唯一匹配时自动执行，存在歧义时应报告并跳过。

## 当前原生增强插件

### EPUB Structure Normalizer

入口:

- `Enhancement > Normalize EPUB Structure...`
- Automate 命令: `NormalizeEpubStructure`

职责:

- 修复 OPF Manifest 中重复 ID/href、无效 href、大小写不一致和未登记资源。
- 修正 XHTML、CSS、NCX 中可唯一匹配的书内链接大小写。
- 将资源整理到 Sigil 标准目录结构，如 `OEBPS/content.opf`、`OEBPS/Text`、`OEBPS/Images`。

### Formatter Enhancer

入口:

- `Enhancement > Enhance Source Formatting...`
- Automate 命令: `EnhanceSourceFormatting`

职责:

- 使用当前项目内置 EPUB-safe formatter backend 批量格式化 XHTML 和 CSS。
- XHTML 先做 well-formed 检查；不合法的文件不会被重写，而是在 Validation Results 中记录跳过原因。
- CSS 使用当前 C++ CSSParser/serializer readable profile；解析失败的文件不会被重写，而是在 Validation Results 中记录 parser 错误。
- 每个资源的修改、跳过、未变化状态都会写入 Validation Results，便于复查本次操作实际影响。

当前边界:

- 第一版不直接调用 HTML Tidy、Prettier 或 Lexbor。
- 外部 formatter 后端必须作为显式可选 backend 接入，不能改变默认 EPUB-safe 行为。
- 不对 SVG、NCX、OPF、XML 等非 XHTML/CSS 资源做格式化。

### BR Paragraph Normalizer

入口:

- `Enhancement > Analyze BR Paragraphs...`
- `Enhancement > Normalize Current BR Paragraphs...`
- `Enhancement > Normalize BR Paragraphs...`
- Automate 命令: `AnalyzeBrParagraphs`
- Automate 命令: `NormalizeBrParagraphs`

职责:

- 扫描 XHTML 文件中直接挂在 `body` 下、使用顶层 `<br/>` 作为段落分隔的正文流。
- 将可自动规范化正文页、需人工确认短页/版权提示页、目录页、图片/扉页、已有块级布局页和已规范化页面分类写入 Validation Results。
- 当前文件入口允许用户显式确认后转换当前 XHTML，并尽量通过编辑器文档替换保留单步 undo。
- 整书入口只转换 `auto-safe` 正文页；短页、版权/发行信息、阅读提示、目录等只报告并跳过；写回前会自动创建 Checkpoint，因为批量资源写回不进入 Code View undo 栈。
- 转换后逐文件校验 XML well-formed、可见文本等价、`id`/`name` 集合等价、`href`/`src` 集合等价，失败则不写回。
- 转换会保留根 XHTML namespace，但不会在每个生成的 `<p>` 上重复输出 `xmlns`；同时注入 `body.se-br-normalized` 作用域 CSS，避免阅读器默认段落 margin 改变行距。

### KFX Paragraph Normalizer

入口:

- `Enhancement > Analyze KFX Paragraphs...`
- `Enhancement > Normalize Current KFX Paragraphs...`
- `Enhancement > Normalize KFX Paragraphs...`
- Automate 命令: `AnalyzeKfxParagraphs`
- Automate 命令: `NormalizeKfxParagraphs`

职责:

- 扫描 Kindle/KFX/calibre 转换痕迹中“正文文本直接挂在 `body` 下，靠空白 spacer `<p>` 分段”的 XHTML。
- spacer p 按语义识别，不绑定固定 class；判断依据是顶层 `<p>` 仅含空白/NBSP、无业务属性，并带 `height` 样式。
- 将正文裸文本、标题锚点、inline `span`、插图等 body-level run 包装为 `<p>`，移除 `height:0` spacer p。
- 保留 `height > 0` spacer p，降低章前空白、插图前后空白和竖排版式发生变化的风险。
- 生成段落显式继承原正文流的字号、字体、行高、对齐和 writing-mode，并将 margin/padding/border 归零，避免包裹成 `<p>` 后引入阅读器默认段落间距。
- 当前文件和整书修复在写回前会对本次修改的 XHTML 自动应用一次当前 XHTML formatter；不会格式化未参与修复的其它文件。
- 转换后逐文件校验 XML well-formed、可见文本等价、`id`/`name` 集合等价、`href`/`src` 集合等价，失败则不写回。
- 当前文件入口允许人工确认候选显式执行，并尽量通过编辑器文档替换保留单步 undo；整书入口只处理 auto-safe 正文页，写回前自动创建 Checkpoint。

### BookLive / EBPAJ Div Paragraph Normalizer

入口:

- `Enhancement > Analyze BookLive Div Paragraphs...`
- `Enhancement > Normalize Current BookLive Div Paragraphs...`
- `Enhancement > Normalize BookLive Div Paragraphs...`
- Automate 命令: `AnalyzeBookLiveParagraphs`
- Automate 命令: `NormalizeBookLiveParagraphs`

职责:

- 结构化发现 BookLive/EBPAJ 正文 content-parent，将其直接子级的伪段落 `div` 原位转换成语义化 `p`，不依赖混淆 class 名。
- 保留 `div.main` 及中间布局 wrapper，避免丢失纵排、justify、line-height 和书籍私有样式。
- 完整迁移源叶子的 class、inline style 和属性；单层嵌套视觉块转换为保持 block 显示的 `span`，不猜测标题、署名或场景语义。
- 原样保留空行、场景分隔、插图、ruby、锚点和链接，不使用固定间距覆盖原书 CSS。
- 目录、图片/扉页和复杂块布局默认跳过；整书只转换 auto-safe 正文，短页通过 Current 入口人工确认。
- 写回前校验 XML、可见文本、原属性/class/style、`id`/`name`、`href`/`src`、ruby 与图片计数；批量写回先创建 Checkpoint。

### Advanced Regex Workbench

入口:

- `Enhancement > Advanced Regex Workbench...`
- Automate 命令: `RunRegexWorkbenchRecipe <Recipe 名称或绝对路径>`

职责:

- 以顺序 Recipe 提供 PreSearch 范围、主匹配内 Filter 接受/拒绝、受限递归替换和命名捕获变量。
- Dry Run 和 Apply 每次都从当前书籍重新 snapshot；worker 只处理内存文本，不接触 `Resource` 或 `QTextDocument`。
- Apply 确认后在内存中完成全批 stage 与 XML 校验；成功时先创建恢复检查点，再对每个变化资源执行一次可撤销写入。
- Dry Run 不修改书籍或 Session 变量；取消、校验失败、Checkpoint 失败和提交冲突均不发布部分文本或变量。
- Recipe 使用严格的版本化 JSON，可从 Search Editor Plus 的 `PS` 模板导入；工作台不接受 Python `\F` 函数替换。
- Automate 固定作用于全部文本资源，并复用与 UI 相同的 Recipe 解析、staging、验证和提交链路。

使用说明与安全边界见 `docs/AdvancedRegexWorkbench.md`。

## 测试要求

- 至少验证 Debug 构建能通过 `cmake --build cmake-build-debug --target Sigil -j 4`。
- 对批量资源移动类功能，必须测试移动后 OPF manifest、spine、XHTML 链接、CSS url、NCX src 是否仍然可用。
- 对路径相关功能，必须覆盖空格、中文、URL 编码、大小写不一致、fragment、外部链接和不存在目标。
- 对 formatter 类功能，必须覆盖 XHTML well-formed 跳过、XHTML 正常重排、CSS 正常重排、CSS parser error 跳过、无变化文件记录。
- 对 BR 段落规范化类功能，必须覆盖正文候选、目录页跳过、扉页/块布局跳过、`ruby`/anchor 保留、连续 `<br/>` 不生成空段落。
- 对 KFX 段落规范化类功能，必须覆盖正文候选、目录页跳过、版权/出版信息页人工确认、`height:0` spacer 移除、`height:1em` spacer 保留、章节锚点/插图/inline span 保留。
- 对 BookLive 段落规范化类功能，必须覆盖两本真实 EBPAJ 样本、标题/署名不误分类、原 class/style 与布局 wrapper 保留、空行不删除、复杂嵌套 fail-closed、ruby/锚点/图片保持和幂等。
- 对高级正则工作台，必须覆盖二级模式、递归终止、变量时序、Recipe schema、Dry Run/Apply 独立 restage、staged XML 校验、报告坐标门控、Cancel、每资源一次可撤销提交、Automate 接线和三语目录。
- 对 Automate 命令，需确认命令列表可见且执行行为与菜单入口一致。
