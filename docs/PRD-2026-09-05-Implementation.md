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

仍未验收：原始文件编码/BOM/混合换行和 Unicode 在普通导出中的字节保真；
完整 EPUB 打开/另存为；缺 nav 的只读临时模型与显式修复；诊断与 modified 分离；
插件/MCP 的 `PluginPackageUpdate` DOM 改写旁路；跨资源失败恢复；性能预算、
三平台 GUI、完整 EPUBCheck 和独立阅读器。不能将本批称为“OPF 完整无损编辑”。
