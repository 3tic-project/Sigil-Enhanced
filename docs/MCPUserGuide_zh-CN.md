# Sigil Enhanced MCP 使用指南

## 1. 功能定位

Sigil MCP Server 让支持 Model Context Protocol 的 LLM Host 操作当前已经在
Sigil Enhanced 中打开的 EPUB。它读取的是 Book、Resource 和编辑器内存中的最新内容，
包括尚未保存到磁盘的修改。

首版适合以下任务：

- 读取 EPUB 版本、修改状态、metadata、manifest、spine、guide 和 bindings；
- 分页列出 XHTML、CSS、SVG、XML、OPF、NCX 等资源；
- 读取当前编辑器、选区、光标、打开标签和文本 revision；
- 定位资源和文本范围；
- 对当前标签执行一次可撤销的小范围编辑；
- 批量生成章节、样式表或其他文本资源；
- 在一个 staged transaction 中修改多个文件、metadata、spine 或完整 OPF；
- preview、validate、确认 commit，或完整 rollback。

MCP Server 不包含模型，也不会自行调用任何本地或远程 LLM。

## 2. 当前架构

```text
LLM / MCP Host
  |-- Streamable HTTP + bearer token
  `-- stdio proxy
          |
          v
Sigil MCP Server（Live v2 book-session 插件）
          |
          v
sigil_live -> PluginSession -> 当前 Book / Editor / Transaction
```

MCP 层只负责协议、参数 Schema、工具映射、endpoint 和错误转换。真正的编辑、revision
检查、writer lock、Checkpoint、OPF 校验和回滚全部由现有 Live Plugin API v2 与 C++
宿主执行。

## 3. 安装插件

插件源码位于：

```text
examples/live_plugins/SigilMcpServer/
```

在仓库根目录执行标准打包工具：

```console
python3 examples/live_plugins/package_plugin.py examples/live_plugins/SigilMcpServer
```

工具会生成：

```text
examples/live_plugins/SigilMcpServer.zip
```

插件管理器只接受 ZIP 文件，不直接接受源码目录。ZIP 文件名决定安装目录名，因此该压缩包的
唯一顶层目录必须是同名的 `SigilMcpServer/`，其内部包含：

```text
SigilMcpServer/plugin.xml
SigilMcpServer/plugin.py
SigilMcpServer/README.md
SigilMcpServer/sigil_mcp/
```

不要使用 macOS Finder 的通用“压缩”结果代替标准工具；额外生成的 `__MACOSX/`、
`.DS_Store`，以及源码目录中的 `__pycache__/` 或嵌套 ZIP 都会被安装器拒绝。标准工具会
自动排除这些内容，并以固定顺序生成可复现的压缩包。

在 Sigil 的插件管理器中选择上述 ZIP 安装。`plugin.xml` 已声明：

- `type=edit`
- `api version=2 interface=live`
- `lifetime=book-session`

因此不需要把它切换成 legacy runtime。正式 macOS/Windows 安装包会同时包含官方稳定
MCP Python SDK `1.28.1`；源码构建会从 `requirements-core.txt` 或 `winreqs.txt` 安装。
Linux 使用系统 Python 时，需要确保该解释器可以导入兼容的 `mcp>=1.28.1,<2`。

## 4. 启动和停止

1. 在 Sigil 中打开目标 EPUB。
2. 运行 **Sigil MCP Server** 插件。
3. 保持 Plugin Session Console 处于运行状态。
4. 状态栏会显示 connection metadata 文件路径。
5. 关闭 Book、取消插件 Session 或退出 Sigil 会停止 endpoint。

每个 Book Session 都生成独立文件：

```text
sigil-mcp-<session-id>.json
```

示例结构：

```json
{
  "schema_version": 1,
  "transport": "streamable-http",
  "endpoint": "http://127.0.0.1:43127/mcp",
  "token_type": "Bearer",
  "token": "<random-secret>",
  "session_id": "<book-session-id>",
  "pid": 12345,
  "book": {
    "file_path": "/path/to/book.epub",
    "epub_version": "3.0",
    "revision": 8
  }
}
```

该文件按当前用户权限写入并原子替换。token 不应出现在日志、截图、EPUB、提交或问题报告中。

## 5. Streamable HTTP 连接

支持 HTTP MCP 的 Host 使用 metadata 中的 `endpoint`，并为所有请求发送：

```text
Authorization: Bearer <token>
```

Server 只监听 `127.0.0.1`，使用同一个 `/mcp` endpoint，并校验：

- bearer token；
- `Host` 必须是 loopback；
- 存在 `Origin` 时必须是 localhost/127.0.0.1 loopback origin；
- POST `Content-Type` 必须是 `application/json`；
- MCP protocol/session headers。

端口和 token 每次 Book Session 都会变化。需要稳定 stdio 配置的 Host 应使用下一节代理。

## 6. stdio Host 配置

代理位于：

```text
src/Resource_Files/plugin_launchers/python/sigil_mcp_stdio_proxy.py
```

正式构建也会把它复制到 Sigil 的 Python plugin launcher 目录。代理只使用 Python 标准库，
不包含任何 EPUB 业务逻辑。

通用 MCP Host 配置形态：

```json
{
  "command": "python3",
  "args": [
    "/absolute/path/to/sigil_mcp_stdio_proxy.py"
  ]
}
```

只有一个运行中的 Book endpoint 时，代理会自动发现。也可以固定选择：

```sh
python3 sigil_mcp_stdio_proxy.py --metadata /exact/path/sigil-mcp-session.json
python3 sigil_mcp_stdio_proxy.py --runtime-dir /runtime/path --session-id <id>
```

可用选项：

| 参数 | 含义 |
| --- | --- |
| `--metadata PATH` | 使用精确 metadata 文件，优先级最高。 |
| `--runtime-dir DIR` | 只在指定目录发现 endpoint。 |
| `--session-id ID` | 从候选中选择精确 Book Session。 |
| `--timeout SECONDS` | 单次 HTTP 请求超时，范围 1-300 秒。 |

环境变量 `SIGIL_MCP_RUNTIME_DIR` 可以提供稳定发现目录。Linux 也会检查
`XDG_RUNTIME_DIR/sigil-enhanced/mcp`，macOS 会检查
`~/Library/Application Support/sigil-enhanced/mcp`。代理还会检查当前用户临时 runtime
目录，因此通常不需要手工传入 metadata 路径。

如果同时打开多个启用了 MCP 的 Book，代理会列出候选并退出，不会静默选择任意一个。
插件异常退出遗留但 PID 已失效的 metadata 文件会被自动忽略。

当前代理转发 request、response 和 notification，并维护 HTTP session/protocol headers。
当前 MCP Adapter 不发送 server-initiated request；如果未来加入 sampling、elicitation 或反向
请求，代理需要同步升级。

## 7. 推荐编辑流程

### 7.1 小范围当前标签编辑

适用于选区替换、光标插入或当前文件少量 patch：

1. 调用 `sigil.editor.state`；
2. 使用返回的 `resource_id`、`revision` 和 UTF-16 range；
3. 调用 `sigil.editor.edit`、`sigil.editor.replace_selection` 或
   `sigil.editor.insert_text`；
4. 修改立即进入编辑器，并形成一个 Qt undo step。

不要用 Python 字符串下标代替 UTF-16 offset。包含非 BMP 字符时，两者不同。

### 7.2 生成新章节

1. 调用 `sigil.book.info` 和 `sigil.book.package`；
2. 阅读相邻 XHTML、CSS、语言和 EPUB 版本约定；
3. 调用 `sigil.transaction.begin`；
4. 使用 `sigil.transaction.add_text_resource` 生成 XHTML；
5. 使用 `sigil.transaction.add_binary_resource` 导入不超过 5 MiB 的图片等资源；
6. 必要时使用 `sigil.transaction.update_spine`；
7. 调用 `sigil.transaction.preview`；
8. 调用 `sigil.transaction.validate`；
9. 调用 `sigil.transaction.commit` 并由用户在 Sigil 中确认。

### 7.3 EPUB 排版和 CSS

1. 分页列出 `html` 与 `css` 类型资源；
2. 读取实际 DOM/CSS，不凭空假设 class 或路径；
3. 在一个 transaction 中同时 stage CSS 与相关 XHTML；
4. 保留语义标签、可访问性、相对 URL 和 EPUB 版本兼容性；
5. preview/validate 后再 commit。

### 7.4 可选 EPUB 排版 Skill

仓库提供独立的 `sigil-epub-layout` Codex Skill，包含从参考 EPUB 提炼的通用版式规则、横排
可重排模板、MCP 两阶段事务流程和磁盘验收脚本。它默认禁止隐式触发，只有用户明确写出
`$sigil-epub-layout` 时才会加载。安装、调用和边界说明见
[SigilMcpEpubLayoutSkill_zh-CN.md](SigilMcpEpubLayoutSkill_zh-CN.md)。

### 7.5 批量修改

一个 endpoint 同时只允许一个 transaction。`sigil.transaction.begin` 返回显式
`transaction_id`，后续每次 transaction 工具调用都必须携带它。

stage 期间 live Book 不改变，但 `sigil.transaction.read_text` 可以读取本事务先前写入的内容。
commit 前宿主会重新检查 resource revision 和 package invariants。失败不会自动覆盖用户修改。

## 8. Commit 确认与回滚

`sigil.transaction.commit` 会先取得 preview，并在 Sigil 中显示：

- modified 数量；
- added 数量；
- deleted 数量；
- renamed/moved 数量。

用户选择 No 时：

- 不修改 Book；
- transaction 仍然存在；
- Agent 可以继续 preview、修改或 rollback。

默认 5 分钟无 transaction 活动会自动 rollback。可在启动插件进程前用
`SIGIL_MCP_TRANSACTION_TIMEOUT` 设置 60-1800 秒。Book 关闭、Session 停止和 server shutdown
也会尝试 rollback；宿主 Session 结束是最终清理边界。

## 9. 错误恢复

工具错误内容包含稳定 JSON 对象，FastMCP 可能在文本前增加标准错误前缀：

```json
{
  "code": "RevisionConflict",
  "message": "Resource changed since it was read",
  "retryable": true,
  "recovery": "Read the resource again and rebuild the edit",
  "data": {}
}
```

常见处理：

| 错误 | 处理 |
| --- | --- |
| `ResourceNotFound` | 重新 `sigil.resource.list`，不要复用旧 Book 的 ID。 |
| `RevisionConflict` | 重新读取，重新计算 patch，不能强制覆盖。 |
| `InvalidPatch` | 修正 UTF-16 range、重叠或 surrogate pair 边界。 |
| `ValidationFailed` | 阅读 conflict/detail，修正 staged package。 |
| `PayloadTooLarge` | 使用分页、分批读取或更小上下文。 |
| `Busy` | 完成或回滚当前 transaction。 |
| `TransactionNotFound` | 使用当前 endpoint 新建 transaction。 |
| `BookClosed` / `SessionEnding` | 停止调用并重新发现 Book Session。 |

## 10. 安全边界

MCP 插件是当前账户下的可信本地 Python 代码，不是 OS sandbox。网络 token 只保护 loopback
endpoint，不是插件方法级权限系统。

首版明确不提供：

- `0.0.0.0` 或公网监听；
- 任意系统命令、Python eval 或 QAction 执行；
- 任意本机文件读取/写入；
- Agent 指定输出路径的 EPUB export；
- 直接访问展开 EPUB 目录；
- 绕过 revision/transaction 的磁盘写回。

MCP tool annotations 只是 Host 审批提示。真实约束来自 Live API 的 plugin type、resource、path、
revision、writer lock、transaction 和 package validation。

## 11. 当前限制

当前 MCP surface 已允许用 `sigil.transaction.add_binary_resource` 导入解码后不超过 5 MiB 的
小型二进制资源。以下能力尚未进入公共 MCP 工具：

- 分块 binary 写入和 archive 操作；
- input/output/validation 插件专用 RPC；
- native EPUB-safe source formatter service；
- EPUB structure normalizer service；
- 简繁转换 plan service；
- HarfBuzz 字体子集化 plan service；
- 原生设置页、自动启动、多窗口审计 UI；
- MCP sampling、elicitation、tasks 和 server-initiated request；
- Windows/Linux 的完整人工 Host 兼容矩阵。

通用文本工具已经可以让模型生成/修改 XHTML、CSS、XML、metadata 与 spine，但原生算法应在
后续通过结构化 Host Service 复用现有 C++ 核心，不能在 MCP Python 层复制实现。

## 12. 测试

```sh
cmake --build cmake-build-debug -j8
ctest --test-dir cmake-build-debug --output-on-failure
```

自动测试覆盖：

- MCP SDK 依赖完整打包；
- deterministic tools/resources/prompts；
- 官方 MCP client lifecycle；
- 结构化 tool result 和 tool error；
- bearer、Host、Origin、loopback URL；
- transaction 状态、确认、idle 和 shutdown rollback；
- atomic owner-only metadata；
- stdio subprocess 实际转发；
- 多 Book 拒绝静默选择；
- 文档/API 清单一致性。
