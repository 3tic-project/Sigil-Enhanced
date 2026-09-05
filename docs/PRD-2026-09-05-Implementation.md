# 2026-09-05 PRD 实施审计

审计基线：`dca3d857a`（`native-agent`）。需求来自本地
`todo/Sigil_Enhanced_PRD_2026-09-05/00…07`；该目录被仓库忽略，本记录独立入库。

## 范围与当前证据

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
