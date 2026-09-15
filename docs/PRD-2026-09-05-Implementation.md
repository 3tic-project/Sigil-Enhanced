# 2026-09-05 PRD 实施审计

审计基线：`dca3d857a`（`native-agent`）。需求来自本地
`todo/Sigil_Enhanced_PRD_2026-09-05/00…07`；该目录被仓库忽略，本记录独立入库。

## 基线范围与证据

| 功能 | 当前源码证据 | 尚需完成的交付 |
|---|---|---|
| OPF | `ImportEPUB::ReadOPF/LoadInfrastructureFiles`、`OPFResource::SaveToDisk/UpdateText` 均会整理或重建；`Utility` 还会归一化换行/Unicode | 原文快照、局部补丁、诊断与修复分离、缺 nav 查看/显式修复、导出与恢复回归（O01–O12） |
| 文本选择 | `CodeViewEditor::mouseDoubleClickEvent` 仍采用 Qt 词选区和修饰键标签选择 | 正文单元策略、设置、Unicode/分屏/真实鼠标回归（S01–S12）；句子模式按 PRD 可后置 |
| TOC | 基线中的 `EditTOC::MoveLeft/MoveRight` 直接修改模型且没有对话框级历史；现状见下方“TOC 层级编辑” | 跨平台/真实书籍、EPUBCheck、阅读器与进程终止级事务验收 |
| Clips | `MainWindow::UpdateClipButton` 更新文字和 tooltip，没有快捷键角标 | QAction 实际绑定提示、设置、辅助名称、主题/DPI/溢出验证（C01–C10） |
| div | `BookLiveParagraphNormalizer` 及其测试已存在 | 标题包装保护、通用保守预设、CSS 依赖、源码范围补丁、增量幂等、整书事务（D01–D14） |
| Agent | 原生 Agent、Memory/Sigil workspace、Live v2、工具注册、取消/事务及 UI 测试已存在 | 对照真实宿主验证范围/修订/审批绑定/恢复；复用上述原生服务完成三个任务演示（A01–A12） |

原生 Agent 是当前分支已经存在且用户明确允许优化的实现；PRD 基于更早版本提出的“不新建聊天产品”不应导致移除现有功能。

## 分支与提交策略

- OPF：`feature/opf-source-preservation`，独立工作树
  `/Users/parsle/Code/sigil-enhanced-opf-preservation`。
- 导航诊断/显式修复：`feature/opf-navigation-repair`，从上述分支的
  `ba220b1df` 继续，复用该工作树。原 OPF 分支仍保留在依赖提交上。
- 插件/MCP 与原生 Agent 的 OPF 结构化更新：`feature/opf-plugin-source-updates`，
  从导航分支的 `d6a6871ae` 继续，复用同一工作树。
- 原生 Agent 段落计划工具：`feature/agent-native-paragraph-tools`，从已验证的 DIV
  规范化分支继续；当前切片仅注册到 Native Agent，不宣称已进入公共 MCP catalog。
- 原生 Agent 目录层级计划工具：`feature/agent-native-toc-tools`，从段落工具分支继续，
  复用已验证的 `TocTreeTransform` 与 Nav/NCX 原节点写回器。
- 原生 Agent 提供商状态：`feature/agent-provider-readiness-status`，从当前书籍与选区状态
  分支继续，区分本地配置完整性和真实请求结果。
- 原生 Agent 已验证配置状态：`feature/agent-verified-provider-status`，从独立连接测试分支
  继续，用精确配置指纹持久化历史成功证据，并在 Dock 安全显示。
- 其余功能分别创建分支；有依赖的分支从已验证的依赖提交继续。
- 每项拆分为可审阅的算法/集成/验证和文档提交，未验证的项不标为完成。
- 原工作树三处未提交的文本资源加载改动保留，不纳入本分支。

## 验收记录规则

全局 G1–G8 仍待逐项建立证据。纯函数/协议测试不能替代实际导入导出、
Qt 鼠标交互或阅读器回归。EPUBCheck 未运行时必须明确说明。
真实书籍不复制入公开测试夹具；采用合成结构与本地私有样本。

首个实施重点是 OPF：先让原文与解析用表示分离，再统一必要更新的局部补丁；
保留完整需求范围，后续补齐字节/编码保留、缺 nav 与普通导出链路。

## OPF 第一批实现与验证（2026-09-06）

代码提交：`d034cfc79`（原文补丁）、`4a2a922e4`（CR 字符转义）、
`581095efd`（原生入口/设置）、`4be3ccadb`（元数据顺序、标识符、UTC 和原生测试）。

- 默认开启“偏好设置 → 一般设定 → 基本 → 保留 OPF 原始格式与注释”；
  关闭后恢复旧的自动整理行为。简中、繁中、日文文案已补齐。
- 导入/资源保存使用原文表示；旧的 Python 整理链只为解析模型提供规范化输入。
  `OPFResource::UpdateText` 将模型前后差异应用到原文；无变化时不重设 QTextDocument。
- 补丁使用 Expat 的 UTF-8 源码范围，保留无关注释、PI、CDATA、前缀和属性引号。
  模型未表达的扩展不是删除指令。无法定位、非法 XML、含自定义 DTD 实体的
  改写报错，不静默回退全文重建。
- `SetDCMetadata` 保留既有位置，避免重复主标识符；修改时间使用真实 UTC，
  只更新未带 refines 的出版物日期，并去除其重复项。

验证环境：macOS 15.7.7、Qt 6.7.3、Python 3.11.12、AppleClang 17、Ninja Debug。

| 检查 | 结果及证明范围 |
|---|---|
| 完整 `Sigil` 应用构建 | 通过；打包时 42 项固定 Python 依赖与隔离导入检查通过 |
| `opf_source` | 21 项通过：精确字符串差异、改名/排序/增删、扩展、CRLF、CDATA/实体、12,000 项 manifest，以及真实旧 Python 解析链 |
| `opf_resource_integration` | 通过：链接实际应用对象，执行真实 OPFResource/Python/QTextDocument/文件保存；验证标题仅改值、无操作保留 Undo、UTC、refines、重复日期、旧设置行为和解析器复用 |
| `epub_file_snapshot`、`export_metadata_policy`、`plugin_package_update`、`plugin_text_transaction`、`agent_harness`、`agent_book_ops`、`python_package_sync` | 通过；这属于既有能力回归，不证明插件入口已经采用新补丁策略 |
| 三种语言覆盖检查 | 各 82 个已有失败；与原工作树逐项比较，新增失败为 0。主要为原生 Agent 缺译，另有旧 Kfx 文案残留 |

复现：

```sh
cmake --build build --target Sigil -j 8
ctest --test-dir build --output-on-failure -R '^opf_(source|resource_integration)$'
```

原生集成测试当前仅在 macOS + Ninja + Debug 注册。它在临时目录中替换测试
入口并链接已构建的应用对象；独立偏好设置和合成资源，不覆盖 Sigil 可执行文件。

第一批当时未验收（第二批进展见下）：原始文件编码/BOM/混合换行和 Unicode 在普通导出中的字节保真；
完整 EPUB 打开/另存为；缺 nav 的只读临时模型与显式修复；诊断与 modified 分离；
插件/MCP 的 `PluginPackageUpdate` DOM 改写旁路；跨资源失败恢复；性能预算、
三平台 GUI、完整 EPUBCheck 和独立阅读器。不能将本批称为“OPF 完整无损编辑”。

## OPF 第二批：字节读写与原生导出（2026-09-06）

代码提交：`666054f45`（二进制桥接）、`e01e3d2bd`（编码/换行算法）、
`40e251535`（无弹窗错误传播）、`26141b94a`（安全模型投影）、
`4022a8dd4`（资源、导入与导出集成）。

- OPF 保存独立于通用文本写入器，保留原始编码、BOM、混合换行与 Unicode
  组合形式。QTextDocument 继续采用 Qt 的标准换行和 UTF-16 坐标；原文单独保存，
  不向选区/工具 API 混入字节偏移。未修改文件直接复用原始字节。
- 编辑行保留既有换行符，新增行采用附近风格；重复行差异先锚定共同首尾，
  避免插入一行导致未改动后缀的混合换行被重写。
- 保留 UTF-8、UTF-16/32 的 BOM/字节序，识别 XML 声明中的其他编码。
  编码无法表示新字符时拒绝保存；用户明确修改编码声明可以转换为 UTF-8。
  删除没有 BOM 的旧编码声明后按 XML 默认 UTF-8 输出。
- 使用 QSaveFile 原子替换单个 OPF，编码失败时不覆盖旧文件、不清除编辑或 Undo。
  Python 桥接双向传递显式二进制长度；原先零字节截断会破坏 UTF-16。
  资源调用自行传播 Python 错误，不在保存线程或持有 Python 锁时弹出对话框。
- 导入前读取原始 OPF 快照，避免资源构造覆盖解包路径后再取到默认内容。
  manifest、spine、identifier 按命名空间和直接父节点识别，扩展同名节点不再
  被误当成书籍资源。
- 保留模式使用独立的 `model_xml` 结构投影替代旧的 OPF 整理输入：统一解析用
  OPF/DC 前缀，嵌套扩展作为不透明 XML 留在原文，不压平成虚假元数据。
  普通及批量资源更新均使用此投影；关闭保留选项仍走旧路径。

本批验证环境同第一批。完整 Sigil 构建与 42 项打包依赖检查通过；以下
10 项 CTest 全通过：

```sh
cmake --build build --target Sigil -j 8
ctest --test-dir build --output-on-failure -R '^(opf_source|opf_source_bytes|opf_resource_integration|epub_file_snapshot|export_metadata_policy|plugin_package_update|plugin_text_transaction|agent_harness|agent_book_ops|python_package_sync)$'
```

- `opf_source`：25 项通过，新增真实模型投影、嵌套扩展、同名伪 manifest、
  非标准 OPF/DC 前缀与外部实体拒绝测试。
- `opf_source_bytes`：10 项通过，包括 UTF-8/16/32、单字节编码、NFD、非 BMP
  字符、混合换行、插入/删除、12,000 重复行及编码失败。
- `opf_resource_integration`：通过真实 OPFResource 与 EPUB 导入/普通导出。
  合成 EPUB 3 结构包含 UTF-16LE+BOM、CRLF/LF、NFD 和私有扩展；无操作导出
  OPF 字节相同，只改正文时 OPF 仅修改出版物时间戳，nav 和 mimetype 字节相同。
  另验证原生元数据修改、手动编辑、Undo、替代前缀与编码失败后的文件/编辑状态。
  测试遇到意外弹窗会输出内容并失败，不允许靠关闭弹窗继续通过。
- 简中/繁中/日文覆盖检查仍各有 82 项基线失败；与原工作树比较，新增失败为 0。

证明边界：这是合成样本及特定入口的字节对比，不是完整 O01–O12 验收。
UTF-32 等编码算法可往返不代表 EPUB 规范允许所有这些编码；私有扩展样本也
未通过 EPUBCheck 合规认证。O02 的整包 SHA-256 一致要求针对 GUI 无编辑
保存/另存为/保存副本的原始快照复制路径；PRD 允许普通 ExportEPUB 重建 ZIP。
本批证明的是后者的 OPF 成员字节保留，没有执行前者的完整 GUI 验收；
不能用成员字节测试代替 O02，也不能把普通导出重新压缩本身记为缺陷。
跨资源事务回滚、外部冲突、多平台 GUI、阅读器与完整 EPUBCheck 仍待验证。

### 第二批结束时的后续项

- O06/O07：`ImportEPUB::GetBook` 仍自动注册缺失的 nav；结束时仍将警告数量
  直接映射为 Book modified。须把只读诊断、临时目录模型和显式修复计划分离。
  不能只删除创建 nav 的调用：`TOCModel` 与 `NavProcessor` 当前假定 EPUB 3
  有非空 nav，需要一并处理 NCX 查看回退及用户确认后的新增资源/节点。
- O08：插件/MCP 的 `PluginPackageUpdate` DOM 改写仍是旁路，现有插件测试通过
  不表示已经采用源码补丁和修订校验。

## OPF 第三批：缺导航只读查看与显式修复（2026-09-07）

分支：`feature/opf-navigation-repair`。主要提交：
`4107bbc03`（NCX 回退/manifest 预览基础）、`5ba0c7f53`（导入路径所有权）、
`89d64313f`（异步刷新/导航绑定）、`a05f6f924`（导入诊断、修复服务与 UI）、
`d31969fff`（原生交互及输出文件回归）、`affa21a3a`（NCX 标题边界）、
`a53db0819`（保留目录的资源类型校验）。

### 行为与入口

- EPUB 3 缺少 nav 时只报告诊断，不自动创建 XHTML、CSS、manifest 或 spine 项。
  此规则不随“保留 OPF”开关关闭而退回隐式创建；该开关只控制 OPF 格式保留。
  导入绑定已有 nav 不再顺便规范化 manifest，`nav` 按独立属性词识别，
  不把 `scripted-navigation` 等子串误认成导航。
- 目录面板从 NCX 构造只读内存树，保留层级，按 NCX 所在目录解析相对链接。
  没有 nav/NCX 时保持空视图；查询空 nav 不再创建空模板。
  缺失提示提供“查看问题”（打开 OPF 源码）和“生成导航文档…”入口。
- 生成操作先保存当前标签页的待写入编辑，再展示新 XHTML、OPF 前、OPF 后
  三个只读源码页及资源/manifest/spine 摘要，默认按钮为取消。
  预览和取消不生成文件。只有确认后才应用；尚未保存的书籍修改仍在工作副本中。
- 有有效 NCX 时使用其目录；否则从现有 spine 生成以文件名为标题的目录。
  已声明但文件缺失的 XHTML nav 使用原声明；否则新增唯一文件名/ID 和一项
  manifest。修复不新增 CSS，不改变 spine 次序或 linear 属性。
- 修复计划绑定 OPF/NCX 的身份、修订号、源码及资源路径集合；应用时重新规划，
  比较所有预览字段，并重新检查实际链接和片段 ID。篡改计划、过期修订、
  新占用目标、重复应用均拒绝。目标需在书籍内的已有目录，且能由 FolderKeeper
  注册为 XHTML（拒绝 META-INF 等保留位置）；文件创建不覆盖旧目标。
- 目录编辑、从标题生成目录、生成 HTML 目录、封面和索引操作在缺 nav 时先经过
  修复确认；自动化的跳过选择器生成目录入口返回失败，不弹出确认或暗中修复。
  landmarks 编辑提示先生成导航；增删 nav 的 spine 项在 nav 缺失时安全返回。
- 保留模式下 EPUB 3 的 NCX 查看不补写 spine toc/href；无可用 spine 时只诊断，
  不自动重建整个 OPF。加载警告不再直接设置 modified；真实 Clean on Open、
  兼容修复或 OPF 内容变化才标记修改。未改变 XHTML 的 Clean on Open 不算修改。

### 稳定性与性能

- 目录刷新期间的新请求合并为一次后续解析，不发布已过时的结果；关闭模型前
  等待现有工作线程退出。导航指针在必要 manifest 更新成功后才原子发布，
  并通知目录面板重连新导航资源的修改信号。
- spine 回退使用一次资源顺序查询，而不是每个条目重复解析 OPF；验证片段时
  对同一目标文档缓存 ID 集合。本批未测量 PRD 的 2 MiB/1.2 倍性能预算。
- 新增临时字符串导入场景复现 `Importer` 借用已释放路径参数的问题；成员改为
  持有 QString 副本，修复偶发的错误文件名与堆损坏。
- NCX 标题读取按元素边界消费，支持连续文本/CDATA；空 `<text/>` 不会吞掉
  下一条标题，截断 `<text>` 不会无限等待字符。修复拒绝空标题或无效目标。

### 测试证据

环境同前两批；完整 Sigil 构建通过，42 项固定 Python 依赖和隔离导入检查通过。
最终 11 项定向 CTest 各连续运行 3 次通过（33 次执行，35.17 秒）；
此前原生 OPF 和导航集成也各连续运行 3 次通过。

```sh
cmake --build build --target Sigil -j 8
ctest --test-dir build --output-on-failure --repeat until-fail:3 -R '^(opf_source|opf_source_bytes|opf_resource_integration|navigation_repair_integration|epub_file_snapshot|export_metadata_policy|plugin_package_update|plugin_text_transaction|agent_harness|agent_book_ops|python_package_sync)$'
ctest --test-dir build --output-on-failure --repeat until-fail:3 -R '^(opf_resource_integration|navigation_repair_integration)$'
```

- `opf_source` 现为 26 项，覆盖纯函数 manifest 新增、重复 ID/href/nav 拒绝和
  未涉及区域的精确字符串比较；`opf_source_bytes` 仍为 10 项。
- `navigation_repair_integration` 链接真实应用对象，使用真实 ImportEPUB、
  ExportEPUB、OPF/NCX/NavProcessor、目录面板和修复对话框，而非 UI 源码契约测试。
  合成样本包括缺 nav、有/无 NCX、已声明但缺文件、错误目标/片段、仅警告、
  EPUB 2 兼容场景。验证 NCX 层级/相对路径、空值/CDATA/截断标签、刷新合并、
  模型销毁、点击取消/确认、篡改/过期/重放拒绝、目标冲突、保留目录拒绝和
  只读目录创建失败。
- 缺 nav、有/无 NCX、声明已存在三种输入的只读普通导出：资源集合不变，
  OPF 字节一致；显式修复后的普通导出只多一个 nav.xhtml。还原必要修改时间、
  移除唯一新 manifest 项后，OPF 与原文完全一致，保留 UTF-16LE+BOM、混合换行、
  NFD、注释和私有扩展；除 container/OPF 外的原有成员字节相同。
- 简中、繁中、日文新文案齐全；覆盖检查与原工作树对比仍各有 82 项既有失败，
  新增失败为 0。不宣称全仓库的 107 项 CTest 全部通过。
- 原生集成测试仍仅在 macOS + Ninja + Debug 注册。可选
  `SIGIL_NATIVE_TEST_ASAN=1` 只给测试入口和链接增加 AddressSanitizer，
  不会重新插桩应用对象，不能当作整套应用的 ASan 验收。

### 尚未关闭的门槛

本批为 O06/O07 提供合成样本的原生证据，不是 OPF 或 PRD 整体验收。

- 既有 EPUB 2 缺 NCX 自动补建/关联仍保留，并正确标记修改；将其迁移到显式
  修复、手工编辑 OPF 后重绑 nav、未知命名空间 NCX 等异常结构还需独立审计。
- 目前修复拒绝重复/不合适的 nav 声明、缺失目标父目录、不良构 NCX、空标题
  及错误本地链接；需要用户先检查源码，不做推测性批量重建。
- 导航新增不是单独的 OPF 文本 Undo：只撤销 manifest 会留下未登记资源。
  完整跨资源 Undo/Redo、任意提交阶段故障注入和恢复尚未实现。
  当前测试证明计划拒绝、文件创建失败与目标冲突时无变更，**不证明完整 O10/G6**。
- O02 的 MainWindow 保存/另存/副本整包哈希，O08 插件/MCP 进展见下一节；
  O09 的其他外部冲突、O11 全书状态恢复及 O12 元数据对话框取消仍需完整原生入口验证。
- 完整主窗口人工操作、Windows/Linux、真实附件、EPUBCheck、独立阅读器及
  性能预算未验证。新 nav 的局部 XML/链接检查不能证明原书全部合规。

## OPF 第四批：插件/MCP 与原生 Agent 结构化更新（2026-09-07）

分支：`feature/opf-plugin-source-updates`。主要提交：
`c1a6c0650`（结构化 OPF 源码补丁）、`23f744706`（源码身份/修订绑定）、
`1bdce2bdf`（不改 live OPF 的恢复 Checkpoint）、`aff48683e`（真实 Live RPC）、
`2e22e5899`（原生 Agent 冲突预检与真实对象测试）。

### 行为与一致性

- Live v2 的 `transaction.updateMetadata/updateSpine` 不再使用 DOM 全文序列化。
  宿主在安全 `model_xml` 上完成结构变化，再调用同一 `opf_source.apply_model_update`
  写回原始源码。严格无操作保持原文相同；局部更新保留无关注释、PI、CDATA、
  非标准前缀、属性引号、换行和不透明嵌套扩展。歧义 ID/href、namespace 错误、
  畸形 XML 和扩展冲突直接失败，不静默回退重建。
- metadata/itemref 数组是模型可见子项的完整替换，因此省略已建模条目表示删除；
  未建模的嵌套私有扩展不因此删除。spine 重排为匹配 idref 保留 API 未表达的
  扩展属性；同事务新增 manifested 资源仍按 href 确定合并到 manifest。
  `replacePackage` 明确保持“完整权威源码替换”语义，不承诺保留调用方已丢失的格式。
- package 计划同时绑定 OPF resource ID、数值 revision 和精确原始源码；再次 stage、
  preview/validate、创建 Checkpoint 后以及 commit 前都重检。即使数值 revision 尚未
  观察到 GUI 改动，源码不同也触发冲突。preview 的 `opf_changes` 增加前后 UTF-16
  长度与 SHA-256，方便调用方确认实际包变化。
- package 有变化时恢复 Checkpoint 通过隔离临时树读取 OPF 源码字节，不调用会更新时间、
  UUID 或保存编辑器的用户 `RepoCommit()`。缺 UUID 时恢复仓库使用工作区外部 ID，
  不先插入 live OPF；commit 返回 `checkpoint_book_id`。原编码不能表示当前源码时，
  Checkpoint 失败且 live OPF 不变。
- Native Agent 的 `metadata.update`、`spine.set/sort` 及资源结构操作继续复用 Book/OPF
  原生局部补丁入口。事务现额外保存 OPF/NCX 源码身份，并在任何写入前核对已暂存正文
  原文、删除/重命名资源基线；preview 后的宿主修改不会被旧计划覆盖。
  资源 revision 跟踪增加显式初始化状态，初始空文档变为非空时不再漏记变化。

### 测试证据

完整 Sigil 构建通过，42 项固定 Python 依赖和隔离导入检查通过。最终定向命令：

```sh
cmake --build build --target Sigil -j 4
ctest --test-dir build --output-on-failure --repeat until-fail:3 -R '^(opf_source|opf_source_bytes|plugin_package_update|plugin_package_update_integration|plugin_text_transaction|plugin_session_package_integration|agent_workspace_package_integration|agent_book_ops|live_plugin_docs|sigil_mcp_docs)$'
```

上述 10 个目标各连续运行 3 次通过（30 次执行，48.78 秒）。

- `plugin_package_update` 为 15 个纯 Python 用例；另一个同名原生集成目标链接实际
  EmbeddedPython/C++ bridge，验证 no-op、metadata/spine/manifest 局部变化与负向输入。
- `plugin_session_package_integration` 启动真实 MainWindow、PluginSessionManager、
  外部 live launcher 和 SDK。合成 UTF-16LE+BOM/mixed-EOL/NFD/私有扩展 EPUB 中，
  同事务 metadata+spine 更新只改变标题值；恢复仓库的 OPF 字节与提交前完全一致。
  preview 后宿主改 OPF 会得到 `RevisionConflict`；不可编码的恢复快照失败且源码不变。
- `agent_workspace_package_integration` 使用真实 MainWindow/Book/SigilBookWorkspace；验证
  metadata 只改标题、spine 只改 `<spine>`，以及 preview 后宿主分别修改 OPF/XHTML
  都被提交预检拒绝。原生 Agent、Live RPC 两项集成各连续运行 3 次通过。

### 尚未关闭的门槛

本批关闭的是 O08 的核心结构化旁路，并为 O09 的目标源码冲突提供合成原生证据；
不等同于 O01–O12 或整份 PRD 完成。

- Legacy v1 插件显式改写 OPF 后仍可能由兼容容器生成完整 package；只有 Live v2/MCP
  的结构化 `update_metadata/update_spine` 与原生 Agent typed 工具走本批保证的路径。
- 恢复 Checkpoint 对 OPF 使用源码字节；其他已加载文本仍按既有 Unicode 快照方式物化，
  不能把此测试表述为整本 EPUB 每个成员的字节级 Checkpoint。
- Live 提交已有进程内补偿回滚和恢复点，但进程被杀/断电级持久化原子性、任意阶段故障
  注入、跨资源统一 Undo/Redo 仍未完成，不能据此关闭完整 O10/G6。
- macOS Ninja Debug 之外的平台、真实大型书籍性能、完整 GUI 人工流程、EPUBCheck、
  独立阅读器及 O02 整包哈希仍未验证。

## OPF 第五批：Live 提交故障补偿与资源删除（2026-09-08）

分支：`feature/transaction-fault-recovery`。主要提交：
`06d48443f`（提交阶段故障注入与进程内补偿）、`aef09f1cf`（可恢复的批量资源删除）、
`c345e72e5`（二进制/archive/移动故障阶段的真实宿主覆盖）。

### 行为与恢复边界

- Live 宿主增加仅供测试使用的一次性提交故障注入点，覆盖资源新增、移动、OPF batch、
  引用更新、package source、逐个文本、二进制、archive 和托管资源删除之后。生产 RPC
  不暴露该开关。异常和显式注入失败统一进入补偿路径并释放全局 writer。
- 回滚不再因一个资源恢复异常而停止：文本、路径、资源新增清理分别逆序尝试并聚合错误；
  已加入 live Book、但在惰性加载时抛错的新资源也会被登记并清理。只有全部恢复步骤成功时，
  返回给插件的错误才声明“all applied changes were rolled back”。
- 托管资源删除改为先关闭目标标签页、为整批文件建立临时备份，再挂起 watcher 并逐一删除。
  任一删除失败会按逆序恢复已删除文件、原权限、OPF 及此前的文本/结构变化；所有磁盘删除
  成功后才从 `FolderKeeper` 中移除对象。最终删除仍发出原有 `Resource::Deleted` 信号，
  保持关联 XHTML 等监听者的生命周期语义。
- 成功 commit 仍依赖提交前 Checkpoint 提供提交后恢复入口。上述保证是 **Sigil 进程仍存活时**
  的补偿事务，不是跨多个文件的持久化 WAL；在文件删除/替换与补偿之间强制杀进程或断电，
  仍可能留下工作目录的中间状态，需从 Checkpoint 恢复。

### 测试证据

完整 Sigil 构建通过，42 项固定 Python 依赖与隔离导入检查通过。最终代码执行：

```sh
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure --repeat until-fail:3 -R '^plugin_session_fault_recovery_integration$'
ctest --test-dir build --output-on-failure -R '^(navigation_repair_integration|plugin_package_update_integration|plugin_session_package_integration|plugin_text_edit|plugin_text_transaction|plugin_package_update)$'
```

- `plugin_session_fault_recovery_integration` 启动真实 MainWindow、PluginSessionManager、外部
  launcher 和 SDK。扩展后的最终目标连续 3 次通过（19.41 秒）。分别在结构 OPF batch、
  package source、两个文本中的第一个、托管二进制、未托管 archive、移动后的引用更新，
  以及批量删除第一个文件之后失败；验证原 Resource 身份、文件字节、OPF 原始源码/字节、
  Book modified 状态与 writer lease 均恢复。
- 删除用例先事务新增两个非活动 CSS，再在第一个文件删除后失败：第一个文件恢复，第二个
  从未丢失，manifest 精确回退。随后正常事务一次删除两项，确认对象、文件和 manifest
  条目全部移除。相关 6 项 package/navigation/text 回归全部通过（16.10 秒）。
- 测试还验证恢复 Checkpoint 中的 OPF 与提交前字节完全相同，以及失败后新事务可以重新
  获取 writer。原生集成仍只在 macOS + Ninja + Debug 注册。

### 尚未关闭的门槛

- 每个主要 mutation 家族已有真实宿主故障注入证据；新增资源惰性加载自身抛错、底层
  物理写入 API 自然失败，以及补偿操作本身再次失败，仍以代码防护与错误聚合为主，
  尚缺可重复的原生环境注入。
- 删除失败会恢复 Book 内容与磁盘文件，但已关闭的编辑标签页不会自动重新打开；活动 XHTML
  的 UI 状态恢复尚无可重复 GUI 验收。跨资源统一 Undo/Redo 也仍未实现。
- 强制杀进程/断电、恢复日志重放、Windows/Linux 文件占用语义、真实大型书籍、O02 整包哈希、
  EPUBCheck 和独立阅读器均未验证。因此本批推进 O10/G6，但不关闭完整门槛。

## Clips 快捷键角标（2026-09-09）

分支：`feature/clip-shortcut-badges`。代码提交：`ed63ead55`（纯展示模型）、
`4c23e92ac`（QToolButton 绘制层、主窗口/设置/翻译与原生测试）。

### 行为与界面

- 前十个有内容的 Clip QAction 使用实际有效绑定生成角标；默认显示 1–9、0，
  Clip 10 不显示 10。同一默认修饰键族改为其他数字时显示新数字，其他组合显示
  平台原生完整文本或在窄按钮上降级为“Key”。清空绑定即隐藏角标。
- 角标不从按钮视觉位置推导槽位，继续使用 QAction `data()` 与
  `MainWindow.ClipN`；不替换动作对象，不改变 Clip 插入、重排、删除或冲突语义。
  标准工具栏溢出菜单继续从同一 QAction 显示完整快捷键列。
- tooltip 第一行加入片段名和实际原生快捷键，无绑定时明确说明；后续预览按字素簇
  截断并转义 HTML。工具按钮的 accessible name 包含名称、固定槽位与完整快捷键。
- 绘制层鼠标穿透，为按钮保留右侧 padding；响应尺寸、字体、palette、局部 stylesheet、
  按下和禁用状态。字体不小于 9 个逻辑像素，颜色来自当前主题 token。
- 默认开启“偏好设置 → 外观 → 主界面 → 显示 Clips 快捷键角标”，保存后实时刷新，
  重置外观设置恢复默认值。简中、繁中、日文文案已补齐。

### 测试证据与边界

完整 Sigil 构建通过，42 项固定 Python 依赖与隔离导入检查通过。`shortcut_badge_model`
和 `action_shortcut_badge` 通过；后者另以 100%、125%、150%、200% 四个 Qt 缩放因子
逐一执行。`clip_shortcut_badge_integration` 链接真实应用对象并连续运行 3 次通过
（16.33 秒），覆盖真实 MainWindow、QAction、KeyboardShortcutManager、AppearanceWidget、
ClipEditorModel、标准菜单和 CodeViewEditor 插入链。

实际工具栏截图确认十个按钮文字与 1–9/0 角标均可见。集成测试还验证自定义/清空
绑定即时同步、安全 Ruby/HTML tooltip、辅助名称、设置关闭/恢复、模型只剩一项时
其他动作隐藏且 Clip 1 不重编号，以及触发固定 QAction 后插入对应内容。

四份新增翻译目录通过 XML 与 `lrelease`。简中、繁中、日文覆盖检查各仍有 82 条
原生 Agent 历史欠账，新增 Clips 文案失败为 0。未执行 Windows/Linux 原生主题、
真实高对比度、人工屏幕阅读器和实体快捷键键盘布局测试，因此不能把 C08–C10
表述为全平台关闭；自动化 offscreen 缩放与菜单动作检查也不替代完整人工 GUI 验收。

详细用户与工程说明见 [Clips 快捷键角标](ClipShortcutBadges.md)。

下一优先项：分别推进 div 规范化与 Agent 工作流，并继续补齐 TOC/OPF 的
崩溃恢复、全书 Undo/Redo 和跨平台/真实书籍验收；每项另建功能分支并保持细粒度提交。

## TOC 层级编辑（2026-09-09）

分支：`feature/toc-hierarchy-editing`。主要提交：`25f1a42f1`（稳定 ID 纯树变换）、
`aadbe1892`（EditTOC、局部撤销、设置与双导航 UI）、`cf6a360e8`（真实资源集成测试；
后续写回保持、性能与文档提交见该分支历史）。

### 行为、历史与写回范围

- `TocTreeTransform` 以操作前快照统一规划提升/降级；同父多选分别接管到下一选中项前
  的兄弟，父子同选规整为最高祖先，跨父项计划不重叠时原子提交。混入顶层项、无前驱
  区段、未知 ID、无效树或重叠计划时整次失败。默认提升与降级均验证先序 ID 不变；
  设置关闭后的旧提升模式明确允许先序变化。
- EditTOC 初始构建、增项和占位项都分配稳定内部 ID。应用计划后按 ID 恢复选择、展开
  和滚动位置；新范围较小时滚动值钳制到有效上限。超过 1,000 项初始只展开顶层。
- 独立 QUndoStack 覆盖升降级、同级上下移动、增删、标题和目标编辑；快照命令精确恢复
  父子关系、标签与目标。内联 QLineEdit 获得焦点时优先消费平台 Undo/Redo，不混用
  主窗口正文历史。
- “提升时接管后续同级条目”默认开启并持久化。左右箭头、tooltip、accessible name、
  边界禁用和底部结果/失败状态已区分提升、降级与同级排序。简中、繁中、日文已补译。
- 取消只丢弃模型；无变化确定不调用资源写回，`MainWindow` 依据实际写入结果决定是否
  标记 Book modified。EPUB 3 默认只改 Nav；检测到 NCX 时提示其默认保持不变，并提供
  每次默认关闭的“同时同步兼容 NCX”。EPUB 2 直接改 NCX。
- 纯层级操作重挂原 Nav `<li>`/NCX `<navPoint>`，保留节点 id、属性、内联标记、标签、
  target 与 source identity。Nav 只替换 toc 根列表，保留 nav 包装、标题、landmarks、
  page-list、其他 nav 和外围注释；NCX 只替换 navMap，保留 head/docTitle/docAuthor/
  pageList。实际增删或标签/目标变化不能映射时才回退到目录区生成器。
- EPUB 3 缺 Nav 的入口继续复用已实现的 NavigationRepair 预览/确认，不在 EditTOC 内
  静默创建资源。

### 测试证据与边界

完整 Sigil 构建与 42 项固定 Python 依赖检查通过。`toc_tree_transform` 覆盖 T01–T07、
T11 的纯规则与不变量；1,000 项 20 次采样 P95 约 6 ms，10,000 项约 56 ms。
`edit_toc_hierarchy_integration` 链接真实 ImportEPUB、EditTOC、NavProcessor、NCXResource
和 Qt 控件，覆盖稳定多选/展开/滚动、完整 Undo/Redo、输入框快捷键优先、旧提升模式、
边界反馈、取消、无变化确定、实际写入结果、EPUB 2、EPUB 3、双导航显式同步，以及
其他 nav/NCX 外围内容与原节点 identity。真实 10,000 项 EditTOC 提升在同一 Debug
环境测得 150 ms，构造约 1.84 s，且没有逐项 expandAll。

四份翻译目录通过 XML 与 `lrelease`；简中、繁中、日文覆盖检查没有新增 EditTOC
失败，仍继承每种语言 82 条原生 Agent 历史欠账。原生集成目前只在 macOS 15.7.7、
Qt 6.7.3、AppleClang 17、Ninja Debug 执行；没有 Windows/Linux 原生 GUI、真实大型
书籍、EPUBCheck、独立阅读器、人工辅助技术或强杀进程/断电测试，因此不能把 T09–T12
及全局事务门槛表述为全平台完全关闭。详细说明见
[目录层级编辑](TocHierarchyEditing.md)。

## DIV 段落结构规范化（2026-09-09）

分支：`feature/div-paragraph-normalization`。实现按源码补丁、CSS 风险、批量计划、
设置、嵌套块兼容、样式依赖、主窗口事务、性能、对话框与翻译拆分为独立提交；
详细历史以该分支日志为准。

### 行为与安全边界

- 交互入口统一为“分析/规范化 DIV 段落结构”，支持当前、Book Browser 选中和
  全部 XHTML。通用 `conservative-v1` 只默认转换满足 `p` 内容模型的正文叶子；
  空行、场景分隔、图片包装和单层嵌套视觉块均默认关闭并持久化。
- 默认输出使用源范围只替换候选起止标签名，不做 DOM 全文序列化。可选嵌套块
  同样只把外层 `div` 改为 `p`、内层直接 `div` 改为 `span`。全文格式化是独立、
  默认关闭的设置。
- `div > a + h1` 形式的标题包装被识别为受保护岛，不再使相邻正文全部失败；
  `a > div`、列表、表格、SVG、MathML、脚本、固定版式和未知混合结构保守拒绝。
  旧页面幂等标记不隐藏后来新增的候选。
- 保真验证改为有序字符、Ruby 子树、标题子树、`id`/`name`、`href`/`src` 和旧
  展示属性映射；修复了文档根未参与语义文本遍历及集合比较会丢失重复/顺序的问题。
- CSS 分析覆盖页内 style、链接样式、递归 `@import`、`xml-stylesheet`、循环、
  查询/片段及不可用样式表。标签/关系/of-type 选择器或不成对的 `div`/`p` 规则
  转人工检查；保守自动转换还要求作者 CSS 明确提供 `div,p` margin 等价证据。
- 批量计划记录资源 ID、内容修订指纹、XHTML/CSS 前后哈希、规则/预设、范围和
  状态。预览后重新分析用户勾选子集，并在写入前再次核对 XHTML 与 CSS；过期计划
  不会套用旧偏移。
- 写回复用 `SearchBatchCoordinator`：全部结果先生成验证，再创建恢复 Checkpoint，
  每资源产生一个撤销步骤，失败执行进程内回滚。分析、预览取消和空计划不标记 Book。
- 旧 QAction 对象、快捷键 ID 和 Automate 命令名保持不变。无界面的
  `NormalizeBookLiveParagraphs` 使用 `booklive-compat-v1`，保留旧类别、样式补偿
  与格式化行为，同时改用原子批量提交。

### 测试证据与未关闭项

完整 Sigil 构建与 42 项固定 Python 依赖检查通过。定向 CTest 覆盖：严格正文分类、
标题/Ruby/引用/空白保真、复杂内容拒绝、CSS selector 与默认 margin 风险、递归样式
解析、幂等和新增候选、取消、计划身份、XHTML/CSS stale revision、主窗口事务契约、
SearchBatch 撤销/回滚及真实 offscreen Qt 对话框状态。

20.20 MiB、200 文件、66,000 个候选的 Debug 合成基线从重复分析时约 21.82 秒降至
复用分析后的约 16.25 秒，测试门槛为 30 秒。预览对话框以 1180×760 离屏截图检查；
最终表格独立显示分类与状态，源码双栏、三个页签、可勾选范围和 Apply 禁用状态均通过。

四份 `en/zh_CN/zh_TW/ja` 目录通过 XML 和 `lrelease`；三种非英文覆盖检查没有新增
DIV 文案失败，仍只报告继承的 Agent/KFX 目录欠账。当前证据不包括未公开的原
Cmoa/BookLive 附件、Windows/Linux 原生 GUI、人工辅助技术、完整 EPUBCheck、独立
阅读器或固定字体/视口的视觉对比。静态 CSS 门不能替代计算样式和跨阅读器像素验收；
进程内回滚与 Checkpoint 也不是强杀进程/断电级 WAL。因此 D01、D06 及全局跨平台/
真实书籍门槛仍不能标为完全关闭。用户与工程说明见
[DIV 段落结构规范化](DivParagraphNormalization.md)。

## Native Agent 段落计划工具（2026-09-10）

分支：`feature/agent-native-paragraph-tools`。主要提交：`a1d918c44`（原生工具与
controller 注册）、`efc41cd91`（计划范围输入校验）、`fce1e1759`（计划生命周期与
回滚测试）、`58d67e52f`（工具编排 skill）、`900960cf4`（真实 Runner 审批门）。

### 行为与安全边界

- Native Agent 新增 `paragraphs.analyze`、`paragraphs.plan`、`paragraphs.apply`，复用
  `BookLiveParagraphNormalizer`、`DivParagraphNormalizationPlan` 和完整 stylesheet
  resolver，不在模型或适配层复制 C++ 规则。范围省略时分析全部 XHTML；四类可选兼容
  转换保持默认关闭。
- analyze/plan 均只读且可取消。响应提供候选、受保护范围、CSS 依赖、分类、状态、
  前后哈希与有界源码差异；最多返回 128 个范围/依赖和 4,096 code unit 摘录，不把
  整章正文作为计划响应。只有 `apply` 状态资源可进入计划。
- 计划绑定当前 controller/Book 会话、`analysis_id`、`plan_id`、SHA-256
  `plan_digest`、book revision、资源路径与 XHTML/CSS 源码身份。新分析废弃旧计划；
  跨会话、摘要不符、路径/修订/源码/CSS 变化或重建结果不同均在开事务前拒绝。
- `paragraphs.apply` 是 Bulk、可预览工具。它要求独占新事务，重验后只把完整 XHTML
  输出暂存到 workspace，返回 `applied_to_book=false`；活书写入仍由既有
  `transaction.preview` / `transaction.commit` 完成。中途取消或第二个文件暂存失败会
  回滚整批事务。
- Edit 模式在工具启动前走实际 `AgentRunner` 批准门，影响说明包含计划 ID、摘要和
  book revision；Ask 拒绝，Plan 可暂存/预览但不能 commit，Auto 允许。提示与新内置
  `paragraph-normalization` skill 明确禁止预先 `transaction.begin`，避免与独占事务冲突。
- 每个响应明确 `full_epubcheck.status=not_run`；局部校验、暂存、commit 到当前 Book 和
  用户随后保存 EPUB 是四个不同状态。工具目前仅由 `AgentController` 注册，公共 MCP
  仍维持原 38 工具及直接 commit 兼容语义。

### 测试证据与未关闭项

完整 `Sigil` 构建通过，打包阶段 42 项固定 Python 依赖及隔离导入检查通过。定向测试：

```sh
ctest --test-dir build --output-on-failure \
  -R '^(agent_(book_tools|book_ops|harness|typeset|div_paragraph_tools)|booklive_paragraph_normalizer|div_paragraph_normalization_contract)$'
```

上述 7 项通过。新增测试覆盖 alias/schema、只读分析、有界响应、CSS 风险拒绝、
计划摘要与 book revision 绑定、仅暂存、Ruby/标题/空行保留、commit、幂等、跨会话
拒绝、CSS 静默变化、取消及多文件暂存失败回滚；Runner 用真实工具验证 Edit 审批
拒绝不会启动工具或留下事务。Prompt 测试验证 DIV 请求会装载专用 skill 和正确的
独占事务顺序。

截至该切片，已推进 AGENT-IM3 的 DIV 服务与 A01/A03–A07/A09/A12 自动化证据，但没有完成
AGENT-IM1/2 或 G8：尚无独立的计划审阅面板、提交后“尚未保存”结果卡和整任务恢复
按钮；TOC/OPF 计划服务仍未接入，段落服务也未桥接 Live v2/MCP。真实未保存编辑器
正文、书籍关闭/超时、公开附件、完整 EPUBCheck、跨平台 GUI、阅读器视觉比较与
提交后覆盖新人工编辑的恢复冲突仍待验收。详细协议见
[Native Agent 原生段落计划工具](AgentNativeParagraphTools.md)。

## Native Agent 目录层级计划工具（2026-09-11）

分支：`feature/agent-native-toc-tools`。主要提交：`f1b2c39b1`（EPUB3 Nav 层级读取）、
`3874873ef`（controller 会话绑定）、`794c7bc13`（检查/计划工具）、`5cf298e96`
（原生源码保真暂存）、`35465efe6`（真实 Nav/Undo/冲突测试）、`954482327`
（计划重验与 apply）、`a6bf1b753`（完整计划生命周期）、`9b28236c3`（Agent 编排规则）。
后续 `02339d0ff` 明确禁止 EPUB 3 缺 Nav 时把兼容 NCX 当作可写主导航。
`e645fb2a3` 将列表替换范围严格限定在 TOC nav 内，防止复制 landmarks/page-list。

### 行为与安全边界

- `SigilBookWorkspace::toc()` 在 EPUB 3 只读取主 Nav 的 TOC 树，不再扫描所有 `<a>`；
  层级与 landmarks 已正确分离。新增的 `tocHierarchy()` 为 EPUB 3 读取 Nav，为 EPUB 2
  读取 NCX，并为先序节点分配当前快照内稳定 ID。
- `toc.inspect_hierarchy` 只读分页返回节点 ID、父项、深度、标题和目标；默认 100，
  上限 500。`toc.plan_transform` 调用与 EditTOC 相同的 `TocTreeTransform`，支持
  `promote` / `demote` 和提升后接管兄弟，要求节点集合、标题、目标与先序均不变；
  审阅差异最多返回 128 项。
- snapshot 与 plan 绑定当前 controller 会话、book revision、完整目录树和主导航资源
  的 ID/路径/精确源码 SHA-256。检查、计划和 apply 均在读树前后比较源码身份；跨会话、
  旧摘要、修订变化或未触发 revision 的直接 Nav/NCX 修改会在开事务前拒绝。
- `toc.apply_transform` 为 Bulk、可预览工具，要求精确 `plan_id`、`plan_digest` 和
  `expected_book_revision`，自行创建独占暂存事务。它返回
  `applied_to_book=false`，仍须 `transaction.preview` 与 `transaction.commit`；取消或
  暂存失败会回滚。Edit 模式需批准，Ask 拒绝，Plan 可暂存但不能 commit。
- commit 前再次核对事务开始时的精确导航源码和 before 树。写回重挂已有 Nav `<li>`
  或 NCX `<navPoint>`，使用文本资源单步 Undo；Nav 保留节点属性、内联标记、landmarks
  及其他区域，NCX 保留 navMap 外围内容。不会编辑 XHTML 标题、增删条目或改变目标。
- EPUB 3 双导航默认只更新主 Nav，与 EditTOC 默认行为一致；不会隐式同步兼容 NCX。
  工具和 prompt 明确禁止为目录升降级修改 `h1`–`h6`。所有结果明确报告完整
  EPUBCheck 未运行，且 commit 到 Book 不等于 EPUB 已保存。

### 测试证据与未关闭项

完整 Sigil 构建通过，42 项固定 Python 依赖与隔离导入检查通过。以下 9 项定向测试
各连续运行 3 次通过（27 次执行，38.74 秒）：

```sh
ctest --test-dir build --output-on-failure --repeat until-fail:3 \
  -R '^(agent_toc_tools|agent_workspace_package_integration|toc_tree_transform|edit_toc_hierarchy_integration|agent_harness|agent_book_ops)$'
```

`agent_toc_tools` 覆盖层级读取、分页、稳定 ID、提升/降级、兄弟接管、参数边界、权限、
跨会话、revision/source stale、只暂存、预览/提交、提交前宿主冲突、取消和暂存失败
回滚。`agent_workspace_package_integration` 链接真实 MainWindow/Book/NavProcessor，验证
预览不写活书、属性/内联 `<span>`/landmarks 保留、一次 Undo/Redo 精确往返，以及
preview 后直接修改 Nav 不被旧事务覆盖。`edit_toc_hierarchy_integration` 已覆盖相同
NCX 重挂器的真实 EPUB 2 路径和 Nav/NCX 外围内容。

本切片推进 AGENT-IM3 的 TOC 服务与 A01/A03–A07/A09/A12 自动化证据，但不关闭
AGENT-IM1/2 或 G8：尚无独立计划审阅面板、双导航显式同步参数、提交后结果/恢复卡，
也未将计划边界桥接 Live v2/MCP。Native Agent 工作区自身的真实 EPUB 2 NCX 事务、
大型 TOC 响应性能、真实未保存目录编辑器、书籍关闭/超时、完整 EPUBCheck、跨平台
GUI、独立阅读器和进程终止级恢复仍待验收。详细协议见
[Native Agent 原生目录层级工具](AgentNativeTocTools.md)。

## Native Agent 事务状态卡（2026-09-11）

分支：`feature/agent-result-status-cards`。主要提交：`80f3b4352`（统一事务事件状态）、
`1755996d4`（预览/提交/回滚卡）、`7398487ff`（真实 Qt 控件断言）、`1a2ba1979`
（Conversation Markdown 状态导出）和 `92a00bdeb`（四语翻译）。

### 行为与协议边界

- `AgentRunner` 为每个成功 preview 事件记录 `applied_to_book=false`、
  `save_status=not_applied` 和 `full_epubcheck.status=not_run`；commit 事件记录
  `applied_to_book=true`、`save_status=not_saved`，同时保留 workspace 返回的
  `applied_changes` 与 `book_revision`。
- commit 的 recovery 字段明确 Sigil Undo 仅“可用处可用”，且
  `task_restore_point=not_created_by_commit`。该表述只描述本次 commit 的能力，不推断
  会话是否曾单独创建 checkpoint，也不把逐资源 Undo 说成整任务原子恢复。
- Preview 卡分别显示文本长度、新增、重命名、删除、metadata、spine 和 TOC 变更；
  首行始终说明活书未变。Applied 卡明确“已应用到当前书籍，EPUB 尚未保存”、应用数、
  revision、完整 EPUBCheck 未运行及恢复边界。
- `TransactionRolledBack` 现在有独立可见卡片和
  `live_book_unchanged=true`。它只代表提交前暂存事务被丢弃；已经 commit 的内容不会被
  rollback 卡伪装为已撤销。Markdown 会话导出使用相同语义。

### 测试证据与未关闭项

`agent_harness` 用真实 Memory workspace 的 Edit、Plan 与取消路径验证 commit、preview、
rollback 三类事件字段；`agent_dock` 通过 offscreen Qt 控件验证七类预览摘要、提交状态
及可见回滚卡；`agent_provider_catalog` 验证 Conversation Markdown 不丢失保存、校验和
恢复边界。四份 `en/zh_CN/zh_TW/ja` 目录通过 XML、`lrelease` 与 22 条本切片文案的
逐项占位符检查；严格全仓覆盖仍有 77 行继承欠账，本切片文案没有新增失败。

本切片直接推进 A08 的“未保存提示和恢复方式”及 A12 的事务结果可见性，但不关闭完整
AGENT-04、AGENT-IM2 或 A11：尚无成功/失败资源分组、整任务快照、恢复按钮、恢复与后续
人工编辑的冲突预览，也没有在此测试中实际运行完整 EPUBCheck。`python.run` 等绕过暂存
事务的立即写入工具也不产生这些事务状态卡。跨平台原生主题、屏幕阅读器和真实长会话
人工验收仍待补充。用户说明见 [Native Agent](NativeAgent.md#预览提交与恢复状态)。

## Native Agent 当前书籍与选区范围（2026-09-11）

分支：`feature/agent-book-scope-status`。主要提交：`d601384e6`（书籍状态与实时编辑器
范围）、`6ce6fab02`（精确选区源码装配）、`fb0b571e3`（四语状态文案）和
`2e92a3e23`（按芯片限制全书上下文）。

### 行为与性能边界

- AgentDock 新增独立书籍状态行，显示当前 EPUB 文件名、`dc:title`、FolderKeeper
  资源数、Book Saved/Unsaved 状态和 Agent workspace revision。状态字段同时暴露为
  QWidget properties，供自动化在任意界面语言下核验。
- `ModifiedStateChanged`、资源增删移动、`UpdateUiWithCurrentFile`、换书和 Agent 写入会
  刷新完整书籍状态。标签切换刷新当前文件；FlowTab/TextTab 的 `SelectionChanged` 只
  刷新编辑器范围，不在每次光标移动时重复枚举资源或读取 metadata。
- 原实现把 `GetCursorPosition()` 同时作为 start/end，使 Selection 芯片永久禁用；现改为
  读取 `GetSelectionStart/End`，只有非空选区可附加。handle 使用
  `resource_id:start-end` 和 UTF-16 code units，与编辑器及 Live v2 一致。
- `PromptAssembler` 不再把范围 handle 当作资源 ID。它从当前 workspace 的对应 offset
  读取精确源码，保留 Ruby/内联标签；自动上下文上限为 4096 code units，超出时返回
  明确截断说明。解析从最后一个范围分隔模式识别，因此资源 ID 含冒号仍可工作。
- 当前书籍最小 identity 始终进入上下文。只有 Book handle（或无 handle 的兼容调用）
  才附加资源表和最多两个 Spine 样本；File/Selection-only 请求不再泄漏额外全书样本。

### 测试证据与未关闭项

`agent_dock` 验证书名/文件名/资源数/保存状态/revision、非空与折叠选区、动态属性和
发送 handle；`agent_dock_contract` 固定 MainWindow 的 modified、资源、真实选区和信号
刷新链。`agent_harness` 使用冒号资源 ID、5,000 code-unit 前缀及 Ruby 源码验证精确
offset，并验证超长选区限长和 Selection-only 不含资源表。真实
`agent_workspace_package_integration` 链接 MainWindow/Book/ContentTab/AgentDock，验证
导入 EPUB 身份、modified 往返和 `SetSelectionRange(1,12)` 到 handle 的完整路径。
完整 Sigil 构建及 42 项固定 Python 依赖检查通过。四份翻译可生成 `.qm`；9 条新增
GUI source 经实际 `lupdate` 逐项检查无本切片失败，三种非英文目录仍有 76 行继承欠账。

本切片推进 AGENT-IM1 的目标书籍/未保存状态和 A02 的选区上下文，但不关闭二者：尚无
提供商连接健康与可读错误状态、Book Browser“选中文件”范围、显式全书范围选择器或
跨窗口人工验收。自动上下文对超长选区有意截断；测试将真实 UI handle 和内存 workspace
源码读取分别覆盖，尚未通过实际网络模型请求做端到端 Ruby 回显。Agent workspace
revision 也不是所有 GUI 修改的全局递增序号，工具仍依赖资源 revision、state token 和
精确源码冲突检查。用户说明见 [Native Agent](NativeAgent.md#当前书籍与上下文范围)。

## Native Agent 提供商就绪与请求状态（2026-09-11）

分支：`feature/agent-provider-readiness-status`。主要提交：`3d95a80f6`（模型请求完成/
失败事件）、`c7cef64fc`（安全的提供商配置与请求状态行）、`5b0981988`（服务端回显
密钥的边界脱敏）和 `677a3f2e6`（四语状态文案）。

### 行为与安全边界

- `providerReadiness` 只做本地配置检查：endpoint 必须是带主机的 HTTP(S) URL，API Key
  和模型必须非空。状态行只接收是否存在密钥，不接收密钥文本；endpoint 仅保留主机及
  显式端口，去除 userinfo、路径与查询参数。
- 配置完整时显示 `Configured · not tested`，不推断网络、鉴权或模型可用。只有
  `ModelRequestStarted` 才显示正在联系提供商；每个无错误的模型轮次（包括纯工具调用
  响应）记录 `ModelRequestCompleted`，提供商错误记录 `ModelRequestFailed`。取消独立
  显示，不伪装为连接失败，也不会使状态停留在 requesting。
- 请求失败状态把 401/403/404/408/429/5xx 和常见网络错误映射成固定、可翻译的安全
  摘要；Error 卡仍保留底层可诊断信息。`OpenAICompatibleProvider` 在错误进入会话前
  替换配置的 API Key，并对 URL、请求体、响应头尾及错误 trace 做同样处理；Debug 导出
  继续执行第二层递归脱敏。
- Preferences 对话框关闭和每次 Send 前都会重装配置并刷新状态，因此 provider/model/
  endpoint 的修改无需重启应用。

### 测试证据与未关闭项

`agent_harness` 验证成功工具轮次逐次产生 completion、401 失败只产生 failure；
`agent_dock` 用 offscreen Qt 验证 setup/configured/requesting/succeeded/failed/cancelled
及安全摘要；`agent_provider_catalog` 验证无效配置、IPv6+端口、URL 敏感部分剥离，并用
本地 HTTP 401 服务实际回显测试密钥，证明会话错误与内存 trace 均已脱敏。
`agent_dock_contract` 固定 Preferences/Send 的刷新接线。完整 Sigil 构建与 42 项固定
Python 依赖检查通过。四份目录的 23 条新增文本逐项匹配、占位符一致且可生成 `.qm`；
严格非英文覆盖仍只有 76 行继承欠账，本切片没有新增缺失。

本切片继续推进 AGENT-IM1，但不将其整体关闭：尚无独立 Chat Completions“测试连接”
操作、最近请求时间/延迟、错误重试入口、跨窗口人工验收或 Windows/Linux 网络与主题
验证。模型列表刷新成功也不等同于对话 endpoint 已验证。Book Browser“选中文件”范围、
显式全书范围选择器及端到端真实模型 Ruby 回显仍属于上一切片列出的未关闭项。用户说明
见 [Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

## Native Agent 显式任务范围（2026-09-12）

分支：`feature/agent-selected-files-scope`。主要提交：`5baa018f3`（Book Browser 多选
接线与互斥范围）、`a9f2481a9`（选中文件上下文限流）和 `83c29a5f0`（四语范围文案）。

### 行为与性能边界

- AgentDock 将原来可叠加的 Book/File/Selection 芯片改为互斥的 Selection、Current
  file、Selected files、Whole book。Whole book 对应唯一的 `book` handle；另外三种
  范围只附加目标资源，不再暗中包含资源表和 Spine 样本。
- 默认规则与 PRD 一致：存在非空编辑器选区时选 Selection，否则选 Current file；两者
  不可用时才使用 Book Browser 选择或 Whole book。用户手动选择的范围会保持，直到该
  范围失效才重新执行默认规则，因此光标移动不会覆盖显式的全书/多文件选择。
- BookBrowser 公开 `SelectedResourcesChanged`，其底层 selection model 通知通过零延迟
  单次定时器合并；程序化批量选择不会为每一行重复枚举资源或刷新 Agent。MainWindow
  读取当前资源 ID/路径，保持 Book Browser 顺序，AgentDock 再按 ID 去重。
- Selected files 会把每份当前内存资源的开头片段加入 prompt；自动读取上限为 60 份，
  超出时在上下文中明确写出省略数量并指向 `resource.read_fragment`。这限制了大规模
  多选的提示词增长，同时不把未读资源说成已经附加。

### 测试证据与未关闭项

`agent_dock` 验证选区优先、折叠后回退当前文件、四范围互斥、去重、手动选择保持及
handle 精确集合；`agent_harness` 用 65 个真实 Memory workspace 资源验证只附加前 60
份、报告其余 5 份且不泄漏全书资源表。`agent_dock_contract` 固定 BookBrowser 合并信号
和 MainWindow 接线。`agent_workspace_package_integration` 链接完整宿主，在真实导入
EPUB 的 Book Browser 同时选择 XHTML/Nav，确认两个 ID 按序进入 Agent，并由
`PromptAssembler` 读取当前内存源码而不附加全书表。完整 Sigil 构建和 42 项固定 Python
依赖检查通过。四语 10 条新增文案可生成 `.qm`，旧 `Book · %1` 条目已移除；严格三种
非英文覆盖降至 73 行继承欠账，本切片范围文案新增失败为 0。

本切片补齐 AGENT-01 的四种范围和默认选择规则，并推进 A01–A03；仍不关闭完整
AGENT-IM1：尚无任务开始后冻结/展示不可变范围快照、书籍关闭时的运行取消集成测试、
独立连接测试与请求延迟信息。Selected files 自动片段有意限流，超过 60 份的任务必须让
模型继续按 ID 读取；尚未在包含大量二进制资源的真实书籍上做性能基准。用户说明见
[Native Agent](NativeAgent.md#当前书籍与上下文范围)。

## Native Agent 运行与书籍会话绑定（2026-09-12）

分支：`feature/agent-run-book-binding`。主要提交：`fdd10d22d`（书籍会话身份、取消原因及
Runner fail-closed 检查）、`fc9270142`（Controller/MainWindow 生命周期延迟和状态卡）、
`e7682f3f8`（四语运行状态文案）和 `7d3a579fc`（拒绝 Controller 重入变更）。

### 安全边界

- `IBookWorkspace::bookSessionId()` 为一次工作区绑定提供不透明 UUID；Memory 与真实 Sigil
  工作区的 `summary()` 均公开 `book_session_id`。`SigilBookWorkspace::setBook()` 每次
  重绑都换发 ID，即使宿主指针恰好相同，旧计划也不能复用。Dock 显示前 8 位，协议事件
  保留完整值。
- Runner 在每轮开始冻结 ID，并在 Provider 返回后、每个工具前后复核。直接重绑而没有
  UI 取消时会产生 `book_target_changed` / `BOOK_TARGET_CHANGED` 并在首个工具请求前失败；
  回滚只允许作用于仍匹配的工作区，绝不对替换后的书执行旧事务清理。
- MainWindow 的正常换书路径在 `setBook()` 前以 `book_changed` 请求取消。关闭窗口若运行
  尚在模型网络或批准门的嵌套 Qt 事件循环中，先 ignore close、请求 `window_closing`，待
  `send()` 返回后再排队关闭，保证 `WA_DeleteOnClose` 不会释放调用栈仍在使用的对象。
- Controller 在活动运行中拒绝 Provider、workspace/Runner 替换和递归 `send()`；Preferences 产生的新配置延后应用。
  New Session 在 UI 中运行期间禁用，Controller 层仍做延迟清理：只先设置 `new_session`
  取消，旧 Runner 退栈后才清除事件、取消标记和工具实例。模式、模型和 thinking 配置由
  Controller 持有，重建 Runner 后不会丢失。

### 测试证据与未关闭项

`agent_harness` 在 Provider 回调内直接换发 Memory workspace 会话 ID，验证旧工具调用在
`ToolRequested` 前被拒绝、没有事务或写入；同一回调还请求 New Session 并尝试替换
Provider/workspace 并递归 send，验证三者均失败且清理只发生在 Runner 返回后。`agent_dock` 覆盖运行中控件锁定、
书籍会话状态和专用结果卡；`agent_dock_contract` 固定换书取消顺序、关闭延迟和 Provider
延迟。`agent_workspace_package_integration` 在真实导入 EPUB 上验证 Dock ID、workspace
summary 以及重绑换发 ID。完整 Sigil 构建与 42 个固定 Python 依赖通过。四语 7 条变更
文案均可生成 `.qm`；严格三种非英文覆盖仍为 73 行继承欠账，本切片新增失败为 0。

本切片关闭“旧模型响应被重定向到新书”和嵌套事件循环销毁 Runner/窗口的实现风险，但
尚未做真实慢速网络请求下的人工换书/关闭压力测试，也没有 Windows/Linux 事件循环验证。
当前范围 handle 集合在 `send()` 参数中固定且书籍身份已冻结；Dock 尚未提供可展开的完整
技术详情面板来展示完整 UUID。用户说明见
[Native Agent](NativeAgent.md#运行与书籍会话绑定)。

## Native Agent 请求诊断与安全重试（2026-09-12）

分支：`feature/agent-request-diagnostics`。主要提交：`62c31ac38`（请求身份与耗时事件）、
`bfa9e5d14`（折叠技术详情）、`48a5bc572`（绑定范围的失败重试）、`131813648`（四语诊断
文案）、`b38c87c8e`（禁止工具执行后的危险重试）和 `41e16d6f7`（重试限制翻译）。

### 请求可观测性

- Runner 为每次模型步骤生成 UUID `request_id`。Started 事件记录 Agent session、冻结的
  book session、当时的 book revision、step、model、mode、thinking、工具数和完整
  context handles；Completed/Failed/Cancelled 用相同 ID 配对并记录
  `provider.stream()` 墙钟耗时。取消不再只能从通用 SessionCancelled 推断，而有明确的
  `model_request_cancelled` 终态。
- Provider 状态的成功、失败、取消现在显示该模型步骤的毫秒耗时和完成时间。该数字不包含
  prompt 组装和后续工具执行，不能解释为整轮任务耗时。
- Dock 新增默认折叠的 Technical details，显示完整 session、request、当前/请求目标
  book-session ID、revision、模式、模型、状态、耗时及发送时范围。安全边界仍只显示
  endpoint host；API Key、URL 路径/查询和响应正文不会进入详情。

### 重试安全边界

- Dock 在 Send 时保存实际提交的用户文本、互斥范围 handles 和完整 book session ID，
  不在失败后重新读取可能已改变的芯片。Retry 触发一轮新的 `sendRequested`，由 Runner
  重新读取当前内存源码，不复用原 HTTP body 或旧计划。
- Retry 只在首个 Provider 请求失败、没有活动运行且 book session 仍完全一致时启用。
  换书或 New Session 会失效；点击后立即禁用，避免双击重入，连续失败才重新开放。
- step 2 及更晚的模型失败不会提供 Retry，因为本轮可能已经执行或提交工具；无条件重跑
  用户原指令可能重复已有副作用。用户必须检查已应用/回滚卡，再显式发起后续操作。

### 测试证据与未关闭项

`agent_harness` 验证多步请求的 started/completed ID 一一对应、session/book/revision/mode
和非负耗时，并覆盖 Provider 内取消的独立 timed outcome。`agent_dock` 验证状态时间、
完整技术字段、默认折叠/展开、精确文本与 handles 重试、连续失败、换书/New Session
失效，以及 step 2 失败禁用；`agent_dock_contract` 固定这些宿主边界。完整 Sigil 构建与
42 个固定 Python 依赖通过。四语 20 条新增文案均可生成 `.qm`；严格三种非英文覆盖仍为
73 行继承欠账，本切片新增失败为 0。

本切片补齐 AGENT-IM1 中最近请求时间/延迟、技术详情和受限错误重试，但仍没有独立的
Chat Completions“测试连接”操作，也没有全任务阶段耗时、token 用量或网络分段指标。
真实慢速/断网 Provider、Windows/Linux 本地化时间显示、屏幕阅读器和长 ID/大量 handles
的人工布局仍待验收。用户说明见
[Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

## Native Agent 冲突安全的文本任务恢复（2026-09-12）

分支：`feature/agent-task-recovery`。主要提交：`10007ec10`（任务恢复点协议与冲突保护）、
`80e377426`（纯文本事务自动快照/封存）、`58bbd0a36`（恢复按钮、Controller 与导出事件）
和 `c448227a0`（四语恢复文案）。

### 恢复协议与边界

- Runner 在执行 `transaction.commit` 前读取事务预览。只有全部变更均为既有文本资源，
  且不含资源新增/删除/重命名、metadata、Spine 或 TOC 结构变更时，才为本次 commit
  自动创建 guarded task restore point；失败提交会丢弃未封存恢复点。
- 恢复点保存受影响资源的提交前文本和路径；commit 成功后再封存同一资源的提交后精确
  文本和路径。Applied 事件以 `recovery.task_restore_point=available` 公开 checkpoint、
  冻结的 book session、资源集合和 `post_commit_text_and_path` 冲突门。结构提交返回
  `unavailable/structural_changes`，不会把不完整的文本快照冒充整包恢复。
- 专用恢复入口先验证书籍会话、无活动 Agent run、无暂存事务、恢复点已封存且未使用；
  然后对所有目标做完整预检。路径、存在性或内容任一不匹配即返回
  `TASK_RESTORE_CONFLICT` 和逐资源原因，设置 `live_book_unchanged=true`，不执行部分写入。
  无关资源不在快照或校验集合中，因此任务后的无关人工编辑会保留。
- 自动恢复点不能经普通 `checkpoint.restore` 使用，避免模型绕过专用冲突门。成功恢复用
  Sigil 的 undoable text edit 写回全部目标、递增 Agent book revision，并把按钮结算为
  Restored；同一点只能成功恢复一次。切书清除 workspace 内恢复点，旧卡还会被完整
  book-session 校验拒绝。

### 测试证据与未关闭项

`agent_book_tools` 覆盖双资源成功恢复、普通 checkpoint 绕过拒绝、一次性语义，以及一份
目标后续修改时整批零写入；`agent_harness` 验证 Runner 自动发布可用恢复点、结构 commit
明确不可用、Controller 拒绝跨书卡并发布完成/失败事件；`agent_dock` 验证按钮绑定、运行/
换书失效、冲突可见与成功结算。`agent_workspace_package_integration` 在完整宿主和真实导入
EPUB 上提交两份 XHTML/Nav 文本，证明冲突时另一文件不被部分恢复，冲突内容回到提交后
状态后可恢复精确原文。完整 Sigil 构建与 42 个固定 Python 依赖通过。四语 15 条新增文案
可生成 `.qm`；严格三种非英文覆盖仍为 73 行继承欠账，本切片新增失败为 0。

本切片直接推进 AGENT-04、AGENT-IM2 和 A11，但不将完整任务恢复关闭：资源增删/重命名、
metadata、Spine、TOC 和二进制的整包恢复仍需接入可精确寻址的宿主 repository checkpoint，
并为其建立等价的提交后冲突清单。恢复点目前不持久化，进程崩溃、关闭后重开、跨平台
GUI、磁盘保存后的恢复及大型书籍内存/延迟基准仍待验收。用户说明见
[Native Agent](NativeAgent.md#预览提交与恢复状态)。

## Native Agent 独立 Chat Completions 连接测试（2026-09-13）

分支：`feature/agent-connection-test`。主要提交：`65c55682b`（有界探测协议与 Provider
超时修正）、`8e8fb9fa3`（偏好设置探测入口）和 `47e7b27c8`（设置页完整四语翻译）。

### 协议与界面边界

- `AgentConnectionProbe` 复用生产 `OpenAICompatibleProvider`，但构造独立最小请求：只有
  固定的 `Reply with exactly OK.` 用户消息，不附加书籍上下文或 tools，强制关闭 thinking，
  设置 `max_tokens=8`，并把设置页等待限制为 15 秒。它只返回成功、模型、finish reason、
  HTTP 状态和墙钟耗时，不把模型正文或 API Key 带回界面。
- `ModelRequest` 新增可选 `maxOutputTokens` / `timeoutMs`，默认值保持正式 Agent 的既有
  120 秒行为。Provider 现在区分用户取消和定时器中止；后者返回明确的
  `Request timed out after … ms` 并进入脱敏 trace，不再因空 decoder 默认 finish reason
  被误判为成功。
- 偏好设置新增 **Test Chat Completions**。发网前用 `providerReadiness` 验证 HTTP(S)
  endpoint、非空 API Key 和模型；运行时冻结提供商、URL、密钥、模型、刷新、thinking
  和 effort 控件，结果状态公开 testing/succeeded/failed、HTTP status、安全 host、模型
  和耗时。字段改变会作废旧结果；测试使用当前表单但不写 SettingsStore。
- 探测不写 AgentSession，因此不会污染书籍对话、调试导出或把 Dock 最近请求伪造为成功。
  提示明确披露服务商可能对最多 8 个输出 token 计费。

### 测试证据与剩余项

`agent_provider_catalog` 使用三个本地 TCP 服务验证成功 SSE、401 回显密钥和无响应超时；
它逐字段断言 POST 路径、Bearer 鉴权、模型、stream、`max_tokens=8`、thinking disabled、
无 tools，证明 401 结果脱敏且 120ms 超时为失败。`agent_dock_contract` 固定设置页本地
校验、15 秒探测、无书籍/工具披露和可检查状态。完整 Sigil 构建与 42 个固定 Python 依赖
通过。四份目录的 10 条新增探测文案均可生成 `.qm`；同时补齐设置页原有 25 条欠账，
严格简中/繁中/日文覆盖从每种 73 行降至 47 行。

本切片关闭此前记录的“无独立 Chat Completions 测试入口”，继续推进 AGENT-IM1，但不把
整体连接体验关闭：测试结果尚不跨偏好设置会话持久化，Dock 仍只显示真实书籍请求状态；
真实 DeepSeek/OpenCode Go/OpenRouter 联网、代理/证书错误、Windows/Linux、关闭偏好设置时
的嵌套事件循环和屏幕阅读器人工验收仍待补充。用户说明见
[Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

## Native Agent 已验证提供商配置状态（2026-09-13）

分支：`feature/agent-verified-provider-status`。主要提交：`87daf9228`（配置指纹和设置存储）、
`a6ef362ac`（设置页记住成功探测）、`7795e369d`（Dock 历史验证状态）和 `e74d51816`
（四语文案）。

### 证明绑定与界面语义

- 成功探测只在设置页内暂存；偏好设置关闭并执行既有保存流程时，才与当前表单配置一起
  写入成功时间和 SHA-256 指纹。指纹覆盖 provider、规范化后的 Chat Completions URL、
  API Key 与模型，持久化值不包含可读密钥或响应正文。
- `AgentSettings::verifiedConnectionAtMs()` 每次都从当前已保存配置重新计算指纹；任一字段
  由设置页或其他入口改变后，即使旧记录仍存在也返回未验证。字段编辑、切换 provider 或
  失败重测还会清除当前表单内待保存的成功证明。
- 重开设置页会为完全匹配的配置显示历史成功时间。Dock 在尚无真实 Agent 请求时显示
  **Chat tested successfully · 时间**，并公开 `verified` / `connectionVerifiedAtMs` 控件
  属性供辅助技术和测试检查；tooltip 明确这是历史测试，不是实时连接状态。
- `ModelRequestStarted/Completed/Failed/Cancelled` 仍优先驱动请求状态，连接测试不写
  AgentSession、不伪造最近书籍请求，也不改变既有重试语义。

### 测试证据与剩余项

`agent_provider_catalog` 验证等价 endpoint 形式得到同一 64 位十六进制指纹，且 provider、
API Key、模型任一变化都会失配，指纹不包含密钥原文；`agent_dock` 验证未测试/历史验证及
真实请求覆盖链；`agent_dock_contract` 固定设置保存和 MainWindow 重算边界。完整 Sigil
构建通过，13 项 Agent 测试连续 3 轮共 39 次通过。四份翻译目录可生成 `.qm`，本切片 5 条
现行文案均已翻译；严格简中/繁中/日文覆盖仍各有 47 条上一分支已记录的继承欠账，本切片
新增失败为 0。

本切片关闭上一节的“测试结果不跨偏好设置会话持久化、Dock 无法显示设置页证明”缺口，
但不声称当前在线：网络、凭据或服务端模型仍可能在历史测试后变化。尚未完成真实三家服务
联网、凭据轮换人工检查、系统密钥链存储、代理/证书错误、Windows/Linux 本地化时间、
屏幕阅读器与偏好设置嵌套事件循环压力测试。用户说明见
[Native Agent](NativeAgent.md#提供商配置与最近请求状态)。

## Native Agent 界面翻译覆盖收口（2026-09-13）

分支：`chore/agent-translation-coverage`。主要提交：`7e36e178c`（四语目录收口）。

- 补齐 `SigilAgent::AgentDock` 的 39 条基础界面文案，包括模式、Composer、范围、审批、
  工具状态、导出与停止/回滚反馈；同时补齐 MainWindow 的 4 条 Agent 导出文件对话框文案、
  PluginSession/PluginSessionManager 的 2 条实时插件错误和 QObject 的 1 条导出错误。
- 从简中、繁中、日文目录删除已不在当前源码中的
  `KfxImportController: Cannot create a temporary EPUB file: %1` 活跃旧条目。英文精简目录也
  同步加入上述 46 条现行源文，避免新界面只依赖运行时 fallback。
- 覆盖测试使用 Qt `lupdate` 从当前 `cpp/h/ui` 重新提取源文，并检查 context、unfinished、
  空译文、占位符、富文本标签、简中误混日文假名及未包裹 `tr()` 的常见控件字面量。

XML 解析通过；四份目录均可由 `lrelease` 生成 `.qm` 且 0 unfinished。简中、繁中和日文
严格覆盖测试全部通过，每份覆盖当前 5,646 条活跃源文；此前连续记录的每种 47 条继承欠账
已关闭。本切片只改变翻译目录，不改变 Agent 协议或运行时逻辑。真实三语言 GUI 的截断、
字体回退、输入法、屏幕阅读器和 Windows/Linux 菜单布局仍需人工验收。

## Native Agent 非阻塞连接测试（2026-09-13）

分支：`feature/agent-async-connection-test`。主要提交：`8818349c9`（可取消探测协议）和
`ed378dfee`（设置页后台执行与生命周期保护）。

- `AgentConnectionProbe` 接受可选的线程安全取消标记；专用 sink 通过 Provider 既有的
  50 ms cancel poll 中止 `QNetworkReply`，并返回失败的 `cancelled` 终态，不等满 15 秒。
  正式 Agent 请求接口和默认超时未改变。
- 设置页用 `QtConcurrent::run` 执行完整探测，`QFutureWatcher` 只在 GUI 线程读取结果并更新
  控件。配置按值捕获，工作线程不读取 QWidget；测试期间仍冻结连接字段，避免结果绑定到
  中途改变的表单。
- 设置页析构时只设置共享原子取消标记。Future 捕获共享所有权，取消指针在工作线程退出前
  始终有效；QObject 自动断开已销毁页面的 finished 回调。旧的全局等待光标和 GUI 线程内
  的嵌套网络事件循环已移除；Provider 的有界事件循环只在工作线程运行。关闭窗口不会留下
  悬空回调或卡住全局光标。

`agent_provider_catalog` 用本地 SSE 服务在 Qt 线程池中完成真实 POST，并以主线程 timer
证明事件循环仍可响应；另用无响应服务证明外部取消在 1 秒预算内返回，而不是等待 5 秒
测试超时。`agent_dock_contract` 固定 Future watcher、线程池调用和析构取消边界。完整 Sigil
构建通过，13 项 Agent 测试连续 3 轮共 39 次通过；简中、繁中、日文严格覆盖测试全部通过，
四份 `.qm` 保持 0 unfinished。

本切片关闭设置页 15 秒嵌套事件循环的已知风险，但请求一旦发出仍可能产生服务商计费；
关闭窗口只能尽快中止本地请求，不能撤回服务端已经处理的内容。真实高延迟代理、DNS/TLS
卡顿、线程池饱和、Windows/Linux 关闭窗口竞态和辅助技术人工验收仍待补充。

## Native Agent 非阻塞模型目录刷新（2026-09-13）

分支：`feature/agent-async-model-catalog`。主要提交：`ac89b71a6`（目录取消和密钥脱敏）、
`ecb425418`（设置页后台刷新）和 `3e87ab7ea`（四语加载状态）。

- `AgentModelCatalog::fetch` 新增可选原子取消标记和 50 ms cancel poll，明确区分外部取消与
  30 秒超时。服务端错误正文、解析错误和结果 `sourceUrl` 在离开目录边界前替换当前 API
  Key，避免模型列表接口成为设置页密钥回显旁路。
- **Refresh models** 通过独立 `QFutureWatcher<CatalogResult>` 在线程池执行。URL、API Key、
  OpenRouter headers、provider kind/id 均在启动时冻结；Provider 默认能力和缓存归属使用
  同一快照，不从完成时可能变化的表单重新推断。
- 模型刷新与 Chat Completions 测试互斥，期间统一冻结连接控件并显示可翻译的
  **Loading models…**；完成状态公开 `modelRefreshState` / `modelRefreshHttpStatus`。设置页
  析构会取消两类后台请求，已销毁 QObject 不接收 finished 回调。

`agent_provider_catalog` 用本地模型服务证明后台 GET 成功时主事件循环仍可响应，验证参数
解析和缓存来源；401 服务证明错误与结果元数据不含密钥原文；无响应服务证明取消不等待
5 秒测试超时。`agent_dock_contract` 固定异步、互斥、状态与析构取消边界。完整 Sigil 构建
通过，13 项 Agent 测试连续 3 轮共 39 次通过；简中、繁中、日文严格覆盖测试全部通过，
四份目录均可生成 `.qm` 且 0 unfinished（非英文目录各覆盖 5,647 条活跃源文）。

本切片关闭设置页模型刷新阻塞与目录错误密钥回显风险，但目录接口仍因服务商而异；成功
只说明列表可读取，不代表所选模型支持 Chat Completions 或 tools。真实三家服务的大目录、
分页、代理/证书、线程池饱和、关闭竞态和跨平台人工布局仍待验收。

## Native Agent 服务端 Token 用量（2026-09-13）

分支：`feature/agent-token-usage`。主要提交：`86d04f52a`（用量模型和流式解码）、
`988931e8e`（请求协商与设置开关）、`81e713d9b`（事件、Controller 和 Dock 展示）以及
`855deab47`（四语文案）、`fe27aa1d0`（DeepSeek 缓存字段兼容）。

### 协议、设置与可观测性

- `StreamingJsonDecoder` 在处理 `choices` 前读取标准 Chat Completions `usage`，因此能保留
  `choices: []` 的最终用量分片。公共 `ModelUsage` 区分 input/output/total、cached input
  和 reasoning token；只接受非负整数。服务端省略 total 但同时给出 input/output 时仅做
  精确加法，不使用字符数或本地 tokenizer 估算。缓存输入同时兼容 OpenAI 风格的
  `prompt_tokens_details.cached_tokens`、input/output 别名，以及 DeepSeek 的顶层
  `prompt_cache_hit_tokens`；推理计数兼容 completion/output details。
- 正式流请求默认发送 `stream_options.include_usage=true`。偏好设置新增
  **Request token usage when supported** 并持久化到 `request_token_usage`；关闭后同一值经
  MainWindow → Controller → Runner → ModelRequest 到达实际 HTTP body 和生命周期事件。
  Provider 配置还执行一次交集校验，避免上层关闭后意外重开。
- 独立 Chat Completions 探测明确关闭 usage 协商，继续只验证 endpoint、API Key、模型和
  最小聊天请求，不把可选扩展能力混入已验证配置证明。设置页提示该边界；usage 开关不改变
  连接指纹。若正式请求的兼容端点拒绝 `stream_options`，用户可单独关闭用量请求。
- `model_request_started` 记录 `usage_requested`；成功且服务端实际报告时，
  `model_request_completed.usage` 使用稳定的 provider-neutral 字段。调试导出自然保留这些
  事件，普通 Conversation Markdown 不复制技术遥测。
- Dock 的 Technical details 显示最近一次请求的 input/output/total 和可用的 cached input/
  reasoning。请求中、服务端未报告以及用户未请求有独立状态；缺失值保持 -1/不可用，绝不
  伪装成 0。控件属性公开 requested/reported 和五项计数，便于辅助技术与宿主测试检查。

### 测试证据与剩余项

`agent_stream_decoder` 覆盖分片截断、空 choices 最终 usage、嵌套缓存/推理计数、别名和省略
total 的精确合计；`agent_provider_catalog` 断言正式 body 包含可关闭的 usage 协商，而真实
本地连接探测不包含它；`agent_harness` 验证多步骤请求的 started/completed 遥测和关闭开关
传播；`agent_dock` 验证完整计数、服务端未报告与未请求三种界面状态；
`agent_dock_contract` 固定设置到 MainWindow/Runner 的宿主接线。完整 Sigil 构建及 42 个
固定 Python 依赖通过；13 项 Agent 测试连续 3 轮共 39 次通过。四份目录可生成 `.qm` 且
0 unfinished；严格简中、繁中、日文覆盖均通过，每份覆盖当前 5,655 条活跃源文。

本切片关闭此前记录的“无 token 用量”缺口，但只显示最近一次模型步骤的服务端整数，不把
多步骤 Agent 的合计、整轮墙钟时间、价格或网络分段延迟混为同一指标。当前也不解析厂商
自定义成本字段；没有 usage 支持的兼容端点会明确显示未报告。真实三家服务的 usage 形态、
长上下文/缓存计费、Windows/Linux 布局、屏幕阅读器和整轮聚合仍待验收。

## Native Agent 整轮任务计时（2026-09-13）

分支：`feature/agent-run-timing`。主要提交：`5dae170c3`（Runner 整轮计时事件）、
`823f21bb4`（Technical details 展示）和 `b6db90f3c`（四语文案）。

### 计时边界与终态

- 每次通过 Runner 前置接线校验的用户轮次生成独立 `run_id`，从
  `preparing_context` 开始计时。后续 `run_state_changed` 阶段事件携带同一 ID；只有
  completed/failed/cancelled 终态附带 `duration_ms`、`model_steps` 和 `tool_calls`，不会把
  进行中的时间冒充最终指标。
- Provider 失败、目标书籍改变、步数上限和取消路径均在错误/取消事件与暂存事务回滚完成后
  才发布计时终态。因此 Whole run 覆盖上下文组装、所有模型步骤、审批等待、工具执行和
  安全收尾；单次 `model_request_*` 的 duration 仍只测 `provider.stream()`，两者字段和界面
  行均独立。
- Dock Technical details 在进行中显示 run ID 和 **Whole run: in progress**，终态显示整轮
  毫秒数、模型请求数、工具调用数和完成时间；`runDurationMs` 等独立控件属性不会覆盖既有
  request `durationMs`。事件进入既有脱敏调试导出，Conversation Markdown 不复制技术遥测。

### 测试证据与剩余项

`agent_harness` 验证多步骤成功轮次从 preparing 到 completed 共用 ID，模型/工具计数正确，
并验证 Provider 失败与请求中取消都发布非负整轮耗时；`agent_dock` 验证进行中不虚构时长，
以及终态 Whole run 与最近请求 Duration 各自保留；`agent_dock_contract` 固定事件消费和
属性边界。完整 Sigil 构建及 42 个固定 Python 依赖通过；13 项 Agent 测试连续 3 轮共
39 次通过。四份目录可生成 `.qm` 且 0 unfinished；严格简中、繁中、日文覆盖均通过，每份
覆盖当前 5,659 条活跃源文。

本切片关闭上一节保留的“无整轮墙钟时间”缺口，但不会把墙钟时间拆成 DNS、TLS、首 token、
审批或各工具子阶段；多轮会话也没有累计时间。真实慢速审批、长工具、窗口关闭、跨平台
单调时钟/本地化时间和辅助技术仍待人工验收。

## Native Agent 整轮 Token 用量汇总（2026-09-13）

分支：`feature/agent-run-usage-summary`。主要提交：`a191c55b6`（Runner 覆盖率汇总）、
`c05d5be36`（Dock 完整/部分状态）、`0d27d3410`（四语文案）和 `880c697a3`
（跨 run 状态复位）。

### 精确汇总与缺失语义

- Runner 在每轮开始时冻结 usage 开关，并为 input/output/total/cached input/reasoning 分别
  累加服务端非负整数和报告请求数。终态 `run_state_changed.usage_summary` 同时记录
  `request_count`、`reported_request_count`、`missing_request_count`、
  `all_requests_reported` 及每个字段自己的 `*_request_count`。
- `request_count` 使用实际发出的模型请求数；Provider 失败或取消中的请求没有成功 usage，
  因此仍进入 missing 数。禁用开关、尚未发出请求、Provider 完全未报告和部分请求报告是
  四种独立状态，不会把未知值补成 0。
- 全部请求报告时 Dock 显示完整整轮合计；部分报告时显示 **N of M requests reported**，并
  只把已报告请求的精确和标为部分数据。某个标准字段若未覆盖所有已报告请求则显示未报告；
  缓存/推理明细也只有完整覆盖已报告子集时才出现。
- 整轮汇总与最近一次请求 usage 使用独立成员和 `run*` 控件属性。新 run 的 preparing 事件
  会清空旧汇总，进行中只显示等待终态，不把上一轮数字带入当前轮。

### 测试证据与剩余项

`agent_harness` 覆盖两步完整汇总（250 input、30 output、280 total）、usage 禁用、第二步
Provider 失败时的 1/2 部分汇总，以及请求中取消的 missing 计数；`agent_dock` 覆盖进行中、
完整 2/2 和部分 1/2 的文案及独立属性；`agent_dock_contract` 固定覆盖率事件字段和宿主
消费边界。完整 Sigil 构建及 42 个固定 Python 依赖通过；13 项 Agent 测试连续 3 轮共
39 次通过。四份目录可生成 `.qm` 且 0 unfinished；严格简中、繁中、日文覆盖均通过，每份
覆盖当前 5,666 条活跃源文。

本切片关闭“整轮 token 不聚合”的缺口，但部分汇总不是消费账单；价格、币种、不同模型/
缓存层级的费率、服务端重试计费和会话跨轮累计均不推断。真实服务商混合字段、超大计数、
屏幕阅读器及多步骤长任务布局仍待验收。

## Native Agent 响应延迟分段（2026-09-13）

分支：`feature/agent-request-latency-breakdown`。主要提交：`ed854c94e`（Provider 流式边界
计时）、`33e81c470`（请求终态事件）、`3cf349f81`（Technical details 展示）和
`5b7d93ffc`（四语文案）。

### 本地测量语义与事件传播

- 公共 `ModelResponseTiming` 用 `-1` 明确表示未观测，只接受非负毫秒数，并可在 JSON 中
  往返 `first_byte_ms` / `first_model_event_ms`。它挂在 `ModelTurn` 上，不改变模型正文、
  reasoning、tool call、usage 或完成原因的既有语义。
- `OpenAICompatibleProvider` 在实际 `post()` 前启动单调计时。首个非空响应正文记录
  first byte；首个包含 reasoning/content/tool call/finish reason 的解码增量记录 first model
  event。SSE 注释、空行和 keepalive 只影响前者，不伪造成模型事件；后者因此也不命名为
  first token。
- decoder 完成时会再次发布尾部增量，使没有结尾换行的最后一条 JSON/SSE 事件仍能到达
  sink。成功、HTTP/协议失败、超时和取消路径都保留已真实观测到的字段，缺失边界不补 0。
  同一组整数进入脱敏 HTTP trace 和 `model_request_completed/failed/cancelled` 的
  `response_timing`，没有响应正文、模型文本或密钥。
- Dock Technical details 在请求中显示等待响应，终态分别展示 first byte 与 first model
  event；单个字段缺失时显示 **Not observed**。独立控件属性不会覆盖最近请求 Duration、
  Whole run 或 token 用量；新请求和 New Session 都会清空旧值。

### 测试证据与剩余项

`agent_stream_decoder` 覆盖响应计时 JSON 往返；`agent_provider_catalog` 的本地 HTTP 服务先
发送 SSE keepalive，再发送没有尾随换行的最终 content 事件，断言 first byte 早于 first
model event 且最终增量不会丢失，该测试另连续运行 5 次通过。`agent_harness` 覆盖成功、
失败和取消事件的字段保真；`agent_dock` 覆盖 9/14 ms 展示、复位与公开属性；
`agent_dock_contract` 固定 Runner/Dock 接线边界。完整 Sigil 构建及 42 个固定 Python 依赖
通过；13 项 Agent 测试连续 3 轮共 39 次通过。四份目录可生成 `.qm` 且 0 unfinished；严格
简中、繁中、日文覆盖均通过，每份覆盖当前 5,670 条活跃源文。

本切片补齐请求内部最有用的两个用户可见边界，但 first byte 可能只是代理或服务端心跳，
first model event 也可能是 reasoning、tool call 或 finish reason，并非保证可见文本 token。
当前未拆分 DNS、TCP、TLS、上传、排队与逐 token 吞吐，也没有跨平台真实高延迟代理和
三家在线服务的人工对照；这些指标只用于诊断，不应单独解释为模型质量或服务端计算时间。

## Native Agent 原生计划审阅卡（2026-09-13）

分支：`feature/agent-plan-review-cards`。主要提交：`c53f69e9f`（原生计划事件）、
`081b5c966`（书籍绑定的结构化审阅卡与资源跳转）、`7a16851b3`（审阅绑定批准门）、
`d1e054b00`（四语文案）和 `68b20ec73`（可读 Conversation 导出）。

### 计划事件、审阅内容与批准闭环

- Runner 只在 `paragraphs.plan` / `toc.plan_transform` 成功、preview-only、未应用且同时返回
  非空 plan ID/digest 时发布 `plan_created`。事件自包含原工具的受限 changes、plan kind、
  review status、run ID 和 book session；普通只读工具、分析、失败结果和事务 preview 不会
  被误标成计划。
- 段落审阅卡按资源显示汇总、变更范围、转换/保护计数及原生工具生成的 bounded before/
  after 片段，正文 QLabel 固定 PlainText。TOC 卡显示受影响/重新归属计数、label/target、
  父级与深度变化、先序和不改正文标题的不变量，并保留 changes-truncated 提示。两者都
  明确活书未改变、局部校验与完整 EPUBCheck 状态。
- 每张卡只为前 8 个唯一 book path 建立跳转按钮，避免大计划创建无界控件。按钮和信号携带
  完整 book session；Dock 在上下文切换时即时禁用旧按钮，MainWindow 再次 fail-closed 核对，
  然后移除 TOC fragment、URL 解码并复用既有 `OpenFile`。卡片属性保留 plan/digest/revision，
  供辅助技术和宿主测试检查。
- Dock 仅记录本会话实际展示的计划。Edit 模式收到 `paragraphs.apply` 或
  `toc.apply_transform` 批准请求时，必须同时匹配 kind、plan ID、digest、expected revision
  和当前 book session 才启用 Approve；不匹配时显示阻断提示且保留 Deny。工具内部原有的
  精确源码/树重验继续构成第二道边界；普通 transaction commit 与 Auto 模式语义未改变。
- Conversation Markdown 抑制这两个计划工具冗余的原始 completion JSON，改为输出按资源
  可读的 Plan review、源码片段/TOC 结构和验证边界，不复制协议绑定。Debug JSON 仍保留
  ToolCompleted 与完整 `plan_created`，用于诊断和可复现取证。

### 测试证据与剩余项

`agent_harness` 用真实段落分析/计划工具验证事件只发布一次，并保留 plan/digest/run/book
绑定；普通 `book.summary` 不产生假计划。`agent_dock` 覆盖段落 XML 纯文本差异、TOC
父级/深度变化、fragment 去除、资源跳转、换书即时禁用与切回恢复，以及匹配/错误 digest
批准门；`agent_dock_contract` 固定 MainWindow 双重书籍核对和 URL 解码接线。
`agent_provider_catalog` 按真实 ToolCompleted → PlanCreated 顺序验证两类可读导出隐藏协议
绑定，而 Debug JSON 完整保留事件。完整 Sigil 构建及 42 个固定 Python 依赖通过；13 项
Agent 测试连续 3 轮共 39 次通过。四份目录可生成 `.qm` 且 0 unfinished；严格简中、繁中、
日文覆盖均通过，每份覆盖当前 5,693 条活跃源文。

本切片直接推进 AGENT-02/A04 的可检查计划和审批失效边界，但不宣称完成整个 AGENT-IM2：
目前只消费两类原生工具已经返回的 bounded changes，未提供整文件统一 diff、专用双栏预览
或卡片内独立操作组勾选；改变段落资源子集仍须重新生成计划。真实三语言 GUI、长片段布局、
键盘遍历、屏幕阅读器、10,000 节点 TOC 和 Windows/Linux 主题仍待人工/性能验收。

## Native Agent 流式界面增量合并（2026-09-13）

分支：`feature/agent-stream-ui-coalescing`。主要提交：`1de4fedb3`（Dock 增量合并与压力
回归）、`d0eca8b0e`（宿主热路径去除重复状态刷新）和 `af2e8c147`（待刷新缓冲取消回归）。

### 刷新预算与顺序保证

- `AssistantDelta` 只向 reasoning/content 缓冲追加字符串。single-shot timer 以 33 ms 为
  刷新周期，同一周期的两类文本合并为一个 render batch；持续流仍保持约 30 FPS 的渐进
  反馈，不再让每个细碎 token 各触发一次 QLabel 文本设置、布局和滚动范围更新。
- 任意非增量事件进入 Dock 前同步冲刷缓冲，覆盖正常完成、失败、取消、工具调用和下一用户
  轮次。最终 `AssistantMessage` 以 Provider 的完整正文校准卡片，并跳过内容完全相同的
  `setText`；既保留事件顺序和尾段，也不会因完成事件重复排版同一正文。
- transcript reset 与 session ID 变化会先停止 timer 并清空旧缓冲；延迟回调不能在新会话中
  生成旧卡片。`streamFlushIntervalMs` / `streamRenderBatches` 作为无正文的自动化属性公开
  刷新预算与实际批次数，不进入会话或导出。
- MainWindow 不再为每个 `AssistantDelta` 重跑 `setRunState` 及其按钮、重试、任务恢复刷新。
  Runner 在开始流式传输前已有显式 `streaming_response` 状态事件；其他事件仍保留原宿主
  同步路径，因此本切片不改变停止、批准、失败和完成状态语义。

### 测试证据与剩余项

`agent_dock` 覆盖 timer 到期时 reasoning/content 同批刷新，以及 2,000 个连续 content chunk
在终止事件前零卡片渲染、终止时一次完整输出，并验证 transcript reset 取消待执行 timer；
多轮卡片隔离继续通过。
`agent_dock_contract` 固定 Runner 先发布 streaming 状态、MainWindow 再跳过 delta 状态刷新
的接线边界。完整 Sigil 构建及 42 个固定 Python 依赖通过；13 项 Agent 测试连续 3 轮共
39 次通过。四份目录可生成 `.qm` 且 0 unfinished；严格简中、繁中、日文覆盖均通过，非英文
目录各覆盖当前 5,693 条活跃源文。

本切片减少 GUI 主线程的更新次数，不改变 Provider 网络读取、decoder、session 事件记录或
导出语义，也不宣称提高模型吞吐。33 ms 是交互刷新预算而非逐 token SLA；超长单卡仍使用
QLabel 保存完整文本，尚未做虚拟化/分块文档，也没有真实慢速在线服务、低端硬件和
Windows/Linux/macOS 三平台 GUI profiler 数据。

## Native Agent 原生计划双栏比较（2026-09-13）

分支：`feature/agent-plan-comparison-dialog`。主要提交：`e8aadaeb9`（通用受限比较组件）、
`1814b1c5e`（段落/TOC 审阅接线）和 `4d2a30e0e`（四语文案）。

### 比较界面与绑定边界

- `AgentPlanComparisonDialog` 是独立的只读双栏组件。before/after 使用 `QPlainTextEdit`
  PlainText、固定宽度字体和 NoWrap，水平/垂直滚动互相同步；每栏即使收到异常事件也最多
  显示 8,192 字符，截断时显示明确提示。窗口不含 Apply，也不读取或写入 Book。
- 段落计划为前 8 个唯一资源在原 Open 操作旁提供短标签 **Compare…**，完整路径保留在
  accessible name。内容只取 `paragraphs.plan` 的 `source_diff.before/after` 及前后截断标志，
  XML 标签保持字面文本，不交给 rich-text renderer。
- TOC 计划提供单个 **Compare hierarchy…**，把 bounded changes 的 label/target 及
  from/to parent/depth 分别排列在两栏；`changes_truncated` 继续传到对话框，不能把前 128 项
  冒充完整目录变化。
- 比较按钮和窗口都绑定计划的 book session；点击前重验，换书立即禁用按钮并关闭已打开窗口，
  transcript reset 也关闭窗口。窗口同时公开 plan ID、digest、book revision、book session
  和 comparison subject，批准卡原有的计划绑定门保持独立且不被比较操作修改。

### 测试证据与剩余项

新增 `agent_plan_comparison_dialog` 覆盖双栏结构、只读/NoWrap/辅助名称、8 KiB 防御上限、
截断提示、双向同步滚动和关闭释放。`agent_dock` 覆盖段落 XML 前后片段、TOC parent/depth、
窗口计划属性、重复打开复用、换书失效和 transcript reset 清理。完整 Sigil 构建及 42 个
固定 Python 依赖通过；新增后的 14 项 Agent 测试连续 3 轮共 42 次通过。严格四语目录覆盖
通过，四份 `.qm` 均可生成且 0 unfinished；英文为 4,735 条活跃源文，简中/繁中/日文各为
5,712 条。

本切片直接补上 AGENT-02 的专用双栏审阅入口，但不宣称完整关闭 AGENT-IM2：它显示的是
原生工具已经生成的 bounded evidence，并非任意 staged transaction 的整文件 unified diff；
尚未支持卡片内选择结构独立操作组，也未在 Windows/Linux、屏幕阅读器、超长真实计划和
不同字体/DPI 下完成人工验收。

## Native Agent 段落独立操作组（2026-09-13）

分支：`feature/agent-paragraph-plan-groups`。主要提交：`3804e6d01`（段落计划与选中子集
暂存）、`d364fda40`（批准参数的受限传递）、`57ddfd7e9`（Dock 组选择界面）、
`b3f58b0ae`（四语文案）和 `418165925`（导出及失败关闭回归）。

### 独立性证明与批准闭环

- `paragraphs.plan` 已有范围不变量为 `changes_css=false`、`changes_opf=false`、
  `adds_resources=false`，每个输出仅替换一份既有 XHTML。因此计划显式返回每资源一个
  `operation_groups` 条目及 `operation_groups_independent=true`；没有把可能互相依赖的 TOC
  reparent/adopt 变化伪装成独立项，TOC 批准卡明确显示它仍是一个不可拆分组。
- `paragraphs.apply` 新增可选 `selected_resource_ids`。省略保持原有全计划行为；显式选择
  必须非空、唯一且属于已审阅计划。工具仍先校验原始 plan ID/digest/expected revision，
  再只对选中资源重建输出与 CSS 依赖，最后在一个独占事务中原子暂存；中途失败整批回滚，
  未选中的 XHTML 保持逐字节不变。
- GUI 批准决策现在可以携带参数覆盖，但 Runner 采用白名单：只有规范化后的
  `paragraphs.apply` 可覆盖 `selected_resource_ids`，plan ID、digest、revision 及其他工具
  参数都不能由 UI 改写。`tool_approved` 记录实际执行参数与是否应用覆盖，形成可审计闭环。
- 匹配计划的批准卡使用一个限高 `QListWidget` 承载所有 XHTML 组，默认全选，支持逐项、
  Select all 和 Clear；零选择禁用 Approve，Deny 始终可用。决定后列表和批量按钮冻结；畸形
  或重复组记录失败关闭。该结构不会为大型计划按行创建无界 QWidget。
- Conversation Markdown 的段落计划摘要显示独立 XHTML 组数而不泄露 plan/digest；Debug
  JSON 保留完整 `plan_created`、批准后的实际 `selected_resource_ids` 与工具结果。

### 测试证据与剩余项

`agent_div_paragraph_tools` 覆盖组声明、空/重复/越界选择在事务前拒绝、二选一精确暂存及
回滚；`agent_harness` 覆盖 operation groups 进入计划事件、只允许组选择覆盖并忽略恶意
digest 覆盖；`agent_dock` 覆盖默认全选、清空阻断、单组批准、决定冻结、普通批准无覆盖、
错误 binding 和重复组失败关闭；`agent_provider_catalog` 验证可读导出保留组数但不输出协议
绑定。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent 测试连续 3 轮共 42 次
通过。严格四语目录覆盖通过，四份 `.qm` 均可生成且 0 unfinished；英文为 4,744 条活跃
源文，简中/繁中/日文各为 5,721 条。

本切片补齐 AGENT-02 中已有独立性证明的段落文件选择，但不把选择能力推广到任意 staged
transaction，也不允许拆分 TOC 依赖变化。选择发生在 apply 批准而不是修改计划内容；要
改变转换选项、加入计划外文件或处理失效资源仍须重新分析/生成计划。整文件 unified diff、
超大真实计划、键盘/屏幕阅读器、Windows/Linux 主题与在线模型端到端人工验收仍待后续。

## Native Agent 原子事务资源结果（2026-09-15）

分支：`feature/agent-transaction-resource-outcomes`。主要提交：`3f5af4f01`（Runner 原子结果
协议与成功/回滚/冲突回归）、`2fe244409`（成功和失败结果卡）、`984f723c7`（可读 Conversation
导出）、`1c7b5b807`（四语文案）和 `7eac7c26c`（模型结果报告约束）。

### 冻结范围、原子语义与用户结果

- Runner 在实际 `transaction.commit` 前复用任务恢复所需的真实 `previewTransaction()`，按
  首次出现顺序冻结并去重 `changes[].resource_id` 与 removed IDs；新增、正文、CSS、重命名和
  删除统一计入资源。metadata、spine、TOC 作为结构操作类别单列，不把旧的
  `applied_changes` 操作计数冒充资源数。
- 每次实际执行的 commit 都在工具数据中写入 `resource_outcomes`：范围是否可用、原子语义、
  资源 ID/成功/失败数、结构类别及其成功/失败数，以及 committed/staged/rolled_back/not_open
  事务状态。成功意味着冻结范围全部成功且失败数为 0；任何提交失败均报告 0 成功和整个
  未应用范围，绝不把中途写入后回滚的资源当作最终成功。
- revision/源码冲突拒绝写入但保留事务时，失败卡明确仍可审阅、修复后重试或 rollback；宿主
  在中途故障后完成原子回滚时，卡片明确没有残留部分修改。提交前 Preview 不可得时只报告
  scope unavailable，不伪造 0/0。服务端/工具错误正文强制按 PlainText 显示。
- Applied 卡继续保留“写入当前 Book、EPUB 尚未保存”、Book revision、完整 EPUBCheck 和恢复
  边界，同时新增资源与结构结果。失败 commit 复用同一个 ToolFailed 事件形成展开的
  **Apply failed** 结果，不制造与工具历史脱节的第二个终态事件。
- Conversation Markdown 抑制已有专用 Preview/Applied/Rollback 摘要的原始 ToolCompleted
  JSON；成功和失败提交各输出一份可读结果。完整 `resource_outcomes` 仍保留在脱敏 Debug
  JSON。系统提示要求模型按这些精确字段报告，禁止从 `applied_changes` 猜资源成功数。

### 测试证据与剩余项

`agent_harness` 以真实 Memory workspace 覆盖单资源成功、两资源加 metadata 的第二资源故障
注入及整批回滚、错误 expected revision 的零写入和暂存保留，并固定模型报告约束；
`agent_dock` 覆盖成功资源/结构计数、原子全成功、回滚失败、冲突仍暂存、公开属性及恶意
样式错误正文的 PlainText；`agent_provider_catalog` 覆盖成功/失败可读导出、秘密脱敏和原始
commit JSON 去重。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent 测试连续 3 轮
共 42 次通过。严格四语目录覆盖通过，四份 `.qm` 均可生成且 0 unfinished；英文为 4,756 条
活跃源文，简中/繁中/日文各为 5,733 条。

本切片直接补齐 AGENT-04 的成功/失败资源计数，并强化 A07/A08 的原子回滚与未保存结果证据。
它统计的是事务 Preview 中的宿主资源，不宣称每个 metadata 字段或每条 TOC 节点都是独立
资源；结构类别仅给出成功/失败操作类别数。当前也未提供失败资源的可点击明细表、跨进程
恢复、真实磁盘保存后结果、Windows/Linux GUI、超大事务性能或在线模型人工端到端验收。

## Native Agent 完整轮次历史预算（2026-09-15）

分支：`feature/agent-history-budget`。主要提交：`0dd8821a4`（完整轮次裁剪与回归）、
`b50eea67b`（请求审计统计）、`37a0a464a`（持久化设置与 Runner 接线）、`77090af9a`
（Technical details 展示）和 `1a733850e`（四语文案）。

### 有界回放与当前轮完整性

- `HistoryAssembler` 先把 User/Assistant/ToolCompleted/ToolFailed 事件按用户轮次分组，并按
  实际 OpenAI 消息对象的紧凑 JSON UTF-8 大小计费。默认 32 KiB 只用于先前已完成轮次；
  当前运行轮始终完整保留，即使其 assistant/tool 往返自身超过预算。
- 选择算法只输出完整轮次的连续最近后缀。若一个更旧轮次无法装入剩余预算，则该轮及更旧
  轮次全部省略；不会留下孤立 tool result，也不会为了填满预算跳过中间轮后再加入更旧轮。
  0 明确定义为 Unlimited，保留旧版全量回放语义。
- 预算不涵盖独立的 system prompt、技能正文或当前书籍/选区上下文，也不会改写 `AgentSession`
  事件、Conversation 导出和任务记忆。因此它限制模型请求增长，但不是 token/context-window
  的硬保证，当前轮和书籍上下文仍可能使请求超过较小模型的上下文长度。
- 每次 `ModelRequestStarted` 都记录 `history_context`：预算是否启用、预算字节、总计/发送/
  省略轮次、发送/省略消息数、先前轮次总字节/发送字节及当前轮字节。该对象同时留在脱敏
  Debug JSON，避免仅凭最终 token usage 反推裁剪行为。

### 设置与可观察性

- 偏好设置新增 **Previous-turn history budget**，单位 KiB，范围 0–512、默认 32；0 以
  **Unlimited** 显示。提示明确只限制先前完整轮次且当前运行完整保留。字节值经
  `AgentSettings → MainWindow → AgentController → AgentRunner` 传递，旧设置中的负数回退默认，
  超范围值在读取、保存和 Runner 边界均夹紧。
- Dock 的 **Technical details** 为最近请求显示发送/总轮次、遗漏轮次、先前历史用量/预算和
  当前轮大小，并标明当前轮 always retained；无限模式单独显示，不把 0 误解成零历史。
  精确字节与轮次数也以 QWidget 动态属性公开给 GUI 自动化。

### 测试证据与剩余项

`agent_stream_decoder` 构造旧大轮次、最近小轮次和超过预算的当前工具轮，验证只省略完整旧
轮、保留连续后缀、当前 user/assistant/tool 全部存在且统计精确；`agent_harness` 固定默认
32 KiB 预算和请求开始事件；`agent_dock` / `agent_dock_contract` 覆盖设置持久化接线、限制
提示、技术详情文本与精确属性。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent
测试连续 3 轮共 42 次通过。四份 `.qm` 均为 0 unfinished，英文 4,762 条，简中/繁中/日文
各 5,739 条；当前 Agent Dock/设置的 274 条活跃文案已逐项核对四语存在、非空和占位符一致。

本切片关闭了长会话每轮请求随完整历史线性增长的默认路径，但没有做语义摘要、向量检索、
provider tokenizer 预估或自动按模型 context length 调整预算。字节计费不含 JSON 数组分隔符和
HTTP 包装开销；Unlimited 仍可能产生很大的请求。尚未完成超长真实在线会话的延迟/费用对比、
低内存设备与 Windows/Linux GUI 人工验收。

## Native Agent 按模式过滤工具目录（2026-09-15）

分支：`feature/agent-mode-tool-catalog`。主要提交：`b9bed70e9`（权限同源的 schema 过滤与
Runner 审计）、`6eeedba7b`（Technical details 展示）和 `096fae2b9`（四语文案）。

### 请求目录与执行策略同源

- `ToolRegistry::openaiToolSchemas` 新增 descriptor predicate，可在保持原有注册顺序、wire name
  和 schema 结构的同时输出严格子集；无 predicate 的旧调用仍返回完整目录。
- `PromptAssembler` 使用 Runner 传入的同一个 `PermissionPolicy` 判断 `Deny`：Ask 不再向模型
  宣告任何 `mutatesBook` 工具；Plan 保留读工具、begin/preview/rollback 及声明支持 preview 的
  暂存工具，隐藏 commit、checkpoint create/restore、`python.run` 和其他不可预览写入；Edit/
  Auto 没有 Deny，继续收到完整目录。空指针直调 PromptAssembler 时使用同类型默认策略，测试
  和非 Runner 调用不会退回无条件全量目录。
- 过滤只改变发送给模型的 schema，不改变 ToolRegistry 内容。Runner 收到 tool call 后仍按
  wire/dotted name 查真实 descriptor 并再次执行权限策略；恶意或幻觉模型返回 Ask 写工具时仍
  产生 `PERMISSION_DENIED` tool-role 结果且不进入 ToolStarted，不会把优化当作授权边界。
- 每个 `ModelRequest` 和 `model_request_started` 新增 `tool_context`：模式、策略已应用标志、
  总计/暴露/隐藏数量、按注册顺序的隐藏 dotted names、完整/暴露紧凑 JSON schema 字节和差值。
  这些统计不发送给模型，只进入会话审计与脱敏 Debug JSON。

### 用户可观察性与测试证据

Dock 的 **Technical details** 显示最近请求暴露/总工具数、按模式隐藏数和过滤后/未过滤 schema
KiB；精确数量、字节、节省字节与隐藏工具名同时作为 QWidget 动态属性供 GUI 自动化读取。
Edit/Auto 的隐藏数和节省值明确为 0，不制造性能收益；这里的字节差也不冒充 provider token。

`agent_book_tools` 覆盖 ToolRegistry predicate 只保留接受的 descriptor；`agent_harness` 分别
固定 Ask、Plan、Edit、Auto 的工具集合与统计，验证 Ask 隐藏 patch/commit/python、Plan 保留
patch 但隐藏 commit/checkpoint create/python、Edit/Auto 完整，并模拟模型返回未宣告 Ask 写
工具后仍被执行策略拒绝；`agent_dock` / `agent_dock_contract` 覆盖请求事件、可读摘要、精确
属性与隐藏名称。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent 测试连续 3 轮共
42 次通过。四份 `.qm` 均为 0 unfinished，英文 4,763 条，简中/繁中/日文各 5,740 条；当前
Agent Dock/设置的 275 条活跃文案已逐项核对四语存在、非空和占位符一致。

本切片减少的是每次请求固定携带的工具 schema 和选择错误工具的机会，不改变 Provider、工具
实现、批准流程或模式语义。它没有做任务意图级动态工具检索、工具分组分页、在线模型 A/B、
tokenizer 精确计数或跨 provider 成本基准；Edit/Auto 仍携带完整目录，后续若继续缩减必须保证
多步骤任务能够发现所需工具，不能只为更小请求而牺牲完成能力。

## Native Agent 大型清单工具分页（2026-09-15）

分支：`feature/agent-paginated-inventory-tools`。主要提交：`4a5e0c5b4`（统一分页协议、边界
和大清单回归）与 `33de488a8`（模型续页规则）。

### 有界结果与兼容字段

- `book.resources`、`book.spine`、`book.toc` 和 `style.stylesheets` 从无参数全量结果改为可选
  `offset` / `limit`。清单类默认 100、硬上限 200；单项含最多一段 CSS 正文的 stylesheets
  默认 12、硬上限 50，避免当前运行轮因一次全量样式读取产生无界 tool-role 消息。
- 四个工具继续使用原有 `resources` / `spine` / `toc` / `stylesheets` 数组键，并新增统一的
  `total_count`、规范化 `offset` / `limit`、`returned_count` 和 `has_more`；仅在确有下一页时
  返回 `next_offset`。小清单无参数调用仍在首页返回全部数据，已有读取字段不需改名。
- 缺省参数由 schema 的 default 明示，offset 最小为 0，limit 最小为 1；执行边界仍夹紧绕过
  schema 的负数/过大输入。offset 超过总数规范化到结尾并返回稳定空页，不产生越界访问。
  Spine 页面保留全局 `index`，TOC/resource/CSS 顺序均沿 workspace 原数组稳定切片。
- 系统提示和每个工具 description 都要求模型在 `has_more=true` 时使用 `next_offset`，不得把
  第一页当作全量。`book.summary` 的资源/Spine/TOC 总数仍可用于先判断是否需要继续读取。

### 测试证据与剩余项

`agent_book_tools` 建立 235 个资源、225 项 Spine、215 项 TOC 和 55 份 CSS，覆盖默认首页、
硬上限夹紧、中间页全局索引、尾页、无 next_offset 的终止状态，以及普通清单与 CSS 两套
schema 边界；原有小型 Physics Book 的数组键和内容回归继续通过。`agent_harness` 固定系统
提示的 `has_more/next_offset` 续页要求。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项
Agent 测试连续 3 轮共 42 次通过。该切片没有新增 UI 文案；四份 `.qm` 继续为 0 unfinished，
英文 4,763 条，简中/繁中/日文各 5,740 条。

本切片界定的是送入模型、会话与导出的工具结果，不改变 `IBookWorkspace` 返回数组的内部接口；
宿主仍先枚举完整内存清单再在 Tool 层切页，因此不宣称降低 Book 侧枚举峰值。单项 label/path
没有额外字符截断，`font.inventory`、`book.check`、transaction preview 等其他聚合结果也未纳入
本次分页；旧提示或第三方脚本若假定无参数永远返回全量，需要读取 `has_more` 后续页。

## Native Agent 单次运行模型步骤上限（2026-09-15）

分支：`feature/agent-model-step-limit`。主要提交：`b0d0f16e6`（Runner 限制、设置接线、审计与
回滚回归）、`104c8ce13`（Technical details 展示）和 `367d9163a`（四语文案）。

### 有界模型循环与事务边界

- `AgentRunner` 默认允许每轮 24 个模型步骤，公开边界为 1–64；所有入口均夹紧该范围，旧设置
  中小于 1 的无效值回退默认。步骤按实际 `provider.stream()` 请求计数，包括工具执行后的
  继续请求；检查发生在下一次请求之前，所以配置 N 最多发送 N 次，不中途截断响应或工具。
- 达到上限后 Runner 先复用正常失败/取消路径的 `rollbackOpenWork()`，再发布带稳定代码
  `MAX_MODEL_STEPS_EXCEEDED` 的 Error 和 Failed 终态。开放的 staged transaction 被回滚；此前
  已 commit 的 Applied 变更不在开放事务中，不会被误报或自动撤销。
- 所有活动运行状态事件包含 `max_model_steps`，终态另含实际 `model_steps`；上限错误同时记录
  两者。这样即使没有触发上限，Debug JSON 也能证明该轮实际采用的配置，而不依赖读取当前
  偏好设置反推历史运行。

### 设置、可观察性与测试证据

- 偏好设置新增 **Maximum model steps per run** 数值项，范围 1–64、默认 24。值经
  `AgentSettings → MainWindow → AgentController → AgentRunner` 传递；提示明确达到限制会停止
  运行并回滚未提交的暂存事务。
- Dock 的 **Technical details** 在运行中显示每轮上限，终态显示实际步骤数/上限；两者也以
  `runModelSteps` / `runMaxModelSteps` 动态属性公开给 GUI 自动化。会话切换和新运行会重置旧值，
  同一运行的后续事件若缺少字段则保留已审计的起始上限。
- `agent_harness` 让模型持续返回工具调用，验证配置 2 时只发送两次请求、产生稳定错误字段、
  发布 Failed 终态、回滚已打开的 Plan 事务且不残留 staged 状态；正常完成路径固定默认 24
  同时出现在开始和终态事件。`agent_dock` / `agent_dock_contract` 覆盖设置持久化接线、运行中/
  终态文本和公开属性。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent 测试连续
  3 轮共 42 次通过。四份 `.qm` 均为 0 unfinished，英文 4,767 条，简中/繁中/日文各 5,744
  条；当前 Agent Dock/设置的 279 条活跃文案已逐项核对四语存在、非空和占位符一致。

本切片防止的是模型通过连续请求/工具往返形成无界单轮循环，不是 token、耗时、工具调用数或
成本上限。一次模型请求仍可能很大或很慢，一次响应也可能提出多个工具调用；已 Applied 的提交
仍按各自恢复能力处理。真实在线 Provider 的长链路、达到上限前已经 commit 的混合任务、
Windows/Linux GUI 和辅助技术人工验收仍属于后续发布边界。

## Native Agent 单次运行工具调用上限（2026-09-15）

分支：`feature/agent-tool-call-limit`。主要提交：`20056f428`（整批预检、设置接线、提示与
Runner 回归）、`28055448f`（Technical details 展示）和 `654a6bc10`（四语文案）。

### 累计预算与整批零执行

- `AgentRunner` 默认每轮接受 128 次工具调用，设置/Runner 边界为 1–512。计数与模型步骤独立，
  按模型返回且通过批次预检的 calls 累计；权限拒绝或工具自身失败仍会消耗额度，防止模型用
  大量无效调用绕开预算。每次请求的 system prompt 都带当前剩余额度。
- 模型响应完成、书籍目标复核通过后，Runner 在发布 AssistantMessage 或进入 ExecutingTools
  前比较本批数量与余额。超限时不截取前缀，而是整批零执行；因此不会形成“同一模型批次只
  做了一半”的新部分状态，也不会把没有对应 tool result 的 assistant tool calls 纳入后续历史。
- 超限路径先 `rollbackOpenWork()`，再发布稳定代码 `MAX_TOOL_CALLS_EXCEEDED` 和 Failed 终态。
  Error 记录此前获准数、本批申请数、剩余数和上限；运行状态事件始终记录上限，终态保留实际
  获准数。此前已经 commit 的 Applied 变更不属于开放事务，继续遵循原恢复边界。

### 设置、可观察性与测试证据

- 偏好设置新增 **Maximum tool calls per run**。值经
  `AgentSettings → MainWindow → AgentController → AgentRunner` 持久化和传递；提示明确超限会
  整批拒绝并回滚未提交工作。`model_request_started` 另记录请求开始时的 used/remaining/max，
  使 Debug JSON 可以还原每步预算。
- Dock 的 **Technical details** 在运行中显示上限，终态显示实际工具调用数/上限，并公开
  `runToolCalls` / `runMaxToolCalls` 动态属性。该指标与相邻的模型步骤预算分开呈现。
- `agent_harness` 构造第一步 `transaction.begin`、第二步在只剩 1 个额度时返回 2 个调用，验证
  第二批没有任何 ToolStarted、没有进入 AssistantMessage 历史、开放事务回滚、稳定错误字段、
  请求提示余额和终态实际计数；正常两步任务固定默认 128 出现在请求与运行事件。
  `agent_dock` / `agent_dock_contract` 覆盖设置链、运行中/终态文案和公开属性。完整 Sigil 构建
  及 42 个固定 Python 依赖通过；14 项 Agent 测试连续 3 轮共 42 次通过。四份 `.qm` 均为
  0 unfinished，英文 4,771 条，简中/繁中/日文各 5,748 条；当前 Agent Dock/设置的 283 条
  活跃文案已逐项核对四语存在、非空和占位符一致。

本切片只限制调用数量，不限制单个调用的参数/结果字节、一次工具内部处理的资源数、Provider
响应体或已经开始的工具耗时；大型聚合结果仍需各工具自己的分页/截断策略。真实在线模型在
低额度下的任务完成率、单响应极大 tool-call 数组的 Provider 解码内存、达到上限前已经 commit
的混合任务、Windows/Linux GUI 和辅助技术人工验收仍属于后续边界。

## Native Agent 大型诊断工具分页（2026-09-15）

分支：`feature/agent-paginated-diagnostics`。主要提交：`4a20d9826`（共享窗口分页、模型续页
规则和大诊断夹具）。

### 多数组共享窗口

- `font.inventory`、`book.validate`、`book.check` 从无参数全量结果改为可选 `offset` / `limit`，
  默认 100、硬上限 200。已有字体/问题/未引用图片/XHTML 良构性数组键与 scalar 总结字段保持
  不变，小书无参数调用仍在第一页返回完整内容。
- 三个工具可能在一个结果里带长度不同的多个数组。统一 helper 对每个声明数组应用同一 offset
  和 limit，以 `total_counts` / `returned_counts` 按数组键报告精确计数；`has_more` 和
  `next_offset` 由最长数组决定。后续页中已经耗尽的短数组为空，尚未耗尽的数组继续返回，
  不把某一数组结束误报为整个诊断结束。
- schema 明示 offset 最小 0、limit 最小 1/最大 200/默认 100；工具边界继续夹紧绕过 schema
  的负数与过大值。系统提示将这三个入口加入分页清单，要求 `has_more=true` 时沿
  `next_offset` 继续，不把第一页当作完整诊断。

### 测试证据与剩余项

`agent_book_tools` 构造 235 份缺 body XHTML、225 张未引用图片、225 份嵌入字体和 215 份
CSS 字体声明，验证首页每数组最多 100、limit 夹紧到 200、共享 offset 尾页分别返回
35/25/35 与 25/15/15、最长数组结束后移除 `next_offset`、全量/本页计数以及字体二进制不进入
结果；原 Physics Book 的小字体、零问题和两份 XHTML 检查仍在首页完整返回。`agent_harness`
固定三项工具进入模型续页规则。完整 Sigil 构建及 42 个固定 Python 依赖通过；14 项 Agent
测试连续 3 轮共 42 次通过。本切片没有新增 UI 文案；四份 `.qm` 继续为 0 unfinished，英文
4,771 条，简中/繁中/日文各 5,748 条。

分页限制的是发给模型、会话与导出的结果，不改变 `IBookWorkspace::fontInventory()` /
`validate()` 或 `inspectBook()` 的内部全量计算；宿主仍先扫描整书、构造全数组再切页，所以不
宣称减少 CPU、Book 侧枚举或临时内存峰值。`transaction.preview`、原生计划和其他聚合结果也
未纳入本切片；真实超大 EPUB、在线模型的续页完整率与 Windows/Linux 构建仍待后续验证。
