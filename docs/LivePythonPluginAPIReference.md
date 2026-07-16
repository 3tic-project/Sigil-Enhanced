# Sigil Live Python Plugin API v2 手册

本文是当前源码中已实现 API 的调用手册。协议的机器可读定义见
`plugin-api-v2.openrpc.json`，架构、兼容策略和设计理由见
`LivePythonPluginAPI.md` 与 `LegacyPythonPluginSystem.md`。

## 1. 插件入口

原生 v2 插件目录至少包含 `plugin.xml` 和 `plugin.py`：

```xml
<plugin>
  <name>My Live Plugin</name>
  <author>Author</author>
  <description>Example</description>
  <type>edit</type>
  <engine>python3</engine>
  <version>2.0.0</version>
  <api version="2" interface="live" />
  <lifetime>command</lifetime>
</plugin>
```

```python
def run(plugin):
    print(plugin.book.get_info())
    return 0
```

`run()` 返回 `None` 或 `0` 表示成功，其他值或未捕获异常表示失败。
`command` 在 `run()` 返回后结束；`book-session` 可在 `run()` 内持续消费事件，
在取消、关闭/替换 Book 或退出 Sigil 时由宿主终止。

## 2. 信任模型

Live v2 不实现 RPC 方法级权限系统。旧 `<permissions>` 元素仍可被解析和回写，保证已有
测试插件与清单兼容，但不会授予或限制任何 API。input、output、validation 专用操作仍检查
插件类型。Python 插件与 Sigil 使用同一系统账户，可直接使用 Python 的文件、网络和进程
API，因此只能安装和运行可信插件；本地 socket 鉴权不是操作系统沙箱。

### 新旧运行时兼容

插件管理器为每个插件保存 `Legacy (v1)` 或 `Live (v2)` 选择，默认仍为 Legacy。原生 v2
插件通过 `<api version="2" interface="live" />` 声明 Live 接口。未声明 v2 的旧 `edit`
插件也可在管理器中选择 Live，此时 launcher 提供 RPC-backed `CompatBookContainer`，保留旧
入口与公开 API；成功时提交隐式事务，非零返回、异常、取消、崩溃或提交失败时回滚。

旧 validation 与 output 插件在 Live 模式下使用只读兼容容器；旧 input 插件通过受限、校验
完整性的流上传生成的 EPUB，且只在插件成功结束后才替换当前 Book。菜单、QuickLaunch 和
Automate 共用同一运行时选择；Automate 会等待 Live command 完成，并拒绝不适用的
`book-session` 插件。旧系统完整行为见 `LegacyPythonPluginSystem.md`。

## 3. 数据类型

### `Resource`

属性：`id`、`book_path`、`media_type`、`resource_type`、`revision`、`loaded`。
`id` 只在当前 Book Session 内稳定；跨 Book 保存引用时应保存 `book_path` 并重新
`resolve_path()`。

### `EditorState`

属性：`active`、`resource_id`、`book_path`、`revision`、`cursor`、`selection`、
`position_encoding`。`Selection` 含 `start`、`end`、`text`。

所有编辑位置均为 UTF-16 code unit 偏移，与 Qt `QTextCursor` 一致。Python 字符串
索引在非 BMP 字符处不同，插件必须按 UTF-16 计算协议位置。

## 4. `Plugin`

| 成员 | 说明 |
| --- | --- |
| `session_info` | 握手结果：Session ID、lifetime、消息上限、Book revision 和 UI 信息。 |
| `book` | `BookApi`。 |
| `editor` | `EditorApi`。 |
| `ui` | `UiApi`。 |
| `events` | `EventsApi`。 |
| `validation` | `ValidationApi`。 |
| `input` | `InputApi`。 |
| `output` | `OutputApi`。 |
| `ping()` | 检查宿主连接。 |
| `finish(status="success", message="")` | 报告最终状态；通常由 launcher 调用。 |
| `close()` | 关闭 transport；通常由 launcher 调用。 |

`connect()` 依赖 launcher 注入的私有 socket 与一次性 token，普通插件不应自行建立连接。

## 5. `BookApi`

### Book 与 OPF 查询

| 方法 | 返回值 |
| --- | --- |
| `get_info()` | EPUB 版本、modified、当前文件路径、Book revision。 |
| `get_revision()` | 当前 Session 的单调 Book revision。 |
| `get_metadata()` | 有序 metadata、属性、原始 XML、package 信息与 OPF revision。 |
| `get_manifest()` | 有序 manifest；含 id、href、book path、类型、属性和 resource ID。 |
| `get_spine()` | 有序 spine 项、spine 标签属性与 OPF revision。 |
| `get_guide()` | EPUB 2 guide，含解析后的 book path 和 fragment。 |
| `get_bindings()` | EPUB 3 media-type handler bindings。 |
| `get_selection()` | Book Browser 当前选择的 `Resource` 列表。 |
| `get_compatibility_snapshot()` | v1 adapter 使用的启动状态；原生插件通常不需要。 |

### 资源读取

| 方法 | 返回值 |
| --- | --- |
| `resources(types=None, page_size=200)` | 分页 `Resource` 迭代器。 |
| `list_resources(types=None, page_size=200, cursor=None)` | 返回一页有界的 `Resource` 与不透明 `next_cursor`，供协议适配层转发分页。 |
| `text_resources()` | 文本资源迭代器。 |
| `resolve_path(book_path)` | 当前路径对应的 `Resource`。 |
| `get_resource(resource_id)` | 当前资源信息。 |
| `read_text(resource)` | `text` 与 `revision`。 |
| `read_text_range(resource, start=0, max_utf16_units=1024 * 1024)` | 按 UTF-16 范围有界读取文本、revision 与续读位置。 |
| `read_many(resources)` | 最多 100 个文本资源；SDK 自动跟随 6 MiB 响应预算的 continuation。 |
| `read_binary(resource)` | 最多 5 MiB 的 bytes 数据与 revision。 |
| `open_binary(resource)` | 二进制资源的分块 `BinaryReader` 快照。 |
| `materialize_temporary(resource=None, book_path=None)` | 将一个实时资源复制到 Session 私有临时目录。 |

### Expanded EPUB/archive

| 方法 | 返回值 |
| --- | --- |
| `archive_files(page_size=200)` | 展开 EPUB 中所有常规文件，包括未托管文件。 |
| `read_archive_file(book_path)` | 最多 5 MiB 的 bytes、SHA-256 与保护状态。 |
| `open_archive_file(book_path)` | archive 文件的分块 `BinaryReader` 快照。 |

`BinaryReader` 支持 `chunks(max_bytes=None)`、`read(max_bytes=None)`、`close()` 和上下文管理器。
其 `size`、`sha256`、`book_path`、`resource_id`、`revision` 对应打开流时固定的同一
份临时快照。单块最多 2 MiB；每个 Session 最多同时保留 8 个快照、合计 1 GiB。退出上下文、
显式关闭或 Session 结束时删除快照。

`materialize_temporary()` 必须且只能传 `resource` 或 `book_path` 之一，不接受目标路径；返回
`path`、`book_path`、`resource_id`、`revision`、`size` 和 `sha256`。文本资源来自当前内存，
每个 Session 最多 16 个文件、合计 512 MiB。修改临时文件不会自动写回，必须使用事务写 API。

### 创建事务

`transaction(label="Plugin changes", checkpoint="auto")` 创建 staged `Transaction`；上下文
退出不会自动提交，必须显式调用 `commit()`。

```python
with plugin.book.transaction("Normalize chapters", checkpoint="auto") as tx:
    # context manager does not auto-commit
    current = tx.read_text(resource)
    tx.replace_text(resource, normalize(current["text"]), current["revision"])
    if not tx.validate()["valid"]:
        raise RuntimeError("invalid changes")
    tx.commit()
```

`checkpoint` 为 `none`、`auto` 或 `required`。同一 Session 同时只允许一个事务。

## 6. `Transaction`

| 方法 | 说明 |
| --- | --- |
| `read_text(resource)` | 读 staged 值，否则读 live 值。 |
| `read_text_range(resource, start=0, max_utf16_units=1024 * 1024)` | 读取 live、已修改或新暂存文本的有界范围。 |
| `replace_text(resource, text, expected_revision=None)` | 暂存完整文本；超过 4 MiB 时 SDK 自动分块。 |
| `begin_text_write(resource, size, expected_revision=None)` | 开始最大 64 MiB 的 UTF-8 分块替换。 |
| `begin_text_add(book_path, size, media_type, ...)` | 开始最大 64 MiB 的 UTF-8 分块新增。 |
| `apply_edits(resource, edits, expected_revision=None)` | 暂存不重叠 UTF-16 patches。 |
| `read_binary(resource)` | 读 staged/live 二进制，内联上限 5 MiB。 |
| `write_binary(resource, data, expected_revision=None)` | 暂存最大 256 MiB 的 bytes-like 数据；超过 4 MiB 时 SDK 自动分块。 |
| `begin_binary_write(resource, size, expected_revision=None)` | 开始最大 256 MiB 的分块写。 |
| `write_binary_file(resource, path, expected_revision=None)` | 将本地文件分块写入事务。 |
| `add_resource(book_path, data, media_type, ...)` | 新增 manifested 或 unmanifested 文件。 |
| `remove_resource(resource, expected_revision=None)` | 删除托管资源。 |
| `move_resource(resource, book_path, expected_revision=None)` | 移动并更新引用。 |
| `rename_resource(resource, filename, expected_revision=None)` | 同目录重命名并更新引用。 |
| `replace_package(text, expected_revision)` | 以完整 OPF 作为权威 package 替换。 |
| `update_metadata(items, expected_revision=None)` | 用结构化条目替换 metadata。 |
| `update_spine(items, attributes=None, expected_revision=None)` | 用结构化 itemref 替换 spine。 |
| `replace_archive_file(book_path, data, expected_sha256)` | 替换未托管 archive 文件。 |
| `remove_archive_file(book_path, expected_sha256)` | 删除未托管 archive 文件。 |
| `preview()` | 返回校验结果与摘要，不提交。 |
| `validate()` | 验证 revision、路径、manifest、spine 和结构不变量。 |
| `commit()` | 创建所需 Checkpoint 并统一提交。 |
| `rollback()` | 丢弃全部 staged 状态。 |

`add_resource()` 的 `data` 可为 `str` 或 bytes-like；可选参数为 `manifest_id`、
`properties`、`fallback`、`overlay`、`add_to_spine`、`manifested`。manifested 项必须
提供唯一 `manifest_id`。`mimetype`、`META-INF/container.xml` 和 OPF 不能通过
archive API 修改。

新增文本返回的 `staging_id` 可在同一事务中继续传给 `read_text_range()`、`replace_text()`
或 `apply_edits()`。其 staged revision 从 0 开始，每次完成替换或 patch 后递增，提交前不会
暴露为 live `Resource`。

`replace_package()` 会验证 XML、EPUB 版本、manifest id/href 唯一性、spine idref，
以及 package manifest 是否与同一事务的 add/remove/move 最终状态完全一致。

`read_text_range()` 返回 `text`、`start`、`end`、`total_utf16_units`、`revision`、`staged`
和可空 `next_start`；首段 `start=0` 额外返回整文 `total_utf8_bytes` 与 `sha256`，后续段省略
这两个需要整文编码/哈希的字段。单次最多 1 Mi 个 UTF-16 code units，且不会切开代理项对。

`begin_text_write()` 和 `begin_text_add()` 返回具有 `chunk_size`、`max_size`、`expected_size`、
`received`、`write(text, offset=None)`、`finish()`、`abort()` 的 writer。offset 是严格 UTF-8
字节偏移；宿主限制单块 1 MiB、每个 Session 两个并行上传、单次文本 64 MiB，并在 finish 时
校验长度、UTF-8 和 SHA-256。commit、rollback 或 Session 结束会删除未完成的临时上传。

`begin_binary_write()` 返回具有 `chunk_size`、`write(data)` 和 `finish()` 的 writer；完成时宿主
核对声明长度与 SHA-256 后才暂存数据。`write_binary_file()` 使用同一分块 writer，不会把整个
文件一次性读入 Python 内存。

`update_metadata(items)` 的每项为 `{"name": str, "content": str, "attributes": {str: str}}`，
其中 `attributes` 可省略。该操作替换 `<metadata>` 的全部子元素；带前缀名称所需 namespace
必须已在 package 中声明，结果仍会接受完整 OPF 校验。

`update_spine(items, attributes)` 的每项必须含字符串 `idref`，并可含字符串 `id`、`linear`、
`properties`；`attributes` 是要更新的 spine 字符串属性。该操作替换全部 `<itemref>`，保留
未指定的原 spine 属性，并验证每个 `idref` 都存在于 manifest。

## 7. `EditorApi`

| 方法 | 说明 |
| --- | --- |
| `get_state()` | 当前编辑器完整状态。 |
| `get_selection()` | 当前 `Selection`。 |
| `get_open_tabs()` | 打开的 `Resource` 列表。 |
| `apply_edits(edits, expected_revision=None, resource_id=None, label=...)` | 一个 undo block 应用 1-1000 个 patch。 |
| `replace_selection(text, ...)` | 替换当前选区。 |
| `insert_text(text, ...)` | 在光标插入。 |
| `set_cursor(position, resource_id=None)` | 移动光标。 |
| `set_selection(start, end, resource_id=None)` | 设置选区。 |
| `open_resource(resource, position=None)` | 打开/激活资源。 |
| `reveal_range(resource, start, end)` | 打开并选择范围。 |

写请求必须携带读到的 revision。用户在两次调用之间修改内容时返回
`RevisionConflict`，插件应重新读取、重新计算，而不是盲目重试旧 patch。

## 8. `UiApi`

| 方法 | 说明 |
| --- | --- |
| `show_status(message, duration_ms=5000)` | 状态栏消息。 |
| `show_message(message, title=None, level="info")` | info/warning/error 对话框。 |
| `confirm(message, title=None)` | Yes/No；默认 No。 |
| `progress(label, total=0)` | `Progress` 上下文；0 表示不确定进度。 |
| `choose_open_file(title=None, filter=None)` | 用户选择的输入路径，取消为 `None`。 |
| `choose_save_file(suggested_name=None, title=None, filter=None)` | 用户选择的输出路径。 |

`Progress.update(value, label=None)` 更新进度，`end()` 结束。一个 Session 同时只有一个
progress。文件对话框仅返回用户明确选择的路径；插件进程仍负责实际文件 I/O。

## 9. `EventsApi`

支持事件：

- `editor.activeChanged`
- `editor.selectionChanged`
- `editor.cursorChanged`
- `editor.contentChanged`
- `book.resourceChanged`
- `book.resourceAdded`
- `book.resourceRemoved`

```python
plugin.events.subscribe("book.resourceChanged")
event = plugin.events.next_event()
print(event["name"], event["params"])
plugin.events.unsubscribe("book.resourceChanged")
```

`subscribe(*events)` 订阅事件，`unsubscribe(*events)` 取消订阅。`poll()` 只取已排队事件，
无事件返回 `None`；`next_event()` 阻塞等待。两者默认过滤
本 Session 自己产生的事件，传 `include_self=True` 可接收。每个事件含
`book_revision` 与 `origin_session_id`。光标/选区按 50ms 合并，内容/单资源变更按
100ms 合并；慢客户端填满 4 MiB 待发送队列时会丢弃通知，后续事件以
`dropped_events` 报告。一个连接上不能并发消费事件和发普通 RPC。
编辑器事件携带选区范围但不携带选中文本，避免 notification 被超大选区撑破；需要文本时
再调用 `get_selection()`。

## 10. Validation、input 与 output

### `ValidationApi`

`plugin.validation.publish_results(results)` 原子替换 Validation Results，最多 10,000
项。每项含 `type` (`info`/`warning`/`error`)、`message`、可选 `book_path`、`line`、
`character`；未知位置用 `-1`。`book_path` 必须是规范的 Book 路径。该接口仅供 validation
插件使用。

### `InputApi`

`plugin.input` 仅供 input 插件：

| 方法 | 说明 |
| --- | --- |
| `begin_epub(filename, size=None)` | 创建 `InputWriter`；文件名必须是以 `.epub` 结尾的 basename。 |
| `submit_epub(data, filename="input.epub")` | 从 bytes-like 分块上传。 |
| `submit_epub_file(path)` | 从文件流式上传，不把整个文件读入 Python 内存。 |

`InputWriter.write(data)` 分块发送并累计 SHA-256，`InputWriter.finish()` 要求宿主核验声明
长度、hash、ZIP signature、mimetype、container、规范且不越界的 OPF 路径，以及 OPF 的
package/metadata/manifest/spine 必需结构。每个 Session 同时只能有 1 个 input 上传，最大
2 GiB。只有插件随后成功结束，宿主才询问是否丢弃当前 Book 的未保存修改并加载上传结果；
上传、校验、插件执行或加载任一步失败都保留原 Book。

### `OutputApi`

`plugin.output` 仅供 output 插件：

| 方法 | 说明 |
| --- | --- |
| `save_source()` | 保存回当前源 EPUB；没有已保存的源路径时失败。 |
| `export_epub(path)` | 保存到绝对 `.epub` 路径；当前源路径选择 source 模式，其他路径选择 copy 模式。 |

两者返回 `{"exported": bool, "path": str, "mode": "source"|"copy"}`，并通过 Sigil 原生
保存流程保留字体混淆、清理和 EPUB 校验。copy 模式不更改当前文件名或 modified。output
插件仍可用 `archive_files()`、`read_text()` 与流 API 将内容保存为其他格式或目录。

## 11. 错误与恢复

SDK 将 JSON-RPC 错误映射为 `sigil_live.errors` 异常。协议错误码：

| code | 名称 | 含义 |
| --- | --- | --- |
| `-32002` | BookClosed | 当前 Book 已关闭或替换。 |
| `-32003` | ResourceNotFound | 资源 ID、Book 路径或文本资源不存在。 |
| `-32004` | RevisionConflict | 写入依据的 revision 已过期。 |
| `-32005` | InvalidPatch | 文本范围或 patch 结构无效。 |
| `-32006` | TransactionRequired | 当前 checkpoint 策略不允许该操作。 |
| `-32007` | ValidationFailed | 数据校验或 Checkpoint 失败。 |
| `-32008` | PayloadTooLarge | 请求、响应、上传或快照超过限制。 |
| `-32009` | Busy | writer、事务或流配额正被占用。 |
| `-32010` | UnsupportedOperation | 插件类型、资源或受保护目标不支持该操作。 |
| `-32011` | TransactionNotFound | 事务 ID 不存在、已失效或属于其他 Session。 |
| `-32012` | SessionEnding | Session 正在结束。 |

SDK 为兼容旧宿主仍能解码历史 `PermissionDenied` 异常，但当前宿主与 OpenRPC 不再发出该
错误；API 可用性不由 `<permissions>` 控制。

发生异常时未提交事务由 Session 清理；已提交事务通过 Sigil Checkpoint/undo 恢复。
不要捕获所有异常后仍返回成功，尤其是 input 插件与结构修改插件。

宿主全局只允许一个 live 写事务。每个 Session 限制为每秒 512 请求、8 个二进制读快照（合计 1 GiB）、
2 个二进制写上传、1 个 input 上传、16 个临时物化文件（合计 512 MiB）、单个分块事务
二进制 256 MiB，控制台最多保留 1000 个文本块。

## 12. 完整示例与验证

可安装示例位于 `examples/live_plugins/`。`api-coverage.json` 将 SDK 公共方法映射到
示例或专门测试，`tests/live_plugin_examples_test.py` 校验 XML、Python 语法和覆盖清单。
宿主 dispatcher 与 OpenRPC 方法集合由 `tests/plugin_openrpc_contract_test.py` 锁定；
`tests/live_plugin_docs_test.py` 从 SDK 源码提取公共方法，确保中英文手册同步覆盖。

```sh
cmake -S . -B cmake-build-debug -DBUILD_TESTING=ON
cmake --build cmake-build-debug -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

当前环境完整结果及尚未自动化的 GUI、崩溃恢复和平台矩阵范围见
`LivePythonPluginSecurityAudit.md`，不能用 SDK 方法覆盖替代这些端到端验收。
