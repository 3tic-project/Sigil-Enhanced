# Native Agent

Sigil-Enhanced 内置的 **Native Agent** 是当前打开书籍的 EPUB 助手。它理解这本书的结构、资源、样式、字体、Spine、目录和元数据，可以用自然语言查询、规划，并在你批准后做可撤销的修改。

它不是编程 Agent，也不会操作整台电脑。它不调用 MCP。需要 typed 工具表达不了的批量逻辑时，可以用 `python.run`：把一段 Python **snippet** 交给 Live Python v2，绑定 `plugin`（`plugin.book` / `plugin.editor`）在当前内存中的 Book 上执行。这不是一份插件包，也不是旧插件那种 ZIP 快照。

## 打开方式

在主窗口 **查看** 菜单中打开 **Agent** 停靠栏（与 Book Browser、Preview 同类）。停靠栏包含：

- 模式：**Ask** / **Plan** / **Edit** / **Auto**
- 当前模型（只读，来自偏好设置）
- 提供商状态：配置是否完整、安全的 endpoint 主机，以及最近一次真实请求结果
- 当前书籍状态：EPUB 文件名、元数据标题、资源数、Saved / Unsaved、书籍会话短 ID 和 Agent revision
- 上下文芯片：选区、当前文件、Book Browser 选中文件、全书（互斥，范围会显示在状态行）
- 输入框：**Enter 发送**，Shift+Enter 换行
- **Stop**（会立刻中止正在等待的 HTTP，不只在收到首包之后）、**New Session**
- **Export**：导出当前对话（Markdown）或完整调试日志（JSON，已脱敏）
- 事件卡片：每一轮独立的用户 / 折叠 Thinking / 回答；工具卡片会更新运行状态；批准带影响说明；预览按变更类型列出暂存内容；提交和回滚显示独立结果；错误

Thinking（模型的 `reasoning_content`）不是给用户看的最终答案；答案只在 Answer 卡片里。新的一轮不会覆盖上一轮的回答。

## 当前书籍与上下文范围

当前书籍状态直接读取这个 MainWindow 的内存 Book，不扫描磁盘副本。正文编辑使 Book
变脏、保存清除 modified 状态、资源增删移动、另存为、切换标签或 Agent commit 后都会
刷新。文件名与 `dc:title` 同时显示，便于在多窗口里确认目标；资源数来自当前 Book。
`Book session` 是每次把工作区绑定到一本书时生成的不透明 ID；状态行显示前 8 位，完整值
保存在上下文事件和 `book.summary` 中。`Agent rev` 是该书籍会话内的协议修订号，不等同于
Saved / Unsaved，三者分开显示。

上下文芯片是互斥任务范围，含义如下：

- **Selection**：编辑器存在非空选区时自动启用，显示 `start–end`。发送时用
  `resource_id:start-end` 记录 UTF-16 code-unit 范围，并从当前内存资源读取该范围的
  精确源码；Ruby 和内部标签不会退化成纯文本。单次自动附加最多 4096 code units，
  超出时明确标为截断，模型必须再用 `resource.read_fragment` 读取剩余范围。
- **File**：附加当前资源的有界开头片段。
- **Selected files**：读取 Book Browser 当前选中的一个或多个资源，保持浏览器顺序并
  去重。自动上下文最多附加 60 份资源片段；更多资源不会静默吞掉，而会报告省略数量，
  模型可按需用 `resource.read_fragment` 继续读取。
- **Whole book**：附加全书资源表和最多两个 Spine 正文开头样本；这是显式的全书范围，
  不再与当前文件默认叠加。

有非空编辑器选区时默认采用 Selection，否则默认 Current file；两者均不可用时才依次
采用 Selected files 或 Whole book。用户手动选择的有效范围会保持，不因光标或 Book
Browser 选择刷新而跳走；若该范围消失才按上述规则回退。File、Selected files 和
Selection 均不会暗中附加全书资源表或 Spine 样本。书籍的最小身份摘要始终保留，用来
确认工具目标。每个窗口有自己的 AgentDock、Book 和 workspace；范围不会跨窗口读取。

### 运行与书籍会话绑定

发送时，Runner 会冻结当时的完整 `book_session_id`。每次模型响应后、每个工具开始前和
结束后都会复核；若工作区已绑定到另一本书，旧响应以 **Book target changed** 失败，
不会把旧计划或工具调用重定向到新书。换书的正常 UI 路径还会主动以 `book_changed`
原因取消请求，并清除旧书的未提交暂存事务。

运行期间 **New Session** 和模式选择会锁定。即使重入代码直接请求 New Session，也只会
先取消，等旧 Runner 完全返回后再清空会话和重建工具；不会在旧调用中途重置取消标记。
偏好设置中的 Provider / 模型变更同样延后到本轮退栈后应用，避免替换仍在执行的对象。
Controller 也会拒绝运行中的 workspace 替换和递归发送，避免非 UI 调用绕过控件锁定。

若在请求或审批期间关闭窗口，第一次关闭只发出 `window_closing` 取消并保持窗口对象存活；
Runner 返回后才重新关闭窗口。状态卡会区分用户 Stop、换书、关闭窗口和防御性目标不匹配。

## 提供商配置与最近请求状态

提供商状态行只显示提供商、模型和 endpoint 的主机名（自定义端口会保留）。URL 中的
用户名、密码、路径和查询参数不会进入状态行，API Key 也不会显示。状态语义如下：

- **Setup required**：缺少或无法识别 endpoint、API Key 或模型。此时没有尝试网络连接。
- **Configured · not tested**：必需设置已填写，**不表示服务器可连接或凭据有效**。
- **Contacting provider…**：已经发出一轮真实模型请求。
- **Last request succeeded / failed / cancelled**：只由该轮请求的完成、失败或取消事件
  更新；工具调用型响应也会产生明确的模型请求完成事件。

失败状态把 401、403、404、408、429、5xx 和常见网络错误整理为简短说明，完整的
提供商错误仍显示在 Error 卡片。若服务端在错误正文或 HTTP trace 中回显当前 API Key，
提供商边界会先替换成 `[redacted]`，导出时还会再次脱敏。当前没有独立的“测试连接”
按钮；**Refresh models** 的成功只证明模型列表请求成功，Chat Completions 的状态仍以
实际对话请求为准。

## 模式

| 模式 | 能做什么 |
|---|---|
| **Ask** | 只读。可摘要、搜索、读片段、列字体、校验。不能改书。 |
| **Plan** | 可以 `transaction.begin`、暂存 create/copy/rename/patch/CSS/metadata，并 `preview`。不能 `commit` / `restore` / `python.run`。活书保持不变。 |
| **Edit** | 可通过工具改书。可逆编辑默认要你点 **Approve**。提交结果会说明保存、校验和可用的恢复边界。 |
| **Auto** | 与 Edit 相同的写入工具，但默认全部允许，不再弹出 Approve。 |

**Stop** 会取消当前轮次，并回滚尚未提交的暂存事务。已经 commit 的步骤不会被
`transaction.rollback` 撤销，结果仍标为 Applied；只能使用提交卡所列的宿主恢复方式。

## 预览、提交与恢复状态

- **Preview** 首行明确当前 Book 未改变，并分别列出文本、新增、重命名、删除、元数据、
  阅读顺序和 TOC 层级变更。预览事件同时记录 `applied_to_book=false`、
  `save_status=not_applied` 和完整 EPUBCheck 未运行。
- **Applied** 表示事务已写入当前内存 Book，不表示 EPUB 文件已保存。卡片和会话事件会
  显示应用项数、Book revision、`save_status=not_saved`，并明确完整 EPUBCheck 未运行。
- **Staged changes discarded** 只表示提交前暂存事务已丢弃，活书没有被该事务修改。
  提交后恢复只能在可用处使用 Sigil Undo。`transaction.commit` 自身不会创建整任务
  恢复点；当前也没有“撤销本次排版”按钮或能避让后续人工编辑的任务级恢复流程。

因此，Agent 回答“完成”不能替代用户保存 EPUB，也不能替代完整 EPUBCheck。会话的
Conversation Markdown 导出保留同样的 Preview / Applied / Rollback 状态边界。

## 工具（模型不能直接写 EPUB ZIP）

只读：`book.summary`、`book.resources`、`book.spine`、`book.toc`、`book.metadata`、`book.search`、`book.search_regex`、`book.check`、`resource.read_fragment`、`style.stylesheets`、`font.inventory`、`book.validate`、`manuscript.parse`、`paragraphs.analyze` / `paragraphs.plan`、`toc.inspect_hierarchy` / `toc.plan_transform`、`session.recall` / `session.tasks`。

写入（先暂存，再预览，再提交）：`transaction.begin` / `preview` / `commit` / `rollback`、`resource.create` / `copy` / `delete` / `rename` / `replace_text` / `patch_fragment`、`content.replace_body` / `insert` / `wrap` / `replace_regex` / `wrap_plain` / `split` / `merge`、`image.insert`、`spine.set` / `spine.sort`、`style.link`、`toc.generate` / `toc.apply_transform`、`css.update_rules`、`metadata.update`、`content.fill_section`、`content.typeset_from_manuscript`、`paragraphs.apply`、`checkpoint.create` / `list` / `restore`。

立即作用于活书（不走 Agent 暂存事务；Plan 模式禁用）：`python.run`。

会话级（不改书，New Session 会清空）：`session.remember`、`session.task_add`、`session.task_update`。

长文本必须已经在书里（拖进 Book Browser 的 TXT/HTML）。`content.wrap_plain` 用你提供的 heading/illustration **正则**套标签；`content.split` 按标题拆章；`content.replace_body` 用 `source_resource_id` 搬运整段 body。不要把小说正文贴进 `patch_fragment` / `replace_text`（后两者对模型有大小上限）。

批量套标签用 `content.wrap`，全书查找替换用 `content.replace_regex`（`$1` 捕获组）。图片必须先拖进 Images，再用 `image.insert` 按锚点插入 `<img>`。`book.check` 会报损坏的图片链接和未引用图片。

`spine.set` 重排阅读顺序；`spine.sort` 按路径字母数字排序；`resource.rename` 可改文件名或换目录（提交时改 href）；`style.link` 重写 XHTML 的 stylesheet `<link>`；`resource.delete` 不能删 OPF/NCX/Nav 或最后一份 XHTML；`toc.generate` 按标题正则生成 TOC；`metadata.update` 支持任意 DC 字段，`_remove` 删除字段。

整理伪段落时使用 `paragraphs.analyze` → `paragraphs.plan` → `paragraphs.apply` →
`transaction.preview` → `transaction.commit`。这条流程复用菜单功能的保守 DIV/CSS
引擎；不要用正则逐段改写，也不要在 `paragraphs.apply` 前调用
`transaction.begin`。`apply` 会核对计划 ID、摘要、书籍/XHTML/CSS 修订并自行创建
独占暂存事务，成功也不代表已写入活书。完整协议、错误和证据见
[Native Agent 原生段落计划工具](AgentNativeParagraphTools.md)。

调整既有目录层级时使用 `toc.inspect_hierarchy` → `toc.plan_transform` →
`toc.apply_transform` → `transaction.preview` → `transaction.commit`。不要预先调用
`transaction.begin`，也不要为目录升降级改写 XHTML 标题。计划绑定当前会话、书籍
修订、完整目录树和 Nav/NCX 精确源码；apply 只创建暂存事务。纯层级写回保留原节点
属性、内联标记、标题、目标、先序及非 TOC 区域。详见
[Native Agent 原生目录层级工具](AgentNativeTocTools.md)。

`python.run` 只落一个临时 `.py` snippet（和其它 harness 的 `run_code` 一样），用 `live_launcher --snippet` 连上现有 `PluginSession` socket，把 `plugin` 绑进这段代码。不要写 `plugin.xml`。Snippet 顶层就能用 `plugin.book`；也可以定义 `def run(plugin)` 或设 `result`。若 Agent 还有未提交事务，会先要求 `commit` / `rollback`。Memory 测试工作区返回 `LIVE_PYTHON_UNAVAILABLE`。脚本上限 64KiB，输出截到 8KiB。优先用 typed 工具；Python 只补工具盖不到的逻辑。

格式不是固定的：标题/插图标记都通过工具参数里的 regex 传入。`ln-template-typeset` 只是一套可选默认启发式；通用流程见 skill `book-structure`。目前仍不能把字体/图片二进制塞进模型，也不能改 EPUB3 landmarks/page-list。

## 按模板灌文稿（可选捷径）

若当前书碰巧是「封面/彩页/Section」那类轻小说模板，仍可用 `manuscript.parse` + `content.typeset_from_manuscript`。更稳妥的通用做法是：`content.wrap_plain`（自带 heading_pattern）→ `content.split` → `image.insert` / `spine.set` / `toc.generate`。

模型**禁止**让你去书籍视图里全选粘贴。

`resource.patch_fragment` 用 **当前原文子串** 定位，不要让模型数字符偏移。必填 `expected_text`（从 `read_fragment` 的 `text` 原样复制，可以是一整行）。子串出现多次时用 `start_line`（来自 `read_fragment.lines` 的 1-based 行号）消歧。区间不能切到半个标签（例如把 `</title>` 切成 `</titl`）。预览会带 staged excerpt。

发给模型的 function 名会把点换成下划线（`book.summary` → `book_summary`），因为 DeepSeek/OpenAI 只接受 `^[a-zA-Z0-9_-]+$`。内部仍用带点的名字。

没有 shell，没有技能脚本，不会把字体二进制或整本书 XHTML 送给模型。若提交时的
`expected_revision` 与当前书籍 revision 不一致，会返回 `BOOK_REVISION_CONFLICT`，不会改书。
提交前还会比较事务开始时的精确 OPF 源码、每份已暂存正文的原文、重命名/删除资源基线，
以及需要改写的 Nav/NCX 原文；因此用户在 preview 后从 GUI 修改目标内容，即使内部书籍计数尚未
更新，也会拒绝旧计划并保留用户改动。`metadata.update`、`spine.set/sort` 和资源结构操作最终
走 OPF 的局部源码补丁，保留未涉及的注释、前缀、属性引号、换行和私有扩展。

HTTP 出错时 Error 卡会带上状态码和服务器返回的 `error.message`；配置的 API Key 若被
服务端回显，会在写入 transcript 和 HTTP trace 前替换为 `[redacted]`。

## 模型与 Thinking

在 **偏好设置 → Native Agent** 中选择提供商并配置密钥。模型在这里选定，Agent 停靠栏不再填写模型名。

| 提供商 | Chat Completions | 模型列表 |
|---|---|---|
| **DeepSeek** | `https://api.deepseek.com/chat/completions` | `GET https://api.deepseek.com/models` |
| **OpenCode Go** | `https://opencode.ai/zen/go/v1/chat/completions` | `GET https://opencode.ai/zen/go/v1/models` |
| **OpenRouter** | `https://openrouter.ai/api/v1/chat/completions` | `GET https://openrouter.ai/api/v1/models?supported_parameters=tools` |
| **Custom** | 你填写的 OpenAI 兼容 URL | 从同一 origin 推导 `/models` |

点 **Refresh models** 会向服务器拉取模型 id 以及它公布的参数（context length、`supported_parameters` 里的 tools / reasoning）。不要手抄模型名；列表来自服务器。密钥只存在本机 Sigil 设置里，不会进入 transcript、崩溃日志、导出文件或 EPUB。

请求体按提供商区分：

- DeepSeek：`thinking: {type: enabled|disabled}` 和 `reasoning_effort`
- OpenRouter：`reasoning: {effort, exclude: false}`（且只在该模型宣称支持 reasoning 时发送）；并带 `HTTP-Referer` / `X-Title`
- OpenCode Go / 其他：不发送 DeepSeek 的 `thinking` 字段，避免 400

协议仍是 OpenAI 兼容 Chat Completions。流式响应里 `reasoning_content`（以及 OpenRouter 的 `reasoning`）、`content`、`tool_calls` 分开解析。当某次请求带了 `tools` 时，同一会话后续请求必须回放助手的 `reasoning_content`（否则部分推理模型会返回 400）；不带 `tools` 的请求可以省略先前的思维链。

Native Agent 不调用 MCP，也不把 MCP 当作内部 RPC。`paragraphs.*` 与原生
`toc.inspect_hierarchy` / `toc.plan_transform` / `toc.apply_transform` 当前是 Native
Agent 专用工具，不在公共 `sigil.*` MCP catalog 中。

## 导出

停靠栏 **Export** 菜单：

- **Conversation**：当前会话的 Markdown（用户 / Thinking / Answer / 工具），供阅读或贴给别人。
- **Debug log**：JSON，含会话事件（不含逐 token 的 `assistant_delta`，只保留完整 assistant/tool 事件）、提供商（不含密钥）、以及跨轮保留的 HTTP 追踪（请求体最多 64KB，响应保留头尾）。API Key 和 `sk-…` 会被替换成 `[redacted]`。

## Harness

Agent 循环在 `AgentRunner` 里，不在 UI 类中：

1. 组装上下文与历史
2. 流式请求模型
3. 按权限 allow / ask / deny 处理工具
4. ask 时先发批准卡片，批准后才执行
5. deny / 用户拒绝 / 取消时仍写入一条 tool-role 结果（`PERMISSION_DENIED` 或 `CANCELLED`），避免下一次请求因缺 tool 消息而 400
6. 在模型与工具边界复核运行开始时冻结的 `book_session_id`
7. 把事件追加到会话日志（transcript 的唯一来源）

书籍写入走现有 Book / 事务 / 撤销机制。
