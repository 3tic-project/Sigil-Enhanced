# Native Agent 原生段落计划工具

Native Agent 可以直接复用“DIV 段落结构规范化”的 C++ 分类、CSS 风险和源码补丁
引擎。模型只编排范围与审批绑定，不读取全书正文后自行生成替换，也不使用正则模仿
原生规则。

这三个工具目前只注册在内置 Native Agent。它们不是 `sigil.*` 公共 MCP catalog 的
一部分；外部 MCP 客户端仍使用既有 Live v2 事务工具。

## 工作流

| 阶段 | 工具 | 对 Book 的影响 |
|---|---|---|
| 分析 | `paragraphs.analyze` | 只读；返回候选、受保护范围、CSS 依赖和诊断 |
| 计划 | `paragraphs.plan` | 只读；只接受分析中状态为 `apply` 的资源，返回有界源码差异和审批摘要 |
| 暂存 | `paragraphs.apply` | 重新校验后创建独占暂存事务；活书仍不变 |
| 检查 | `transaction.preview` | 查看实际暂存资源；不应用 |
| 应用 | `transaction.commit` | 再次核对书籍修订后写入当前 Book；EPUB 文件仍未保存 |

不要在 `paragraphs.apply` 前调用 `transaction.begin`。`apply` 必须独占地创建自己的
事务，以保证已审阅计划与暂存内容是一批不可拼接的变更。Plan 模式允许进行到预览，
但不能 commit；Edit 模式中的暂存和最终 commit 都服从现有批准策略；Auto 模式不弹出
批准。

## 分析和选择

`paragraphs.analyze` 的 `resource_ids` 可指定 XHTML 范围；省略表示全部 XHTML。
默认使用 `conservative-v1`，只启用安全正文类别。下列布尔参数默认为 `false`：

- `convert_blank_lines`
- `convert_scene_breaks`
- `convert_image_wrappers`
- `convert_nested_blocks`

每个文件会报告 `apply`、`review`、`skip` 或 `error`，以及候选/受保护源码范围、
CSS 依赖、输入与输出哈希。范围和依赖列表各最多返回 128 项，避免把整章或大型样式
分析结果塞进模型上下文；总数仍在摘要字段中保留。

`paragraphs.plan` 必须带当前 `analysis_id`。省略 `resource_ids` 时只选全部 `apply`
资源；显式列表也不能加入 `review`、`skip` 或 `error` 文件。计划返回：

- `plan_id`、`plan_digest`、`book_revision`；
- 每份 XHTML 的路径、修订、XHTML/CSS 前后哈希；
- 最多 4,096 个 UTF-16 code unit 的前后源码摘录；
- `changes_css=false`、`changes_opf=false`、`adds_resources=false`；
- `local_validation=passed` 和 `full_epubcheck.status=not_run`。

`local_validation` 只表示原生 XHTML/CSS 与结构不变量检查，不表示完整 EPUBCheck。

## 审批绑定与失败边界

`paragraphs.apply` 要求原样传回 `plan_id + plan_digest + expected_book_revision`。
暂存前还会重新读取资源路径、XHTML 修订/内容、关联 CSS 内容，并重建确定性计划。
任何一项变化都会拒绝旧计划，不能用后台重试绕过重新分析。

计划只保存在当前 Agent controller 的内存中；New Session、换书或 controller 重建后
不能复用。一次新分析会废弃旧计划。常见错误包括：

- `ANALYSIS_NOT_FOUND` / `PLAN_NOT_FOUND`：当前书籍会话没有对应状态；
- `ANALYSIS_BINDING_MISMATCH` / `PLAN_BINDING_MISMATCH`：ID、摘要或修订不一致；
- `PLAN_SELECTION_UNSAFE`：请求包含非自动安全资源；
- `ANALYSIS_STALE` / `PLAN_STALE` / `BOOK_REVISION_CONFLICT`：XHTML、CSS、路径或书籍已变化；
- `TRANSACTION_OPEN`：已有事务，须先预览、提交或回滚；
- `CANCELLED`：取消发生在分析、重建或暂存期间；
- `STAGING_ROLLED_BACK`：多文件暂存中途失败，已丢弃整个独占事务。

`paragraphs.apply` 成功只返回 `applied_to_book=false` 和
`requires_transaction_commit=true`。最终 commit 后应向用户报告“已应用到当前书籍，
尚未保存”；不能把暂存、局部校验或 Agent 回答完成误报为已保存 EPUB。

## 验证

```sh
cmake --build build --target Sigil agent_div_paragraph_tools_test agent_harness_test agent_typeset_test -j2
ctest --test-dir build --output-on-failure \
  -R '^(agent_div_paragraph_tools|agent_harness|agent_typeset|booklive_paragraph_normalizer|div_paragraph_normalization_contract)$'
```

自动测试覆盖跨会话拒绝、计划摘要、CSS 变化但书籍计数未更新的冲突、Ruby/标题/空行
保留、幂等、取消、第二个文件暂存失败后的整批回滚，以及 Edit 模式审批拒绝时工具不
启动。当前未执行完整 EPUBCheck、真实书籍视觉比较或 Windows/Linux Native Agent
交互，因此这些仍是发布验收边界。
