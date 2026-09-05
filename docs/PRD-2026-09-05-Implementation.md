# 2026-09-05 PRD 实施审计

审计基线：`dca3d857a`（`native-agent`）。需求来自本地
`todo/Sigil_Enhanced_PRD_2026-09-05/00…07`；该目录被仓库忽略，本记录独立入库。

## 基线范围与证据

| 功能 | 当前源码证据 | 尚需完成的交付 |
|---|---|---|
| OPF | `ImportEPUB::ReadOPF/LoadInfrastructureFiles`、`OPFResource::SaveToDisk/UpdateText` 均会整理或重建；`Utility` 还会归一化换行/Unicode | 原文快照、局部补丁、诊断与修复分离、缺 nav 查看/显式修复、导出与恢复回归（O01–O12） |
| 文本选择 | `CodeViewEditor::mouseDoubleClickEvent` 仍采用 Qt 词选区和修饰键标签选择 | 正文单元策略、设置、Unicode/分屏/真实鼠标回归（S01–S12）；句子模式按 PRD 可后置 |
| TOC | `EditTOC::MoveLeft/MoveRight` 直接修改模型；没有对话框级历史 | 保持先序的稳定 ID 变换、多选规划、撤销、局部写回/事务、性能（T01–T12） |
| Clips | `MainWindow::UpdateClipButton` 更新文字和 tooltip，没有快捷键角标 | QAction 实际绑定提示、设置、辅助名称、主题/DPI/溢出验证（C01–C10） |
| div | `BookLiveParagraphNormalizer` 及其测试已存在 | 标题包装保护、通用保守预设、CSS 依赖、源码范围补丁、增量幂等、整书事务（D01–D14） |
| Agent | 原生 Agent、Memory/Sigil workspace、Live v2、工具注册、取消/事务及 UI 测试已存在 | 对照真实宿主验证范围/修订/审批绑定/恢复；复用上述原生服务完成三个任务演示（A01–A12） |

原生 Agent 是当前分支已经存在且用户明确允许优化的实现；PRD 基于更早版本提出的“不新建聊天产品”不应导致移除现有功能。

## 分支与提交策略

- OPF：`feature/opf-source-preservation`，独立工作树
  `/Users/parsle/Code/sigil-enhanced-opf-preservation`。
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
未通过 EPUBCheck 合规认证。O02 要求整包 SHA-256 一致，而普通 ExportEPUB
仍重建 ZIP；本批只证明 OPF 成员字节一致，未满足整包及 GUI 另存/副本验收。
跨资源事务回滚、外部冲突、多平台 GUI、阅读器与完整 EPUBCheck 仍待验证。

### 下一批审计重点

- O06/O07：`ImportEPUB::GetBook` 仍自动注册缺失的 nav；结束时仍将警告数量
  直接映射为 Book modified。须把只读诊断、临时目录模型和显式修复计划分离。
  不能只删除创建 nav 的调用：`TOCModel` 与 `NavProcessor` 当前假定 EPUB 3
  有非空 nav，需要一并处理 NCX 查看回退及用户确认后的新增资源/节点。
- O08：插件/MCP 的 `PluginPackageUpdate` DOM 改写仍是旁路，现有插件测试通过
  不表示已经采用源码补丁和修订校验。
