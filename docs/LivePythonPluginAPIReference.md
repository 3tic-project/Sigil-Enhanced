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
  <permissions>
    <permission>book.read</permission>
  </permissions>
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

## 2. 权限

| 权限 | 允许的操作 |
| --- | --- |
| `book.read` | Book、资源、archive 和二进制流读取。 |
| `book.write.text` | 创建事务并暂存文本。 |
| `book.write.binary` | 暂存二进制资源替换。 |
| `book.structure` | 新增、删除、移动、重命名资源，替换 OPF 和 archive 文件。 |
| `editor.read` | 当前 Tab、光标、选区和打开 Tab。 |
| `editor.write` | 当前编辑器 patch、插入、选区和光标操作。 |
| `ui.navigate` | 打开资源和定位范围。 |
| `ui.message` | 状态栏、消息框和确认框。 |
| `ui.progress` | Session 控制台进度。 |
| `ui.fileDialog` | 用户授权的打开/保存路径选择。 |
| `validation.publish` | 发布 Validation Results。 |
| `input.submit` | input 插件上传最终 EPUB。 |
| `output.export` | output 插件通过 Sigil 原生 exporter 导出 EPUB。 |
| `events.book` | Book 资源事件。 |
| `events.editor` | 编辑器活动资源事件。 |

声明 `<permissions>` 后只授予列出的权限。未声明时使用按插件类型提供的兼容默认值；
正式插件应始终显式声明最小权限。

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
| `session_info` | 握手结果：Session ID、lifetime、权限、消息上限、Book revision 和 UI 信息。 |
| `book` | `BookApi`。 |
| `editor` | `EditorApi`。 |
| `ui` | `UiApi`。 |
| `events` | `EventsApi`。 |
| `validation` | `ValidationApi`。 |
| `input` | `InputApi`。 |
| `output` | `OutputApi`。 |
| `ping()` | 检查宿主连接。 |

`finish()` 与 `close()` 由 launcher 管理，普通插件不应自行调用。

## 5. `BookApi`

### Book 与 OPF 查询

| 方法 | 返回值 |
| --- | --- |
| `get_info()` | EPUB 版本、modified、当前文件路径、Book revision。 |
| `get_revision()` | 当前 Session 的单调 Book revision。 |
| `get_metadata()` | 有序 metadata、属性、原始 metadata XML 与 package 信息。 |
| `get_manifest()` | 有序 manifest；含 id、href、book path、类型、属性和 resource ID。 |
| `get_spine()` | 有序 spine 项与 spine 标签属性。 |
| `get_guide()` | EPUB 2 guide，含解析后的 book path 和 fragment。 |
| `get_bindings()` | EPUB 3 media-type handler bindings。 |
| `get_selection()` | Book Browser 当前选择的 `Resource` 列表。 |
| `get_compatibility_snapshot()` | v1 adapter 使用的启动状态；原生插件通常不需要。 |

### 资源读取

| 方法 | 返回值 |
| --- | --- |
| `resources(types=None, page_size=200)` | 分页 `Resource` 迭代器。 |
| `text_resources()` | 文本资源迭代器。 |
| `resolve_path(book_path)` | 当前路径对应的 `Resource`。 |
| `get_resource(resource_id)` | 当前资源信息。 |
| `read_text(resource)` | `text` 与 `revision`。 |
| `read_many(resources)` | 最多 100 个文本资源的一批结果。 |
| `read_binary(resource)` | 最多 5 MiB 的 bytes 数据与 revision。 |
| `open_binary(resource)` | 任意大小资源的 `BinaryReader` 快照。 |

### Expanded EPUB/archive

| 方法 | 返回值 |
| --- | --- |
| `archive_files(page_size=200)` | 展开 EPUB 中所有常规文件，包括未托管文件。 |
| `read_archive_file(book_path)` | 最多 5 MiB 的 bytes、SHA-256 与保护状态。 |
| `open_archive_file(book_path)` | 任意大小 archive 文件的 `BinaryReader` 快照。 |

`BinaryReader` 支持 `chunks(max_bytes=None)`、`read()`、`close()` 和上下文管理器。
其 `size`、`sha256`、`book_path`、`resource_id`、`revision` 对应打开流时固定的同一
份临时快照。单块最多 2 MiB；退出上下文或 Session 结束时删除快照。

### 创建事务

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
| `replace_text(resource, text, expected_revision=None)` | 暂存完整文本。 |
| `apply_edits(resource, edits, expected_revision=None)` | 暂存不重叠 UTF-16 patches。 |
| `read_binary(resource)` | 读 staged/live 二进制，内联上限 5 MiB。 |
| `write_binary(resource, data, expected_revision=None)` | 暂存二进制替换，内联上限 5 MiB。 |
| `add_resource(book_path, data, media_type, ...)` | 新增 manifested 或 unmanifested 文件。 |
| `remove_resource(resource, expected_revision=None)` | 删除托管资源。 |
| `move_resource(resource, book_path, expected_revision=None)` | 移动并更新引用。 |
| `rename_resource(resource, filename, expected_revision=None)` | 同目录重命名并更新引用。 |
| `replace_package(text, expected_revision)` | 以完整 OPF 作为权威 package 替换。 |
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

`replace_package()` 会验证 XML、EPUB 版本、manifest id/href 唯一性、spine idref，
以及 package manifest 是否与同一事务的 add/remove/move 最终状态完全一致。

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
- `book.resourceChanged`
- `book.resourceAdded`
- `book.resourceRemoved`

```python
plugin.events.subscribe("book.resourceChanged")
event = plugin.events.next_event()
print(event["name"], event["params"])
plugin.events.unsubscribe("book.resourceChanged")
```

`poll()` 只取已排队事件，无事件返回 `None`；`next_event()` 阻塞等待。每个事件含
`book_revision` 与 `origin_session_id`。一个连接上不能并发消费事件和发普通 RPC。

## 10. Validation 与 input

`plugin.validation.publish_results(results)` 原子替换 Validation Results，最多 10,000
项。每项含 `type` (`info`/`warning`/`error`)、`message`、可选 `book_path`、`line`、
`character`；未知位置用 `-1`。

`plugin.input` 仅供 input 插件：

| 方法 | 说明 |
| --- | --- |
| `begin_epub(filename, size=None)` | 创建 `InputWriter`。 |
| `submit_epub(data, filename="input.epub")` | 从 bytes-like 分块上传。 |
| `submit_epub_file(path)` | 从文件流式上传。 |

`InputWriter.write(data)` 分块发送并累计 SHA-256，`finish()` 要求宿主核验长度、hash、
ZIP signature 和 2 GiB 上限。只有插件随后成功结束，宿主才询问是否丢弃当前 Book
的未保存修改并加载上传结果。

`plugin.output.export_epub(path)` 仅供 output 插件使用。它要求绝对 `.epub` 路径，
拒绝覆盖当前打开文件，通过 Sigil 原生 exporter 写出当前内存 Book，并保留字体混淆、
清理和 EPUB 校验行为；不会更改当前 Book 文件名或 modified 状态。output 插件仍可用
`archive_files()`、`read_text()` 与流 API 实现自定义非 EPUB 格式导出。

## 11. 错误与恢复

SDK 将 JSON-RPC 错误映射为 `sigil_live.errors` 异常。协议错误码：

| code | 名称 |
| --- | --- |
| `-32001` | PermissionDenied |
| `-32002` | BookClosed |
| `-32003` | ResourceNotFound |
| `-32004` | RevisionConflict |
| `-32005` | InvalidPatch |
| `-32006` | TransactionRequired |
| `-32007` | ValidationFailed |
| `-32008` | PayloadTooLarge |
| `-32009` | Busy |
| `-32010` | UnsupportedOperation |
| `-32011` | TransactionNotFound |
| `-32012` | SessionEnding |

发生异常时未提交事务由 Session 清理；已提交事务通过 Sigil Checkpoint/undo 恢复。
不要捕获所有异常后仍返回成功，尤其是 input 插件与结构修改插件。

## 12. 完整示例与验证

可安装示例位于 `examples/live_plugins/`。`api-coverage.json` 将 SDK 公共方法映射到
示例或专门测试，`tests/live_plugin_examples_test.py` 校验 XML、Python 语法和覆盖清单。
宿主 dispatcher 与 OpenRPC 方法集合由 `tests/plugin_openrpc_contract_test.py` 锁定。
