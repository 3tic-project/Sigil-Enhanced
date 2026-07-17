# Sigil Enhanced MCP 工具 API 手册

## 1. 版本与通用约定

| 项目 | 值 |
| --- | --- |
| Adapter | `Sigil Enhanced MCP 0.7.0` |
| MCP spec | `2025-11-25` |
| Python SDK | `mcp==1.28.1`，开发与发布构建使用同一精确依赖锁 |
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
- `text_range_utf16_units_max=1048576`
- `text_write_size_max=67108864`
- `text_write_chunk_size_max=1048576`
- `external_import_size_max=33554432`
- `external_binary_add_size_max`：按当前 Live 单消息预算动态计算
- `editor_edits_max=1000`
- `position_encoding=utf-16`
- `active_transactions=1`
- transaction idle timeout
- `external_import`：原始字节 endpoint、认证方式、add/replace、text/binary，以及
  `batch_uploader_path` 指向的已安装批量上传器绝对路径
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

### `sigil.resource.read_text_range`

参数：`resource_id`、可选 `start=0`、可选 `max_utf16_units=1048576`。

返回有界 `text`、`start`、`end`、`total_utf16_units`、`revision`、`staged=false` 和可空
`next_start`。首段 `start=0` 还返回整文 `total_utf8_bytes` 与 `sha256`；后续段省略这两个
整文计算字段。`start`/`end` 均为 UTF-16 code units，范围不会切开代理项对。

### `sigil.resource.read_many`

参数：`resource_ids`，1-100 个。

参数：`resource_ids`（1-100 个）和可选不透明 `cursor`。

返回一页 `items` 和可空 `next_cursor`，页面保持在宿主 6 MiB response budget 内。后续调用
必须传相同 `resource_ids` 并原样带回 `next_cursor`；MCP adapter 不再把多页重新聚合成可能
超过 8 MiB 的响应。单个文本本身超过页面预算时，改用 `read_text_range`。

读取尚未加载的文本资源时，宿主会从当前 Book 工作目录惰性载入，但不会发出资源编辑事件、
增加内容 revision 或触发编辑器光标同步。

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
- `state_token`：活动资源、revision、光标与选区的 SHA-256 状态令牌
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

参数：`resource_id`、`expected_revision`、`expected_state_token`、`text`、可选 `label`。
只替换读取该 token 时的活动选区；用户只移动选区也会返回 `RevisionConflict`。

### `sigil.editor.insert_text`

参数：`resource_id`、`expected_revision`、`expected_state_token`、`text`、可选 `label`。
只在读取该 token 时的活动光标插入；用户只移动光标也会返回 `RevisionConflict`。

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

### `sigil.transaction.status`

参数：无，不需要预先知道 transaction ID。

始终返回正常结果。无事务时为 `active=false`、`transaction_id=null`；有事务时还返回
`base_book_revision`、`checkpoint`、`idle_seconds`、`expires_in_seconds`、
`pending_text_uploads` 和 `pending_external_imports`。用于重连、工具或上传超时、Agent 状态丢失后
判断应继续、rollback 还是新建事务。外部上传进行期间不会触发 idle rollback，commit 返回 `Busy`。

### `sigil.transaction.read_text`

参数：`transaction_id`、`resource_id`。

返回 staged text（如已修改），否则返回 live text；包含本事务可继续使用的 revision。

### `sigil.transaction.read_text_range`

参数：`transaction_id`、`resource_id`、可选 `start=0`、可选
`max_utf16_units=1048576`。返回结构与资源范围读取一致，但可读取已修改文本和新资源的
`staging_id`，并通过 `staged` 标记事务视图。

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

1. 宿主再次 validate revision/package；
2. 按策略创建 Checkpoint；
3. 每个最终资源只应用一次；
4. 成功释放 writer lease。

成功返回包含：

```json
{
  "committed": true,
  "transaction_id": "...",
  "confirmation_required": false
}
```

commit 不显示原生确认对话框。调用者必须先显式调用 `preview` 和 `validate`；需要放弃 staged
changes 时，应在 commit 前调用 `rollback`。

### `sigil.transaction.rollback`

参数：`transaction_id`。丢弃 staged state 并释放 writer lease。

## 8. Transaction 文本与资源写入

### `sigil.transaction.replace_text`

参数：`transaction_id`、`resource_id`、`expected_revision`、完整 `text`。

### `sigil.transaction.apply_edits`

参数：`transaction_id`、`resource_id`、`expected_revision`、`edits`。

edits 与 editor edit 使用相同 UTF-16 `{start,end,text}` 结构，但针对 transaction 当前 staged
文本组合。对同一资源多次 stage，commit 时仍只写入最终文本一次。

### 分块文本替换与新增

长文本不应通过一次 `replace_text` 或 `add_text_resource` 发送。使用以下状态机：

1. 现有或已暂存文本调用 `sigil.transaction.begin_text_write`，参数为
   `transaction_id`、`resource_id`、`expected_revision`、UTF-8 `size`；
2. 新文本资源调用 `sigil.transaction.begin_text_resource`，参数为 `transaction_id`、
   `book_path`、UTF-8 `size`、`media_type`，以及 manifest 相关可选项；manifested 资源必须
   提供唯一 `manifest_id`；
3. 顺序调用 `sigil.transaction.write_text_chunk`，传 `transaction_id`、`upload_id`、严格
   UTF-8 字节 `offset` 和 `text`；
4. 完成后调用 `sigil.transaction.finish_text_write`；若放弃则调用
   `sigil.transaction.abort_text_write`。

begin 返回 `upload_id`、`chunk_size`、`max_size` 和 `expected_size`。单文档最多 64 MiB，宿主
单块最多 1 MiB，MCP adapter 会按 Unicode 字符边界继续拆分过大的工具参数。宿主只接受连续
字节偏移，并允许最后一个已接受块按相同 offset、内容重试。finish 由 adapter 提供累计
SHA-256，宿主核对声明长度、UTF-8 round trip 与哈希后才写入 staged view。

新资源完成后返回的 `staging_id` 可继续用于 `read_text_range`、`replace_text`、`apply_edits`
或 `begin_text_write`。commit、rollback、事务超时和 Session 结束都会清除未完成上传。

### `sigil.transaction.add_text_resource`

参数：

| 参数 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `transaction_id` | string | 是 | 当前 handle。 |
| `book_path` | string | 是 | canonical Book path，例如 `Text/chapter.xhtml`。 |
| `text` | string | 是 | 新资源完整文本。 |
| `media_type` | string | 是 | 例如 `application/xhtml+xml`、`text/css`。 |
| `manifest_id` | string/null | manifested 时是 | manifested 资源必须提供唯一值。 |
| `properties` | string/null | 否 | OPF manifest properties。 |
| `add_to_spine` | boolean | 否 | 默认 `true`。CSS 应设为 `false`。 |
| `manifested` | boolean | 否 | 默认 `true`。 |

该工具用于生成章节、CSS、SVG、XML 等文本资源。
commit 会在资源加入 Book 后立即物化文本缓存；随后直接保存、创建 checkpoint 或批量读取都必须
保留传入的完整 `text`，不能用未加载的空文档覆盖文件。

### `sigil.transaction.add_binary_resource`

参数：

| 参数 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `transaction_id` | string | 是 | 当前 handle。 |
| `book_path` | string | 是 | canonical Book path，例如 `OEBPS/Images/cover.jpg`。 |
| `data_base64` | string | 是 | 严格 Base64；解码后最大 5 MiB。 |
| `media_type` | string | 是 | 例如 `image/jpeg`、`font/woff2`。 |
| `manifest_id` | string/null | manifested 时是 | manifested 资源必须提供唯一值。 |
| `properties` | string/null | 否 | OPF manifest properties。 |
| `add_to_spine` | boolean | 否 | 默认 `false`。 |
| `manifested` | boolean | 否 | 默认 `true`。 |

Base64 在进入 Live API 前严格校验，路径、manifest ID、media type 和最终 package invariants
仍由宿主验证。该工具只适合模型本身已经持有的少量小型二进制内容。本地图片、字体和生成文件
应使用第 12 节外部导入接口，避免 Base64 进入模型上下文。

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
调用时，宿主先把同一事务中此前完成的 manifested 新资源按 href 确定排序合并到 staged
manifest，再替换 spine。因此生成新书时应先 stage/finish 全部 XHTML、CSS、图片，再调用一次
`update_metadata` 和最终 `update_spine`，随后 preview、validate、commit；不再需要为了让
manifest ID 可见而中途 commit。若 `update_spine` 后又新增资源，必须重新调用它。

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

## 12. 外部原始文件导入 API

该接口不是 MCP tool，而是同一 Book Session 的本地数据通道。Agent 只需通过 MCP 获取
`transaction_id`，随后让本地上传器读取文件；图片、长 XHTML、CSS 或字体字节不会进入模型消息。

### 12.1 HTTP 契约

`POST /api/v1/imports`，使用 metadata 中的 `external_import_endpoint`。请求必须包含：

| 位置 | 名称 | 说明 |
| --- | --- | --- |
| Header | `Authorization` | 与 `/mcp` 相同的 `Bearer <token>`。 |
| Header | `Content-Length` | 原始文件精确字节数，最大 32 MiB。 |
| Header | `X-Content-SHA256` | 64 位十六进制 SHA-256。 |
| Query | `transaction_id` | 当前活动事务。 |
| Query | `kind` | `text` 或 `binary`；text 必须是严格 UTF-8。 |
| Query | `operation` | `add`（默认）或 `replace`。 |

add 还使用 `book_path`、`media_type`、`manifest_id`、`properties`、`fallback`、`overlay`、
`add_to_spine` 和 `manifested`。replace 使用 `resource_id` 与 `expected_revision`。成功响应为
HTTP 201，正文是正常 transaction staged result，并附加
`external_import.operation/kind/size/sha256`。

服务端不接受 `source_path`，不会代替调用者读取任意本机路径。请求先完整写入私有临时文件，
长度、哈希和 UTF-8 验证全部成功后才进入 staged view；任何失败都会删除临时文件。最多两个上传
并发。文本新增/替换和二进制替换可使用 32 MiB 上限；二进制新增仍受 Live 消息预算限制，调用
前读取 `external_binary_add_size_max`。

### 12.2 单文件上传器

```sh
python3 examples/live_plugins/SigilMcpServer/sigil_mcp_upload.py \
  --transaction TRANSACTION_ID \
  --file /absolute/source/cover.jpg \
  --book-path Images/cover.jpg \
  --media-type image/jpeg \
  --manifest-id cover-image \
  --no-add-to-spine
```

上传器路径应取自 `sigil.capabilities.list` 的 `external_import.batch_uploader_path`，不要假设当前
目录或 `PATH` 中存在脚本。上传器自动发现唯一活动 Book；多 Book 时必须加 `--metadata` 或
`--session-id`。manifested 新资源
必须提供 `manifest_id`。替换现有资源时改用 `--operation replace --resource-id ID
--expected-revision REVISION`。

### 12.3 批量清单

```json
{
  "resources": [
    {
      "source": "generated/chapter.xhtml",
      "book_path": "Text/chapter.xhtml",
      "media_type": "application/xhtml+xml",
      "manifest_id": "chapter"
    },
    {
      "source": "images/cover.jpg",
      "book_path": "Images/cover.jpg",
      "media_type": "image/jpeg",
      "manifest_id": "cover-image",
      "add_to_spine": false
    }
  ]
}
```

`source` 相对清单目录解析。begin 前执行 `sigil_mcp_upload.py --manifest imports.json --check`，
可在不发现 session、不建立网络连接的情况下检查全部源文件和清单字段，并拒绝批内重复 Book
path、manifest ID、replace resource ID 或混用 transaction ID。通过后加
`--transaction TRANSACTION_ID` 正式执行，上传器顺序暂存全部条目并只输出短 JSON 结果。
上传结束后仍须调用 transaction preview、validate 和 commit；
外部接口不会自动提交。若第 N 项失败，错误 JSON 会返回 `completed_indices`、`failed_index` 和
`next_index`；确认同一事务仍活动后，用 `--start-at next_index` 继续，不要重传已经暂存的条目。

## 13. 完整工具清单

```text
sigil.session.info
sigil.capabilities.list
sigil.book.info
sigil.book.package
sigil.resource.list
sigil.resource.read_text
sigil.resource.read_text_range
sigil.resource.read_many
sigil.editor.state
sigil.editor.tabs
sigil.editor.open
sigil.editor.reveal
sigil.editor.edit
sigil.editor.replace_selection
sigil.editor.insert_text
sigil.transaction.begin
sigil.transaction.status
sigil.transaction.read_text
sigil.transaction.read_text_range
sigil.transaction.replace_text
sigil.transaction.apply_edits
sigil.transaction.begin_text_write
sigil.transaction.begin_text_resource
sigil.transaction.write_text_chunk
sigil.transaction.finish_text_write
sigil.transaction.abort_text_write
sigil.transaction.add_text_resource
sigil.transaction.add_binary_resource
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
