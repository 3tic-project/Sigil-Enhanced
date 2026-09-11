# Native Agent 原生目录层级工具

Native Agent 可以复用“编辑目录”的 C++ 树变换和原节点写回能力，安全地提升或
降级既有目录项。它不通过标题标签重建目录，也不会为层级请求改写 XHTML
`h1`–`h6`。

这三个工具只注册在内置 Native Agent，不属于公共 `sigil.*` MCP catalog。

## 固定流程

| 阶段 | 工具 | 行为 |
|---|---|---|
| 检查 | `toc.inspect_hierarchy` | 只读；分页返回稳定节点 ID、父项、深度、标题和目标 |
| 计划 | `toc.plan_transform` | 只读；规划 `promote` 或 `demote`，返回有界的父项/深度差异 |
| 暂存 | `toc.apply_transform` | 重验已审计划并创建独占事务；活书仍不变 |
| 审阅 | `transaction.preview` | 显示 `toc_changed=true` |
| 提交 | `transaction.commit` | 将暂存层级写进当前 Book；EPUB 文件仍未保存 |

不要在 `toc.apply_transform` 前调用 `transaction.begin`。Plan 模式可以暂存和预览，
但不能 commit；Edit 模式会在 apply 前显示计划 ID、摘要和书籍修订供批准。

## 检查与计划

`toc.inspect_hierarchy` 默认返回 100 项，`limit` 范围为 1–500，可用
`next_offset` 继续读取。同一次 controller/Book 会话中的节点 ID 按原始先序稳定；
重新打开会话后必须重新检查，不能沿用旧 ID。

`toc.plan_transform` 要求传回当前 `snapshot_id`：

- `operation=promote` 将选择项提升一级；默认
  `adopt_following_siblings=true`，保持目录先序阅读顺序。
- `operation=demote` 将连续选择区段移到前一个同级项下，并保持先序。
- 父子同时选中、跨父项、顶层边界和无前驱区段使用与“编辑目录”相同的原生规则。
- 计划只改变父子关系。节点集合、先序、标题和目标必须完全相同。

响应最多列出 128 个受影响节点；更多变化用 `changes_truncated=true` 表示。计划会
明确报告 `changes_xhtml_headings=false`、`applied_to_book=false` 和
`full_epubcheck.status=not_run`。

## 绑定、暂存与冲突

`toc.apply_transform` 必须原样传回 `plan_id`、`plan_digest` 和
`expected_book_revision`。计划还在进程内绑定：

- 当前 Agent controller/Book 会话；
- 原生导航资源的 ID、路径和精确源码 SHA-256；
- 计划前的完整稳定节点树。

apply 会在开事务前重新读取源码身份和目录树。换书、New Session、书籍修订变化、
手工修改 Nav/NCX、旧摘要或跨会话计划都会被拒绝。开始暂存后，commit 还会再次比较
事务开始时的精确导航源码；用户在预览期间做的宿主编辑不会被覆盖。

apply 成功只表示计划进入工作区事务：返回 `requires_transaction_preview=true`、
`requires_transaction_commit=true` 和 `applied_to_book=false`。取消或暂存失败会回滚
这次独占事务。commit 后修改进入当前 Book 和文本资源 Undo，但用户仍需保存 EPUB。

## 源码保真边界

EPUB 3 使用主 Nav；EPUB 2 使用 NCX。写回会移动原有 Nav `<li>` 或 NCX
`<navPoint>`，保留节点属性、内联标题标记、标签和目标。Nav 只替换 TOC 的根列表，
landmarks、page-list、其他 nav 和外围内容保持不变；NCX 只替换 `navMap`，并按新先序
更新 `playOrder`，保留 `head`、`docTitle`、`docAuthor` 和 `pageList`。

EPUB 3 同时带兼容 NCX 时，本流程默认只改主 Nav，与“编辑目录”的默认行为一致；
不会暗中同步 NCX。它也不会增删目录项、修改标题/目标、生成缺失 Nav 或调整
landmarks/page-list。需要这些操作时使用相应的显式工作流。

## 验证

```sh
cmake --build build --target Sigil agent_toc_tools_test -j2
ctest --test-dir build --output-on-failure \
  -R '^(agent_toc_tools|agent_workspace_package_integration|toc_tree_transform|edit_toc_hierarchy_integration)$'
```

测试覆盖分页与稳定 ID、提升/降级、兄弟接管、跨会话和旧计划拒绝、取消、暂存失败
回滚、预览/提交以及提交前宿主冲突。macOS 原生集成使用真实 EPUB3 Nav，验证属性、
内联 `<span>`、landmarks、精确 Undo/Redo 和源码冲突。已有 EditTOC 集成覆盖相同
NCX 重挂器的 EPUB2 保真路径。最终 9 项相关测试各连续运行 3 次通过。

这些测试不等于完整 EPUBCheck、Windows/Linux Native Agent GUI、真实大型书籍、
独立阅读器或进程终止级事务验收。
