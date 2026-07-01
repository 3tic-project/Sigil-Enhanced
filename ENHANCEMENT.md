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

## 测试要求

- 至少验证 Debug 构建能通过 `cmake --build cmake-build-debug --target Sigil -j 4`。
- 对批量资源移动类功能，必须测试移动后 OPF manifest、spine、XHTML 链接、CSS url、NCX src 是否仍然可用。
- 对路径相关功能，必须覆盖空格、中文、URL 编码、大小写不一致、fragment、外部链接和不存在目标。
- 对 Automate 命令，需确认命令列表可见且执行行为与菜单入口一致。
