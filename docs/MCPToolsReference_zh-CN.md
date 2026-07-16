# Sigil Enhanced MCP 工具 API 手册

## 1. 版本与通用约定

| 项目 | 值 |
| --- | --- |
| Adapter | `Sigil Enhanced MCP 0.1.0` |
| MCP spec | `2025-11-25` |
| Python SDK | `mcp>=1.28.1,<2`，发布包固定 `1.28.1` |
| Live API | v2 / protocol 1 |
| 位置编码 | UTF-16 code units |
| 传输 | Streamable HTTP；stdio proxy |
| endpoint | 每个打开的 Book 一个 `/mcp` |
| 写事务 | 每 endpoint 最多一个 active transaction |

Resource ID 和 revision 只在当前 Book Session 内有效。Book 关闭后不得缓存或复用。

所有工具返回 JSON-compatible structured result，同时由官方 SDK生成兼容文本 content。
预期宿主错误作为 MCP tool error 返回，错误文本包含机器可读 JSON：

```json
{
  "code": "RevisionConflict",
  "message": "...",
  "retryable": true,
  "recovery": "...",
  "data": {}
}
```

## 2. Tool Annotations

| 类别 | `readOnlyHint` | `destructiveHint` | 说明 |
| --- | --- | --- | --- |
| 查询 | `true` | `false` | 不修改 Book 或 UI。 |
| 导航 | `false` | `false` | 只改变当前标签、光标或选区。 |
| stage | `false` | `false` | 改变 transaction staged view，不改变 live Book。 |
| editor/commit/delete/move | `false` | `true` | 修改 live 内容或可能删除/替换内容。 |

Annotations 只是 MCP Host UI 提示，不是授权判定。

## 3. Session 与能力

### `sigil.session.info`

参数：无。

返回：

- `server.name/version`
- `mcp_protocol`
- `live_api.api_version/protocol_version/position_encoding/max_message_size`
- `session.session_id/lifetime/transaction_active/transaction_idle_timeout_seconds`
- `book` 当前 Book 信息

不会返回 bearer token 或 metadata 文件路径。

### `sigil.capabilities.list`

参数：无。

返回稳定的 `tools`、`resources`、`prompts` 清单，以及：

- `resource_page_size_max=500`
- `read_many_max=100`
- `editor_edits_max=1000`
- `position_encoding=utf-16`
- `active_transactions=1`
- transaction idle timeout
- 当前可用 native `enhancements`，首版为空列表

## 4. Book 与资源查询

### `sigil.book.info`

参数：无。

返回宿主 `book.getInfo`：

```json
{
  "epub_version": "3.0",
  "modified": true,
  "file_path": "/path/book.epub",
  "revision": 8
}
```

### `sigil.book.package`

参数：无。

返回：

- `metadata`：有序 metadata items、原始 XML、package attributes、OPF revision；
- `manifest`：有序 manifest entries；
- `spine`：items、attributes、OPF revision；
- `guide`：EPUB 2 guide；
- `bindings`：EPUB 3 media type bindings。

大型 Book 仍受 Live API 单消息预算限制。不要在每次小编辑前重复取完整 package。

### `sigil.resource.list`

参数：

| 参数 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `types` | `string[]` | 否 | 例如 `html`、`css`、`xml`、`text`、`opf`、`ncx`。 |
| `page_size` | integer | 否 | 1-500，默认 100。 |
| `cursor` | string/null | 否 | 上次原样返回的 opaque `next_cursor`。 |

返回 `items` 和 `next_cursor`。每个 item 包含：

- `id`
- `book_path`
- `media_type`
- `resource_type`
- `revision`
- `loaded`

### `sigil.resource.read_text`

参数：`resource_id`。

返回 `resource_id`、当前内存 `text` 和 `revision`。只接受 text resource。

### `sigil.resource.read_many`

参数：`resource_ids`，1-100 个。

返回 `items`。SDK 会自动处理宿主 6 MiB response budget 产生的 continuation，最终结果仍受
MCP 8 MiB 消息限制。大型任务应主动分批。

## 5. Editor 查询与导航

### `sigil.editor.state`

参数：无。

返回：

- `active`
- `resource_id`
- `book_path`
- `revision`
- `cursor`
- `selection.start/end/text`
- `position_encoding=utf-16`

### `sigil.editor.tabs`

参数：无。返回 `items`，元素与 resource list item 相同。

### `sigil.editor.open`

参数：

- `resource_id`：必需；
- `position`：可选 UTF-16 offset。

返回新的 editor state。只改变 UI 状态。

### `sigil.editor.reveal`

参数：`resource_id`、`start`、`end`。打开资源并选择 UTF-16 range。

## 6. Editor 立即写入

### `sigil.editor.edit`

参数：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `resource_id` | string | 必须是当前打开的 editor resource。 |
| `expected_revision` | integer | 来自最新 editor state/read。 |
| `edits` | object[] | 1-1000 个 `{start,end,text}`。 |
| `label` | string | 可选 undo label。 |

范围必须有界、互不重叠且不能切开 surrogate pair。宿主按位置逆序在一个 Qt edit block 中应用，
形成一个 undo step。

### `sigil.editor.replace_selection`

参数：`resource_id`、`expected_revision`、`text`、可选 `label`。只替换当前活动选区。

### `sigil.editor.insert_text`

参数：`resource_id`、`expected_revision`、`text`、可选 `label`。只在当前活动光标插入。

Editor 三个写工具立即修改 live Book，不属于 staged transaction。

## 7. Transaction 生命周期

### `sigil.transaction.begin`

参数：

- `label`：默认 `MCP changes`；
- `checkpoint`：`auto`、`always` 或宿主支持的策略。

返回：

```json
{
  "transaction_id": "...",
  "base_book_revision": 8,
  "checkpoint": "auto",
  "idle_timeout_seconds": 300
}
```

已有 transaction 时返回 `Busy`。

### `sigil.transaction.read_text`

参数：`transaction_id`、`resource_id`。

返回 staged text（如已修改），否则返回 live text；包含本事务可继续使用的 revision。

### `sigil.transaction.preview`

参数：`transaction_id`。

返回：

- `valid`
- `conflicts`
- `summary.modified/added/deleted/renamed`
- `text_changes`
- `binary_changes`
- `structure_changes`
- `opf_changes`
- `warnings`

不修改 live Book。

### `sigil.transaction.validate`

参数：`transaction_id`。重新检查 revision、resource 和 package invariants，不提交。

### `sigil.transaction.commit`

参数：`transaction_id`。

流程：

1. Adapter 获取 preview；
2. Sigil 显示 native summary confirmation；
3. 用户确认后宿主再次 validate revision/package；
4. 按策略创建 Checkpoint；
5. 每个最终资源只应用一次；
6. 成功释放 writer lease。

用户拒绝时返回：

```json
{
  "committed": false,
  "confirmed": false,
  "transaction_id": "...",
  "preview": {}
}
```

此时 transaction 仍然有效。

### `sigil.transaction.rollback`

参数：`transaction_id`。丢弃 staged state 并释放 writer lease。

## 8. Transaction 文本与资源写入

### `sigil.transaction.replace_text`

参数：`transaction_id`、`resource_id`、`expected_revision`、完整 `text`。

### `sigil.transaction.apply_edits`

参数：`transaction_id`、`resource_id`、`expected_revision`、`edits`。

edits 与 editor edit 使用相同 UTF-16 `{start,end,text}` 结构，但针对 transaction 当前 staged
文本组合。对同一资源多次 stage，commit 时仍只写入最终文本一次。

### `sigil.transaction.add_text_resource`

参数：

| 参数 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `transaction_id` | string | 是 | 当前 handle。 |
| `book_path` | string | 是 | canonical Book path，例如 `Text/chapter.xhtml`。 |
| `text` | string | 是 | 新资源完整文本。 |
| `media_type` | string | 是 | 例如 `application/xhtml+xml`、`text/css`。 |
| `manifest_id` | string/null | 否 | 空值时由宿主生成/处理。 |
| `properties` | string/null | 否 | OPF manifest properties。 |
| `add_to_spine` | boolean | 否 | 默认 `true`。CSS 应设为 `false`。 |
| `manifested` | boolean | 否 | 默认 `true`。 |

该工具用于生成章节、CSS、SVG、XML 等文本资源。不接受 binary/base64。

### `sigil.transaction.remove_resource`

参数：`transaction_id`、`resource_id`、`expected_revision`。

OPF、nav、最后一个 XHTML 等保护规则由宿主执行。

### `sigil.transaction.move_resource`

参数：`transaction_id`、`resource_id`、目标 `book_path`、`expected_revision`。

宿主在 commit 时处理资源路径和引用更新。不能用绝对路径或 traversal path。

### `sigil.transaction.rename_resource`

参数：`transaction_id`、`resource_id`、新 `filename`、`expected_revision`。

只修改当前 Book 目录中的文件名。

## 9. Transaction OPF 写入

### `sigil.transaction.replace_package`

参数：`transaction_id`、完整 OPF `text`、`expected_revision`。

这是 authoritative package replacement。宿主校验：

- XML/package 结构；
- EPUB version 不改变；
- manifest ID/href 唯一；
- spine idref 存在；
- 与 staged add/remove/move 的最终资源集合一致。

### `sigil.transaction.update_metadata`

参数：

- `transaction_id`；
- `items`：有序 `{name,content,attributes?}`；
- `expected_revision`：可选，未提供时 SDK 先读取 OPF revision。

该调用替换 metadata 全部子元素。prefix 必须能在 package namespace 中解析。

### `sigil.transaction.update_spine`

参数：

- `transaction_id`；
- `items`：有序 `{idref,id?,linear?,properties?}`；
- `attributes`：可选 spine attributes；
- `expected_revision`：可选。

所有 `idref` 必须存在于最终 manifest。

## 10. MCP Resources

| URI | MIME | 内容 |
| --- | --- | --- |
| `sigil://book/info` | `application/json` | Book info。 |
| `sigil://book/metadata` | `application/json` | structured metadata。 |
| `sigil://book/manifest` | `application/json` | manifest entries。 |
| `sigil://book/spine` | `application/json` | spine。 |
| `sigil://editor/state` | `application/json` | 当前 editor state。 |
| `sigil://resource/{resource_id}` | `text/plain` | 当前 text resource 内容。 |

URI 只在当前 Book endpoint 内有意义。

## 11. MCP Prompts

### `edit_epub_safely(task)`

通用 revision-safe 工作流：先读、选择 editor/transaction 路径、preview、validate、commit。

### `generate_chapter(topic, book_path)`

要求模型先检查 EPUB 版本、相邻 XHTML、CSS、manifest 和 spine，再生成合法章节。

### `layout_epub(goal)`

要求模型使用语义 XHTML、可复用 CSS、相对路径和 EPUB 兼容属性，以一个 transaction 修改。

### `repair_epub(issue)`

要求先复现和定位问题，只做最小改动；冲突时重新读取，不能覆盖。

## 12. 完整工具清单

```text
sigil.session.info
sigil.capabilities.list
sigil.book.info
sigil.book.package
sigil.resource.list
sigil.resource.read_text
sigil.resource.read_many
sigil.editor.state
sigil.editor.tabs
sigil.editor.open
sigil.editor.reveal
sigil.editor.edit
sigil.editor.replace_selection
sigil.editor.insert_text
sigil.transaction.begin
sigil.transaction.read_text
sigil.transaction.replace_text
sigil.transaction.apply_edits
sigil.transaction.add_text_resource
sigil.transaction.remove_resource
sigil.transaction.move_resource
sigil.transaction.rename_resource
sigil.transaction.replace_package
sigil.transaction.update_metadata
sigil.transaction.update_spine
sigil.transaction.preview
sigil.transaction.validate
sigil.transaction.commit
sigil.transaction.rollback
```
