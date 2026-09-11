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
