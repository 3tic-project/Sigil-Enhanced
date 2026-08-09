# 搜索模板批处理性能优化：审计与实施设计

> 状态：Implemented v1 / audited（兼容回退见 1.1）
> 日期：2026-08-09
> 审计基线：`de0ea00e0`（`Add BookLive paragraph normalizer`）
> 实施范围：`9290e4de3` 至 `6289416cd`
> 上位路线：`../todo/Sigil-Enhanced-Development-Plan.md` 第 5 节
> 本文边界：只设计“已保存搜索模板的 Replace All 批处理”；单次交互式 Find/Replace、Dry Run 和 Replace Current 暂不迁移。

## 1. 结论与决策

审计基线中的问题已在 v1 支持路径上修复。普通版和增强版保存搜索不再逐规则调用 GUI `ReplaceAll()`；规则先按原顺序作用于内存 working text，整批成功并建立恢复快照后，每个实际变化的资源只写回一次。不可避免的 `规则数 × 资源数` 文本扫描仍然存在，但同量级的 `QTextDocument::setPlainText()`、`Modified()` 信号、重复 checkpoint 和 Preview 更新已从默认批处理路径移除。

目标实现采用以下边界：

1. 批处理开始时只调用一次 `SaveTabData()`，并冻结搜索范围。
2. 把搜索条目编译为与 UI 解耦的不可变规则 DTO。
3. 所有规则按原顺序作用于 staging text；分析阶段不调用 `Resource::SetText()`。
4. 全部规则成功后，只创建一个非变异式恢复快照。
5. 每个实际变化的资源最多调用一次 `SetTextAsUndoableEdit()`，并形成一个撤销步骤。
6. Book modified、标签页刷新和 Preview 更新在提交阶段汇总。
7. 任意编译、Python、取消、冲突或 checkpoint 错误都必须发生在正式写回前，并保证零资源写入。

第一版保持单线程顺序执行。性能收益来自去除 GUI/资源反复写回，而不是冒险并行 PCRE、Python 或 Qt 文档对象。

### 1.1 v1 实施边界

已启用的默认路径：

- 普通保存搜索的 normal、case-sensitive、PCRE regex 及现有 regex options；
- Plus 保存搜索的 normal、regex 和 pre-search；
- Current、HTML/CSS、Selected/Tabbed、OPF/NCX、SVG/JS/Misc XML 等现有资源范围；
- SearchEditor 直接执行和 Automate 的三个保存搜索命令。

为保持既有语义，以下整批仍显式回退旧路径：

- Marked Text；
- 普通版 Python `\F<function-name>` replacement；
- 普通版 spell-check replace；
- 普通版 Current File 且 wrap 关闭。

尚未纳入 v1：临时文件 staging 后端、GUI 取消/进度、完全脱离 QWidget 的 controls compiler，以及 Python function 的 staged executor。纯 runner 已支持取消并按 fail-closed 处理，但 coordinator 暂未暴露 GUI 取消入口。

## 2. 审计范围

已检查以下调用链：

- `SearchEditor` / `SearchEditorPlus` 的搜索条目所有权和完成状态；
- `FindReplace::ReplaceAllSearch()`；
- `FindReplacePlus::ReplaceAllSearch()`；
- `FindReplace::ReplaceAll()` / `FindReplacePlus::ReplaceAll()`；
- `SearchOperations` 普通、pre-search 和 Python function replacement；
- `TextResource::SetText()`、`HTMLResource::SetText()`；
- `FlowTab::ResourceModified()` 与 Preview 信号；
- `MainWindow::RepoCommit()`；
- Automate 的 `RunSavedSearchReplaceAll`、`OnFailed...`、`OnSuccess...`。

审计未发现 `SearchBatchRunner`、`BookEditSession`、working-text map、搜索批处理 signal-count test 或 1000 × 50 benchmark 的现有实现。

## 3. 变更前调用链（审计基线）

### 3.1 普通版

```text
SearchEditor::ReplaceAll
→ FindReplace::ReplaceAllSearch
→ foreach searchEntry
  → LoadSearch（把 controls 写入 UI）
  → ReplaceAll
    → currentTab.SaveTabContent
    → GetFilesToSearch(true)
    → ReplaceInAllFiles
      → currentTab.SaveTabContent（再次执行）
      → foreach Resource
        → PerformGlobalReplace
        → SetText（包括 0 match）
    → Book.SetModified
    → currentTab.ContentChangedExternally
```

普通版全书路径支持 `\F<function-name>` Python function replacement。Automate 当前固定调用普通版 `m_SearchEditor` / `m_FindReplace`，不跟随增强版 UI 模式。

### 3.2 增强版

```text
SearchEditorPlus::ReplaceAll
→ FindReplacePlus::ReplaceAllSearch
→ foreach searchEntry
  → LoadSearch
  → ReplaceAll
    → currentTab.SaveTabContent
    → GetFilesToSearch(true)
    → CountInFilesPlus
    → RepoCommit
      → 更新 OPF modification date
      → SaveTabData
      → SaveAllResourcesToDisk
      → 创建 repo checkpoint
    → foreach Resource
      → PerformGlobalReplacePlus
      → SetText（包括 0 match）
    → Book.SetModified
    → currentTab.ContentChangedExternally
```

这意味着增强版中每条有匹配的规则都可能创建 checkpoint，并将上一条规则的结果写盘。

## 4. 审计发现

| ID | 等级 | 发现 | 影响 | 处理要求 |
| --- | --- | --- | --- | --- |
| A-01 | High | 两套 `ReplaceAllSearch()` 都逐条调用 GUI `ReplaceAll()` | 无法在组级合并写回、刷新和错误处理 | 统一进入批处理 coordinator |
| A-02 | High | 每条规则、每个资源都调用 `SetText()`，零匹配也调用 | `R × F` 次整份 `QTextDocument` 替换和 `Modified()` | staging 阶段禁止访问写接口；提交时 changed resource 一次 |
| A-03 | High | `TextResource::setPlainText()` 触发 `Modified()`，打开 XHTML 随即更新 Preview | 大量重复排版、光标保存/恢复、Preview 渲染 | signal-count test；每个 changed open tab 最多一次 |
| A-04 | High | 增强版把 checkpoint 放在单条规则内 | 重复全书写盘、OPF 更新时间和 repo commit | checkpoint 移到组级 |
| A-05 | Critical | `RepoCommit()` 会修改 OPF，并非只读快照 | 暂存 OPF 后再 checkpoint 会形成 stale staged text，可能覆盖新 modification date | 新增非变异式 recovery snapshot；不得直接复用当前 `RepoCommit()` |
| A-06 | Medium | 普通版一条规则可能执行两次 `SaveTabContent()` | 重复工作且扩大 UI 副作用 | 组入口只执行一次 `SaveTabData()` |
| A-07 | High | 规则 controls 通过修改 Find/Replace UI 后再读取 | 引擎依赖活动窗口状态，难以验证和自动化 | 引入纯 `SearchBatchRuleCompiler` |
| A-08 | High | `qApp->processEvents()` 接受用户输入，scope 和当前 Tab 可在批处理中变化 | 重入、目标漂移、当前资源变化 | scope snapshot；只处理 `ExcludeUserInputEvents` |
| A-09 | High | Automate 固定走普通版，且找到模板后无条件 `success = true` | UI/Automate 语义分叉；运行错误可能被当成功 | 统一 coordinator 与结构化结果 |
| A-10 | High | Python function replacement 每条规则建立有状态 Python 环境 | 简单交换循环层级或并行资源会改变结果 | 保持 rule outer loop、resource inner loop；每规则一个 Python session |
| A-11 | Medium | 搜索条目在每条执行后即 `RecordEntryAsCompleted`，包括增强版 checkpoint 返回错误时 | 失败后无法原样重试，完成状态并不代表已提交 | 仅在整组成功提交后统一完成 |
| A-12 | Medium | replacement count 与 changed resource 不等价，例如把 `x` 替换为 `x` | Automate 条件依赖 count，不能用 changed flag 替代 | 分开保留 `replacementCount` 与 `changedResourceCount` |
| A-13 | High | 当前文件/Marked Text 走编辑器 `Searchable`，语义不同于全资源替换 | 直接改成全文 staging 可能扩大作用域 | 第一版 Marked Text 整组回退 legacy；当前文件非 marked 可 staging |
| A-14 | Medium | 没有差分、信号次数、checkpoint 次数和大样本基准 | 无法证明性能收益且容易产生隐性语义回归 | 测试和计数器必须先于 UI 切换落地 |

## 5. 目标架构

建议新增：

```text
src/Misc/SearchBatchRule.h
src/Misc/SearchBatchRuleCompiler.h/.cpp
src/Misc/SearchBatchRunner.h/.cpp
src/Misc/SearchBatchStagingStore.h/.cpp
src/MainUI/SearchBatchCoordinator.h/.cpp
tests/search_batch_rule_compiler_test.cpp
tests/search_batch_runner_test.cpp
tests/search_batch_integration_test.cpp
tests/search_batch_benchmark.cpp
```

职责边界：

- `SearchBatchRuleCompiler`：把普通/Plus entry 和 controls 编译为稳定 DTO；不读写 QWidget。
- `SearchBatchRunner`：纯文本规则执行、计数、取消和错误传播；不持有 MainWindow/Tab/Resource。
- `SearchBatchStagingStore`：按 bookpath 保存 original/working text，并支持内存与临时文件后端。
- `SearchBatchCoordinator`：一次保存、scope snapshot、冲突检查、checkpoint、资源写回和 UI 汇总。
- `SearchOperations`：保留交互式 API；抽出可复用的纯 QString transformation，避免复制 PCRE 行为。

### 5.1 DTO 草案

```cpp
enum class SearchBatchEngine {
    Regex,
    PreSearch,
    PythonFunction
};

enum class SearchBatchScope {
    CurrentFile,
    AllHtml,
    SelectedHtml,
    TabbedHtml,
    AllCss,
    SelectedCss,
    TabbedCss,
    SelectedFiles,
    Opf,
    Ncx,
    SelectedSvg,
    SelectedJs,
    SelectedMiscXml
};

struct SearchBatchRule {
    QString id;
    QString displayName;
    QString findText;
    QString replacementText;
    QString preSearchText;
    QString compiledRegex;
    QString pythonFunctionName;
    SearchBatchEngine engine;
    SearchBatchScope scope;
    bool reverseDirection = false;
};

struct SearchBatchRuleResult {
    QString id;
    qint64 replacementCount = 0;
    int matchedResourceCount = 0;
    int changedResourceCount = 0;
    QStringList warnings;
};

struct SearchBatchResult {
    bool success = false;
    bool cancelled = false;
    bool conflict = false;
    qint64 replacementCount = 0;
    int changedResourceCount = 0;
    QList<SearchBatchRuleResult> rules;
    QString error;
};
```

DTO 不直接复用 `FindReplace`/`FindReplacePlus` enum，避免两个 enum 的数值和语义差异泄漏进 engine。

## 6. 执行算法

### Phase 0：资格判断

1. 读取整组 entries，但不改变 SearchEditor 完成状态。
2. 若存在 Marked Text，第一版整组走 legacy 路径，并记录 telemetry 原因；禁止一半规则走新引擎、一半走旧引擎。
3. 识别 Python function、pre-search、OPF/NCX 等能力，整组必须全部被当前 batch engine 支持才能启用。

### Phase 1：一次保存和快照

1. `SaveTabData()` 一次。
2. 记录当前 resource、BookBrowser selection、打开 tabs、全部 HTML/CSS、OPF、NCX 和其他 selected resources。
3. 对目标 `TextResource` 调用 `InitialLoad()`。
4. 保存 `bookpath → Resource*`、原始文本 fingerprint 和资源顺序。
5. 编译全部规则；任意 controls/regex/function 错误立即退出，零写入。

scope 必须按每条规则解析，但数据源只能来自同一个 snapshot。`force_all=true` 的 Replace All 语义不依赖方向和 wrap；资源顺序仍保持旧实现顺序。

### Phase 2：staging

```text
for rule in originalRuleOrder:
    executor.beginRule(rule)       # Python 在这里建立一次 session
    for resource in snapshot.resolve(rule.scope):
        text = store.readWorking(resource.bookpath)
        result = executor.apply(rule, resource.bookpath, text)
        ruleResult.replacementCount += result.replacementCount
        if result.text != text:
            store.stage(resource.bookpath, result.text)
    executor.endRule()
```

关键约束：

- 必须保持规则在外、资源在内；后续规则读取前一规则的 staged result。
- replacement count 按旧实现累计，即使替换结果与输入相同也保留计数。
- 只有 `result.text != text` 才更新 working text。
- staging 阶段不调用 `SetText()`、`SetModified()`、`ContentChangedExternally()` 或 `RepoCommit()`。
- 进度事件仅使用 `QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents)`。
- 取消立即销毁 staging store；零 checkpoint、零资源写入。

### Phase 3：验证与冲突检测

提交前执行：

1. 所有 staged XHTML/XML 仍满足既有 well-formed 约束。
2. 每个目标资源当前文本 fingerprint 与 Phase 1 相同。
3. Book、FolderKeeper、目标 Resource 指针仍有效，bookpath 未移动/重命名。
4. staged store 可完整读取，临时文件 hash 匹配。
5. 若没有实际变化，直接返回成功；不创建 checkpoint、不标记 Book modified。

任何冲突都中止整个批次，不做部分提交。

### Phase 4：单次 recovery snapshot

正式切换前必须提供新的 checkpoint API，例如：

```cpp
CheckpointResult MainWindow::CreateRecoverySnapshot(
    const QString& reason,
    CheckpointMutationMode mode = CheckpointMutationMode::DoNotTouchBookMetadata);
```

要求：

- 不调用 `OPFResource::AddModificationDateMeta()`；
- 不改变 Book modified 状态；
- 不再次调用 `SaveTabData()`；
- 可以把 Phase 1 的原始资源状态写入 repo/临时快照；
- 失败时返回结构化错误，并保证正式写回尚未开始。

如果短期内无法提供非变异式 snapshot，则 batch engine 不得支持 OPF scope。不能用“checkpoint 后重新覆盖 OPF”的方式规避冲突。

### Phase 5：统一提交

1. 进入不可取消的短提交段。
2. 再次执行 lightweight fingerprint precondition。
3. 按稳定资源顺序，对每个 changed resource 获取写锁并调用一次 `SetTextAsUndoableEdit(stagedText)`。
4. 校验 `GetText() == stagedText`；若异常，使用保留的 original text 回滚已应用资源并报告 fatal error。
5. 仅调用一次 `Book::SetModified()`。
6. 对 changed open tabs 汇总刷新；同一 tab 最多一次。当前 tab 的 `ContentChangedExternally()` 最多一次。
7. 提交成功后才统一 `RecordEntryAsCompleted`。

正常成功路径不要求立即再次 `SaveAllResourcesToDisk()`；`Resource`/`QTextDocument` 是当前编辑状态的事实来源，磁盘由正常保存流程处理。recovery snapshot 保存的是修改前状态。

## 7. staging 存储策略

### 7.1 默认内存后端

使用 `QHash<QString, StagedText>`，按需加载，只保存被规则触达的文本。利用 QString implicit sharing，未变化文本不复制。每个资源只保留 original fingerprint、working text 和 changed flag。

优点：实现简单、速度最快、失败时销毁即可。对于常规 EPUB 应作为默认路径。

### 7.2 大书 spill-to-temp 后端

当估算 working text 超过可配置上限时切换到 `QTemporaryDir`：

- 临时目录由 Qt 安全创建，不使用可预测路径；
- 文件名使用递增 ID，不直接暴露 bookpath；
- UTF-8 写入使用 `QSaveFile`，并保存 SHA-256；
- 原始文本不重复落盘，除非 rollback 策略需要；
- RAII 清理；崩溃恢复不依赖 staging temp；
- 日志只写统计和 bookpath，不输出正文。

建议初始默认内存预算为 256 MiB，但最终值应通过 1000 × 50 fixture 和真实大书基线确定，并允许设置覆盖。内存与 temp 后端必须通过同一 golden test。

## 8. 特殊语义

### 8.1 Python function replacement

- 每条 Python 规则建立一次 Python environment，沿资源顺序复用。
- 输入必须是当前 staged text，而不是 `Resource::GetText()`。
- Python 抛错中止整组，零正式写回。
- 在兼容性测试完成前，包含 Python function 的整组回退 legacy，不允许混合提交。

### 8.2 pre-search

复用 `Utility::GetSearchInfoWithPreSearch()` 和现有捕获组偏移逻辑。每条规则必须在最新 working text 上重新计算 pre-search 区间，不能复用上一条规则的 offset。

### 8.3 Current File 与 Marked Text

- Current File、未启用 Marked Text：可把快照中的当前 resource 当作普通单资源 scope。
- Marked Text：第一版整组回退 legacy。后续只有在实现可随替换长度变化更新的 range tracker，并完成上下边界/零宽匹配测试后才能迁移。

### 8.4 Automate

三个 Automate 命令与 SearchEditor UI 必须调用同一个 coordinator。结构化结果映射为：

- `success=false`：编译、checkpoint、冲突、Python 或提交失败；停止 automate。
- `success=true && replacementCount==0`：触发 `OnFailedRunSavedSearchReplaceAll`。
- `success=true && replacementCount>0`：触发 `OnSuccessRunSavedSearchReplaceAll`。

不能用 `changedResourceCount` 代替 replacement count。

## 9. 失败与恢复矩阵

| 阶段 | 失败示例 | 资源写入 | Checkpoint | 处理 |
| --- | --- | ---: | ---: | --- |
| Compile | controls/regex 无效 | 0 | 0 | 报规则名和错误 |
| Snapshot | resource 消失 | 0 | 0 | 整组失败 |
| Stage | Python 异常 | 0 | 0 | 丢弃 staging |
| Stage | 用户取消 | 0 | 0 | cancelled result |
| Validate | malformed staged XML | 0 | 0 | 报资源和规则 |
| Conflict | 外部修改/路径变化 | 0 | 0 | 保留搜索选择，允许重试 |
| Recovery snapshot | repo/磁盘错误 | 0 | 0 | 整组失败 |
| Commit | postcondition 异常 | 可能部分 | 1 | 用 original text 回滚并标 fatal |
| Refresh | Preview 刷新异常 | 已全部 | 1 | 内容提交成功，报告 UI warning；不回滚文本 |

## 10. 测试与性能验收

### 10.1 纯引擎测试

- 普通、大小写敏感、regex、dot-all、minimal、UCP、text-only；
- capture groups、零宽匹配、lookbehind、`\R`、空 replacement；
- pre-search 和 pre-search 捕获区域；
- 有顺序依赖的规则链：规则 2 必须看到规则 1 输出；
- replacement 与原文相同：count 增加、changed 不增加；
- Python function 的 per-rule state 和异常传播；
- UTF-8 spill 与内存后端结果逐字一致。

### 10.2 scope 和集成测试

- Current/All/Selected/Tabbed HTML、CSS、OPF、NCX、SVG、JS、Misc XML；
- 批处理期间改变 UI selection 不影响 snapshot；
- 0 match：`SetText=0`、`Modified=0`、checkpoint=0；
- N 条规则只修改同一资源：`SetText=1`、该 tab Preview update ≤ 1；
- 多资源：`undoable resource writes == distinct changed resources`；
- 一组只 `SaveTabData=1`、recovery snapshot ≤ 1、Book modified ≤ 1；
- checkpoint 失败、冲突、取消、Python 失败均为零写入；
- 搜索完成状态只在 commit 成功后改变；
- Automate success/failed conditional 保持 replacement count 语义。

### 10.3 差分测试

在隔离的 QString fixture 上同时运行 legacy transformation 和新 runner，比较：

- 每条规则 replacement count；
- 每个资源最终文本，要求逐字符相等；
- 资源处理顺序和 Python 调用顺序；
- warning/error 分类。

差分测试通过前不得替换默认 UI 路径。

### 10.4 benchmark

至少建立：

- small：10 resources × 5 rules；
- medium：200 × 20；
- target：1000 × 50；
- mixed：HTML/CSS/OPF/NCX，包含 80% 零匹配规则；
- large-text：少量 5–20 MiB resource，用于验证 spill。

硬性验收不是单一 wall-clock 数字，而是：

```text
SaveTabData calls              = 1
recovery snapshots             = changed ? 1 : 0
undoable resource writes      <= distinct changed resources
Modified signals               <= distinct changed resources
current-tab external refreshes <= 1
final text and counts          = legacy baseline
```

性能目标：target fixture 相比 legacy 至少 3× 加速；small fixture 不应有超过 10% 的稳定回退。最终门槛以同机多轮中位数记录。

## 11. 初始分阶段路线与 v1 落地情况

### PR-1：可测性和低风险止损

状态：已完成核心项；信号次数目前由单写回结构保证，尚缺真实 Tab spy 集成测试。

- 抽出纯 QString replace functions；
- 零变化不调用 `SetText()`，覆盖普通、Plus、Python；
- 增加 `SetText`/`Modified`/Preview/checkpoint 计数测试；
- 建立 1000 × 50 baseline。

### PR-2：Rule compiler 与 staging runner

状态：runner、DTO、内存 staging 和顺序测试已完成；controls 仍由现有 UI 编译后冻结，纯 compiler 待后续拆分。

- DTO、普通 controls、Plus controls；
- 内存 staging；
- regex/pre-search；
- rule/resource order differential tests；
- 暂不接 UI。

### PR-3：非变异式 recovery snapshot

状态：已完成，并在复审中从直接 `SaveAllResourcesToDisk()` 修正为隔离临时树 materialization；故障注入测试待补。

- 从 `RepoCommit()` 分离“更新 OPF/保存”和“创建恢复快照”；
- checkpoint failure test；
- OPF scope conflict test。

### PR-4：普通 SearchEditor 与 Automate 接入

状态：已完成；Python 整批回退；Automate 可传播批次错误；未增加 feature flag。

- coordinator、scope snapshot、单次提交；
- 结构化结果；
- Python function 支持或整组 legacy fallback；
- feature flag：`search_batch_staging_v1`。

### PR-5：Plus 接入与临时文件后端

状态：Plus/pre-search/selected-files 已完成；staging spill-to-temp 未实现。

- pre-search 完整接入；
- selected-files 语义；
- spill-to-temp；
- 移除增强版 per-rule `RepoCommit()`。

### PR-6：默认启用和清理

状态：v1 已默认启用并保留显式兼容回退；真实 GUI fixture 和 legacy 清理尚未开始。

- 真实 EPUB/搜索模板回归；
- 默认启用 feature flag；
- 一个发布周期保留 legacy fallback 和诊断计数；
- 稳定后删除搜索组对 GUI `ReplaceAll()` 的循环依赖。

## 12. 审计门禁

每个实现 PR 必须附带：

1. 受影响路径和旧/新调用图；
2. 文本/计数差分结果；
3. `SetText`、`Modified`、Preview、SaveTabData、checkpoint 计数；
4. cancel、conflict、checkpoint failure 的零写入证明；
5. OPF scope 审计；
6. Python function 顺序审计；
7. 内存峰值与 temp cleanup 结果；
8. benchmark 原始数据、机器信息和中位数；
9. feature flag 回退验证；
10. `git diff --check`、目标测试和全量 CTest 结果。

## 13. 已确认决策与后续 backlog

当前建议已经给出默认答案：

- staging：v1 使用内存；超预算 spill 到安全临时目录留待后续；
- execution：第一版单线程；
- Marked Text：整组 legacy fallback；
- Python：完整支持前整组 fallback，不混合执行；
- checkpoint：新增非变异式 recovery snapshot，禁止直接把现有 `RepoCommit()` 放到 staging 后；
- commit：每个 changed resource 一次 `SetTextAsUndoableEdit()`，不立即进行第二次全书写盘；
- compatibility：最终文本、每规则 replacement count、资源顺序是硬约束。

v1 已在明确回退边界内把 `FindReplace::ReplaceAllSearch()` 和 `FindReplacePlus::ReplaceAllSearch()` 切换到新 runner。扩展 Python、Marked Text 或 spill 后端前，仍必须先完成对应差分和故障注入测试。

## 14. v1 实施审计记录

### 14.1 提交映射

| Commit | 内容 | 审计意义 |
| --- | --- | --- |
| `9290e4de3` | 无变化时不调用 `SetText()` | 即使兼容回退也避免 0-change 文档重建 |
| `e956aa4a4` | 纯内存 `SearchBatch::Runner` | 规则顺序、working text、计数和 fail-closed 与 UI 解耦 |
| `cf9052b85` | 分离 recovery checkpoint API | 批次不调用会更新 OPF modification date 的普通 checkpoint |
| `107ad0c57` | recovery snapshot 隔离 materialization | 修复复审发现的 `SaveAllResourcesToDisk()`/UUID 隐式文档变异 |
| `9f7a60df7` | 普通版与 Plus coordinator 接入 | 一次保存、一次 checkpoint、每资源一次写回、成功后完成 entries |
| `b185c6f30` | Automate 错误传播 | checkpoint、冲突或 commit 错误不再被标记为成功 |
| `7e1a11c3b` | 1000 × 50 benchmark | 固化目标规模、结果和写回上限验收 |
| `6679316b2` | commit postcondition 回滚补强 | 当前资源写入后校验失败时也纳入逆序回滚 |
| `861b02a9c` | 批处理失败消息翻译 | 补齐简中、繁中和日文覆盖 |
| `92ffa2dd7` | 搜索进度事件重入防护 | 修复 Plus Replace All 运行期间重复点击/关闭窗口导致的递归与析构 crash |
| `6289416cd` | 搜索写回保留撤销历史 | 成功写回按资源形成一个 undo command，保留更早的编辑历史 |

### 14.2 新调用链

```text
SearchEditor / SearchEditorPlus / Automate
→ LoadSearch 并冻结每条规则的 regex、replacement 和 resource paths
→ SearchBatchCoordinator
  → SaveTabData（一次）
  → snapshot original texts
  → SearchBatch::Runner
    → rule outer / resource inner
    → 后续规则读取 working text
    → staging 期间不访问 Resource 写接口
  → original-text conflict check
  → CreateRecoveryCheckpoint（仅有实际变化时一次）
    → 隔离临时树，不写 live Resource、OPF 或工作目录
  → second conflict check
  → unique changed resource SetTextAsUndoableEdit（每资源一个撤销步骤）
  → Book.SetModified（一次）
  → current tab external refresh（至多一次）
→ 整批成功后 RecordEntryAsCompleted
```

### 14.3 不变量复核

| 不变量 | v1 证据 | 结论 |
| --- | --- | --- |
| 后续规则看到前序输出 | runner 使用同一 `workingTexts`；顺序依赖测试 `A→B→C` | 通过 |
| replacement count 与 changed 分离 | same-text replacement 测试：count=2、changed=0 | 通过 |
| staging 失败零发布 | 前序成功、后序故意失败时 `changedTexts` 仍为空 | 通过 |
| cancel/missing target fail-closed | runner cancellation、missing-path 用例 | 通过 |
| checkpoint 失败前零资源写入 | coordinator 在第一个正式 `SetTextAsUndoableEdit()` 前完成 checkpoint | 结构审计通过 |
| checkpoint 不变异 live book | temp tree 从已加载文本/磁盘副本构造；UUID 只读，缺失即失败 | 结构审计通过 |
| 每 changed resource 最多一次写回 | `changedTexts` 按 bookpath 唯一；`commitOrder` 用 `QSet` 去重 | 结构审计通过 |
| 冲突不覆盖外部修改 | checkpoint 前后各比较 original full text，提交锁内再次比较 | 通过 |
| 部分 commit 可回滚 | 写入前登记 applied path，失败时逆序恢复 original text（含 postcondition 失败的当前资源） | 结构审计通过 |
| entry 完成状态不早于提交 | 两套 SearchEditor 均只在 coordinator success 后统一完成 | 通过 |
| Automate 失败可见 | `ReplaceAllSearch() < 0` 映射为 `success=false` | 通过 |

### 14.4 benchmark 记录

fixture：1000 resources × 50 ordered rules × 256 tokens/resource，共 50,000 次规则/资源计算和 12,800,000 次 replacement。

- 5 次 staging wall-clock：271、275、270、272、283 ms；中位数 272 ms；
- legacy write candidates：50,000；
- staged final write candidates：1,000；
- 结构性写回/渲染上限缩减：50×；
- 机器：Darwin 24.6.0 x86_64，Intel Core i5-12400，64 GiB RAM；
- 工具链：CMake 4.0.2、Apple `/usr/bin/c++`、Qt 6.7.3，Debug build。

wall-clock 只衡量 staging runner，不伪装成完整 GUI legacy 对比；主要性能结论来自可验证的写回次数上限。完整 UI Preview/Modified signal spy 和真实 EPUB 中位数仍列入后续回归工作。

### 14.5 构建与测试记录

- CMake configure：通过；
- `Sigil` Debug 完整目标编译与链接：通过；
- `search_batch_runner`：通过；
- `search_batch_benchmark`：通过；
- `booklive_paragraph_normalizer`：通过，确认本次性能改造未回归 BookLive 插件；
- 全量 CTest：41 项中 38 项通过；3 个语言覆盖检查仍失败。

新增的 `FindReplace` / `FindReplacePlus` 批处理错误消息已在简中、繁中、日文目录补齐，复跑后不再出现在 missing-source 清单。剩余 3 个失败均为本批处理范围外的既有翻译目录债务：BookBrowser overwrite 消息、DryRunReplace 两条消息、OPF duplicate-path 消息、若干 stale source，以及 PluginRunner 未翻译 UI literal。它们不影响本次功能测试和链接结果，未在本提交中扩大范围处理。

## 15. 2026-08-09 crash 复审

证据来源：`todo/log/0809.txt`，macOS crash report，主线程 `EXC_CRASH/SIGABRT`。

### 15.1 堆栈结论

这次 crash 不在新 `SearchBatchCoordinator` / `SearchBatch::Runner` 路径中，而在用户直接点击 Plus `Replace All` 时采用的旧交互路径中：

```text
FindReplacePlus::ReplaceAllClicked
→ FindReplacePlus::ReplaceAll
→ CountInFilesPlus / ReplaceInAllFIlesPlus
→ qApp->processEvents()                  # 无事件过滤
→ 再次接收 QToolButton mouseRelease
→ 递归进入 ReplaceAllClicked
```

crash 栈中可见 7 层 `ReplaceInAllFIlesPlus()`、1 层 `CountInFilesPlus()`，每层之间都是 Cocoa/Qt mouse event dispatch。最外层搜索尚持有 `Resource*` 和 Find/Replace 对象时，嵌套事件又投递了 `MainWindow` 的 deferred delete；最终在 `MainWindow::~MainWindow()` / `QObjectPrivate::deleteChildren()` 中触发 malloc abort。

根因是进度刷新调用无参数 `processEvents()`，允许按钮、快捷键和窗口关闭等用户输入在非可重入资源循环中执行。性能优化放大或引入此问题的证据不足；堆栈明确命中 legacy Plus 单次替换，但同样的危险调用也存在于普通 Count/Replace 和 Python function replacement，因此必须统一修复。

### 15.2 修复与门禁

- `SearchOperations` 的 5 个跨资源循环统一改用 `QEventLoop::ExcludeUserInputEvents`；进度和非输入事件仍可处理；
- 普通/Plus 的 Find、Replace、Replace All、Count 按钮槽统一检查并维护 `m_SearchRunning`，程序化递归激活也会被拒绝；
- 新增 `search_operations_reentrancy` 源契约测试，禁止重新引入无过滤 `qApp->processEvents()`，并检查 8 个按钮槽均有 busy guard；
- Sigil Debug 完整编译、链接通过；新增重入测试、batch runner、1000 × 50 benchmark 和 BookLive normalizer 回归通过；
- 全量 CTest 38/41 通过，仍只有第 14.5 节记录的 3 个既有翻译覆盖失败。

修复后，搜索循环不会再从进度刷新入口接收造成堆栈中该模式的 mouse-release/window-close 重入；重复输入最多在外层操作结束、对象状态恢复后由正常事件循环处理。

## 16. Undo 可用性复审

用户复测发现保存搜索批处理结束后 Undo 不可用。根因不是 Undo action 的菜单状态：`TextTab` / `FlowTab` 已从当前 `QTextDocument::isUndoAvailable()` 读取状态，且 `undoAvailable` 信号会触发 MainWindow 刷新。真正的问题是搜索写回调用 `TextResource::SetText()`，其 GUI 线程实现最终调用 `QTextDocument::setPlainText()`；Qt 会把它视为重新装载全文并清空既有 undo/redo stack。

该问题同时影响两类路径：

- 新 coordinator 的成功提交，因此即使 Current File 只写回一次，Undo 仍会消失；
- legacy 普通/Plus 跨资源替换和 Python function replacement，它们同样通过 `SetText()` 重建全文。

修复新增了明确区分“装载内容”和“用户可撤销编辑”的写接口：

1. `TextDocument::replaceTextAsSingleUndoStep()` 用 `QTextCursor::Document` selection 和单个 edit block 替换全文；同文输入直接 no-op；
2. `TextResource::SetTextAsUndoableEdit()` 只允许 GUI 线程调用，不经过 `setPlainText()`；
3. `HTMLResource` override 保留 `TextChanging`、caret restoration、linked-resource tracking 和 Preview 更新语义；
4. 普通、Plus、Python 的跨资源搜索，以及 staged saved-search commit，全部切换到 undoable 接口；
5. coordinator 在多条规则都修改同一资源时仍只发布最终文本一次，因此该资源只增加一个撤销步骤。

Undo 的原子性边界是单个 `TextResource`，与 Sigil 当前按标签页/文档维护 undo stack 的架构一致。当前标签页可立即撤销本资源的整批最终变化；其他已修改资源各自在自己的文档中保留一个撤销步骤。这不是跨多个资源的全局原子 Undo，整书级回退仍由批处理前 recovery checkpoint 提供。

唯一例外是 coordinator 在极少见的 commit postcondition 失败后执行的 fail-safe rollback：该失败路径继续用装载式 `SetText(original)` 强制恢复已发布资源，并可能清空相应文档的 undo stack。因为批次最终报告失败且不保留用户可见修改，此处优先保证原文恢复；正常成功路径不经过该分支。

新增测试门禁：

- `text_document_undo`：验证一次 undo 精确恢复批处理前全文和 clean state，更早的 undo 历史仍可用，redo 可恢复批处理结果，NBSP/换行逐字符保真，同文写入不产生空 command；
- `search_undo_contract`：验证普通、Plus、Python 和 coordinator 的成功写回均使用 undoable 接口，并保留 HTML 通知链；
- 定向搜索/BookLive 回归 6/6 通过；Sigil Debug 完整编译链接通过；全量 CTest 38/41 通过，仍只有第 14.5 节所列 3 个既有翻译覆盖失败。
