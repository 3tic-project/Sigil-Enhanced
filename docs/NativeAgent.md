# Native Agent

Sigil-Enhanced 内置的 **Native Agent** 是当前打开书籍的 EPUB 助手。它理解这本书的结构、资源、样式、字体、Spine、目录和元数据，可以用自然语言查询、规划，并在你批准后做可撤销的修改。

它不是编程 Agent，也不会操作整台电脑。它不调用 MCP。需要 typed 工具表达不了的批量逻辑时，可以用 `python.run`：把一段 Python **snippet** 交给 Live Python v2，绑定 `plugin`（`plugin.book` / `plugin.editor`）在当前内存中的 Book 上执行。这不是一份插件包，也不是旧插件那种 ZIP 快照。

## 打开方式

在主窗口 **查看** 菜单中打开 **Agent** 停靠栏（与 Book Browser、Preview 同类）。停靠栏包含：

- 模式：**Ask** / **Plan** / **Edit** / **Auto**
- 当前模型（只读，来自偏好设置）
- 提供商状态：配置是否完整、安全的 endpoint 主机，以及最近一次真实请求结果、耗时和时间
- 当前书籍状态：EPUB 文件名、元数据标题、资源数、Saved / Unsaved、书籍会话短 ID 和 Agent revision
- 上下文芯片：选区、当前文件、Book Browser 选中文件、全书（互斥，范围会显示在状态行）
- 输入框：**Enter 发送**，Shift+Enter 换行
- **Stop**（会立刻中止正在等待的 HTTP，不只在收到首包之后）、**New Session**
- **Retry**：仅在可以安全重跑的首个 Provider 请求失败后启用
- 纯文本提交卡上的 **Restore this task**：在后续目标内容未改变时恢复该次提交
- 默认折叠的 **Technical details**：完整会话/整轮运行/请求/书籍 ID、两级耗时、token
  用量、模型步骤与工具调用上限、请求历史与工具目录预算、revision、模式与冻结范围
- **Export**：导出当前对话（Markdown）或完整调试日志（JSON，已脱敏）
- 事件卡片：每一轮独立的用户 / 折叠 Thinking / 回答；工具卡片会更新运行状态；原生计划按资源展示可检查差异；批准带影响与计划绑定；预览按变更类型列出暂存内容；提交和回滚显示独立结果；错误

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
- **Chat tested successfully · 时间**：这组已保存的 provider、endpoint、API Key 和模型曾经
  通过独立 Chat Completions 测试；这是历史结果，不是实时在线指示。
- **Contacting provider…**：已经发出一轮真实模型请求。
- **Last request succeeded / failed / cancelled**：只由该轮请求的完成、失败或取消事件
  更新；工具调用型响应也会产生明确的模型请求完成事件。终态同时显示该次
  `provider.stream()` 的耗时和完成时间，不把整个多步骤 Agent 任务时长冒充网络耗时。

每次正式流式请求还在本地单调时钟上记录两段响应延迟：**first byte** 是收到首个非空
响应正文时的时间，**first model event** 是解码出首个有意义的 reasoning、content、
tool call 或 finish reason 时的时间。SSE 注释/心跳可以让 first byte 更早，但不会被当作
模型事件；因此后者是“首个可用模型事件”，并不承诺等同于首个文本 token。两者都从实际
`provider.stream()` 发请求前起算，不是整轮 Agent 时长，也没有推断 DNS/TLS 等子阶段。
超时、取消或错误发生前没有观测到对应边界时，界面明确显示 **Not observed**。

偏好设置中的 **Request token usage when supported** 默认开启。正式流式请求会加入
`stream_options.include_usage=true`；若兼容端点拒绝这个可选字段，可关闭该项。服务端
返回 usage 时，最近请求的 Technical details 显示输入、输出、总计以及可用的缓存输入/
推理 token，并把同一组整数写入 `model_request_completed` 调试事件。这里绝不按字符数
估算 token；服务端未返回时明确显示 **not reported by provider**，关闭开关时显示
**not requested**，两者都不会显示成 0。

整轮结束时还会汇总该轮所有模型请求。全部请求都报告 usage 时显示完整合计；只有部分
请求报告时明确写出 **N of M requests reported**，显示的只是这 N 次请求的精确和，不冒充
完整总量。缓存输入和推理明细只有在所有已报告请求都提供对应字段时才汇总；缺字段不会
按 0 补齐。失败或取消中的未完成请求计入请求数，但保持未报告。

失败状态把 401、403、404、408、429、5xx 和常见网络错误整理为简短说明，完整的
提供商错误仍显示在 Error 卡片。若服务端在错误正文或 HTTP trace 中回显当前 API Key，
提供商边界会先替换成 `[redacted]`，导出时还会再次脱敏。**Refresh models** 的成功只
证明模型列表请求成功，不证明 Chat Completions 可用。模型刷新也在线程池中执行，最多等待
30 秒；刷新与连接测试互斥，运行期间连接字段被冻结，关闭设置页会取消请求。目录接口的
错误正文若回显当前 API Key，同样会先脱敏再显示。

偏好设置提供独立的 **Test Chat Completions**。它先在本地校验 URL、API Key 和模型，
再向当前表单中的 endpoint 发送一条流式小请求：不含书籍内容、不含 tools、关闭 thinking，
不发送可选的 usage 请求，并把输出限制为最多 8 token、等待限制为 15 秒。提供商仍可能
按这条请求计费。设置页会
显示冻结的安全主机名、模型、HTTP 结果和耗时；401 等服务端错误继续执行密钥脱敏，超时
明确显示失败，不会把中止误报成成功。修改 URL、API Key 或模型后，旧结果立即标为
“当前设置未测试”。测试运行期间不会提前保存表单；关闭偏好设置时，成功结果才与当前
配置一起保存。网络探测在线程池中执行，设置窗口和主事件循环不会被嵌套网络循环占用；
关闭设置页会通过线程安全标记取消仍在等待的探测，完成回调不会访问已销毁控件。

连接测试不进入书籍 Agent 会话，也不伪造 Dock 的最近请求状态。Dock 的
**Last request succeeded / failed / cancelled** 仍只代表一次真实的书籍 Agent 请求；
尚无真实书籍请求时，Dock 可以显示同一组已保存配置上次通过连接测试的时间。证明只保存
成功时间与配置的 SHA-256 指纹，不复制 API Key；provider、规范化后的 Chat URL、API Key
或模型任一不匹配都会自动失效。重新测试失败同样会清除待保存的成功证明。

**Technical details** 默认折叠，展开后显示完整 Agent session ID、当前 book session ID、
最近一次 run ID 和 request ID。**Whole run** 从进入上下文准备开始，覆盖本轮所有模型请求、
审批等待、工具调用和失败/取消时的暂存回滚；终态显示总耗时、模型请求数、工具调用数和
完成时间。独立的 **Duration** 仍只表示最近一次 `provider.stream()`，不会冒充整轮时间。
**Response latency** 独立显示最近一次请求的 first byte 与 first model event，未观测字段
不会用 0 补齐。详情还显示请求绑定的 book session/revision、步骤、模式、模型、终态、
发送时冻结的 scope handles、当前模型步骤/工具调用用量与上限、最近请求 token 用量和带覆盖率
的整轮 token 汇总。这里只有安全的 endpoint 主机，不显示 API Key、URL 路径、查询参数或
响应正文。

### 单次运行模型步骤上限

在 **偏好设置 → Native Agent** 中，**Maximum model steps per run** 限制一轮任务最多发送
多少次正式模型请求，默认 **24**，可设范围为 **1–64**。一次模型步骤就是一次 Provider
请求；模型调用工具后，为读取工具结果而继续发送的请求也计入。限制不会截断正在进行的
HTTP 响应或工具调用，而是在准备发送下一次模型请求前停止本轮，因此设为 N 时最多实际
发送 N 次请求。

达到上限会以 `MAX_MODEL_STEPS_EXCEEDED` 失败，并自动回滚当时仍开放的暂存事务，避免一个
持续索取工具的模型无界运行。此前已经 commit、标为 **Applied** 的变更不会被这个回滚悄悄
撤销，仍须按 Applied 卡片所列的恢复边界处理。这与 **Stop** 的用户主动取消原因不同，但
两者都只回滚尚未提交的暂存工作。

每个运行状态调试事件都带本轮 `max_model_steps`，终态还带实际 `model_steps`；错误事件同时
记录两者。展开 **Technical details**，运行中可看到本轮上限，结束后可看到“已用/上限”，
无需等到触发错误才确认当前安全预算。

### 单次运行工具调用上限

**Maximum tool calls per run** 限制一轮中模型提出并获准进入执行阶段的累计工具调用数，
默认 **128**，可设范围为 **1–512**。它与模型步骤上限独立：一次模型响应可以同时提出多个
工具调用，而模型读取这些结果的下一次请求又会消耗一个模型步骤。每次请求的系统提示会告知
模型剩余额度，避免较低的自定义上限成为隐藏约束。

Runner 在执行每个模型返回的工具批次之前检查整批大小。若“此前已用 + 本批申请”会超限，
则本批 **一个也不执行**，并以 `MAX_TOOL_CALLS_EXCEEDED` 结束；被拒绝的 assistant tool-call
批次也不会写进后续请求历史，从而不留下缺少对应 tool result 的协议消息。此前已开放的暂存
事务会回滚，已经 Applied 的提交仍不会自动撤销。

`model_request_started` 记录该请求开始时的已用、剩余和上限；超限错误另记录本批申请数，
运行终态保留实际获准的工具调用数。**Technical details** 在运行中显示上限，结束后显示
实际用量/上限。这里限制的是工具调用数量，不限制单个工具结果大小或一个工具内部处理的
资源数。

### 请求历史预算

在 **偏好设置 → Native Agent** 中，**Previous-turn history budget** 控制每次正式模型请求
最多回放多少先前对话，默认 **32 KiB**，范围为 1–512 KiB；设为 **Unlimited**（数值 0）
可恢复完整回放。这个预算只作用于先前已经完成的用户轮次，不包括系统提示、当前书籍/选区
上下文，也不限制正在运行的当前轮。当前轮即使本身超过预算也会完整发送，使该轮内已有的
assistant tool call 与对应 tool result 不会被截断。

裁剪按完整用户轮次选择连续的最近后缀：若再加入一个较旧轮次就超限，则从该轮及更旧轮次
全部省略，不从轮次中间切开消息。预算以最终 OpenAI 消息对象的紧凑 JSON UTF-8 字节数计算，
不是模型 token 上限，也不会删除会话日志、Conversation 导出或本地任务记忆。

每个 `model_request_started` 调试事件都包含 `history_context`，记录预算、总计/已发送/已省略
轮次和消息数、先前轮次总字节/已发送字节及当前轮字节。展开 **Technical details** 可直接看到
最近请求的已发送轮次、遗漏轮次、预算使用量与始终保留的当前轮大小；多步骤工具任务会随
最新请求更新当前轮统计。

**Retry** 不是底层 HTTP 自动重试。它会作为新一轮，重新发送用户上次实际提交的文本和
同一组 scope handles，并重新组装当前内存内容。仅当首个模型请求失败、当前仍是同一
book session 且没有活动运行时启用；换书或 New Session 后失效。第二步及更晚的请求失败
意味着本轮可能已经执行过工具，因此 Retry 明确禁用，防止重复提交已有操作。

## 模式

| 模式 | 能做什么 |
|---|---|
| **Ask** | 只读。可摘要、搜索、读片段、列字体、校验。不能改书。 |
| **Plan** | 可以 `transaction.begin`、暂存 create/copy/rename/patch/CSS/metadata，并 `preview`。不能 `commit` / `restore` / `python.run`。活书保持不变。 |
| **Edit** | 可通过工具改书。可逆编辑默认要你点 **Approve**。提交结果会说明保存、校验和可用的恢复边界。 |
| **Auto** | 与 Edit 相同的写入工具，但默认全部允许，不再弹出 Approve。 |

每次模型请求只发送当前模式的权限策略允许调用的工具 schema。Ask 隐藏所有会改 Book 的工具；
Plan 保留只读、transaction begin/preview/rollback 和可暂存预览的工具，但不发送 commit、
checkpoint create/restore、`python.run` 及其他不能预览的写工具；Edit 与 Auto 继续发送完整目录。
过滤直接调用 Runner 随后用于执行工具的同一个 `PermissionPolicy`，避免请求目录和实际权限
各写一套规则。

这是减少请求体和无效工具选择的优化，不是唯一安全边界。模型若仍返回未宣告、拼错或当前
模式禁止的工具，Runner 会照常按注册表解析并在执行前拒绝，不能靠伪造 tool call 绕过模式。
每个 `model_request_started.tool_context` 会记录完整/暴露/隐藏工具数、隐藏名称以及过滤前后
schema 字节；**Technical details** 显示数量和大小，Debug JSON 保留精确字段。schema 大小是
紧凑 JSON 数组大小，不是 token 估算，也不包括消息正文和 HTTP 包装。

**Stop** 会取消当前轮次，并回滚尚未提交的暂存事务。已经 commit 的步骤不会被
`transaction.rollback` 撤销，结果仍标为 Applied；纯文本提交可使用该 Applied 卡上的
任务恢复按钮，其他提交只能使用卡片所列的宿主恢复方式。

## 原生计划审阅

`paragraphs.plan` 或 `toc.plan_transform` 成功后，Runner 会在普通工具结果之外发布独立的
`plan_created`，并在对话中展开一张 **Review paragraph plan** 或 **Review TOC plan**：

- 段落卡按文件列出可处理/跳过/失败数、转换数、受保护项、CSS/OPF/资源范围，以及工具
  生成的受限 before/after 源码片段。片段强制按纯文本显示，`<div>` / `<p>` 不会被界面
  当作 HTML 渲染。
- TOC 卡列出受影响与重新归属的节点、label/target、父节点和深度前后值，并明确先序是否
  保留、是否修改 XHTML 标题层级；工具截断变化列表时卡片也会明确提示。
- 卡片明确说明活书未改变、局部校验结果和完整 EPUBCheck 是否运行。每个计划最多为前 8
  个唯一目标提供 **Open…** 按钮；它打开当前 Book 中的对应资源，现有 Preview 随编辑标签
  更新。按钮绑定生成计划时的完整 book session，换书立即禁用，MainWindow 跳转前还会
  再核对一次；TOC fragment 会去除，URL 编码路径会按 EPUB 路径规则解码。
- 段落资源旁的 **Compare…** 会把计划携带的 before/after 源码片段放进只读双栏；TOC 的
  **Compare hierarchy…** 则分栏显示每项变换前后的 parent/depth。两栏使用固定宽度字体、
  不自动折行并同步滚动。对话框继承计划 ID、digest、revision 与 book session，换书或重置
  transcript 时立即关闭；片段本身有工具侧上限，界面另有每栏 8 KiB 防御上限并明确提示省略。
- Edit 模式中，`paragraphs.apply` / `toc.apply_transform` 的批准卡会把参数中的 plan ID、
  digest、expected revision 与本会话已展示的计划及当前 book session 逐项匹配。任一项不符
  时 **Approve** 禁用，**Deny** 保持可用；工具层仍会再次执行原有的计划重验。普通
  `transaction.commit` 批准不受这条专用门影响。Auto 模式没有人工批准，但工具层绑定校验
  仍然有效。
- 匹配的段落 apply 批准卡会按 XHTML 显示独立操作组，默认全选，也可 **Select all**、
  **Clear** 或逐项取消；至少保留一项才能批准。批准只把勾选资源作为受限参数覆盖传给
  `paragraphs.apply`，plan ID/digest/revision 不允许由界面覆盖，未勾选 XHTML 不进入该次
  暂存事务。TOC 的父子关系变化可能互相依赖，因此仍明确显示为一个不可拆分的操作组。

这推进了宿主计划审阅，但还不是通用可编辑计划表：只有原生段落计划按 XHTML 声明为独立
的组可在批准时选择；TOC 和其他事务不会被界面武断拆分。双栏只比较计划自带的受限片段/
层级清单，不是重新读取活书得到的整文件统一 diff。要改变转换选项、加入原计划外的资源，
或重新审阅已变化的文件，仍须重新调用 `paragraphs.analyze` / `paragraphs.plan` 生成绑定。

## 预览、提交与恢复状态

- **Preview** 首行明确当前 Book 未改变，并分别列出文本、新增、重命名、删除、元数据、
  阅读顺序和 TOC 层级变更。预览事件同时记录 `applied_to_book=false`、
  `save_status=not_applied` 和完整 EPUBCheck 未运行。
- **Applied** 表示事务已写入当前内存 Book，不表示 EPUB 文件已保存。卡片和会话事件会
  显示资源成功/失败数、metadata/spine/TOC 结构操作成功/失败数、应用操作数、Book revision、
  `save_status=not_saved`，并明确完整 EPUBCheck 未运行。资源按提交前 Preview 中的唯一 ID
  统计；新增、删除和重命名仍算资源，结构类别不混进资源数。
- 实际执行的 `transaction.commit` 若失败，工具卡会展开为 **Apply failed**：首行明确未应用
  到当前 Book，并显示 0 个资源成功和该原子批次中未能应用的资源数。revision/源码冲突时
  `transaction_state=staged`，卡片说明暂存仍可审阅、重试或回滚；写入中途失败并完成整批
  回滚时则说明没有残留部分修改。若提交前范围本身无法读取，会明确显示资源结果不可用，
  不用 0 冒充已核对的数量。
- **Staged changes discarded** 只表示提交前暂存事务已丢弃，活书没有被该事务修改。
- 仅修改既有文本资源、且不含新增、删除、重命名、metadata、Spine 或 TOC 结构变更的
  commit，会在写入前自动保存受影响资源的原文，写入后封存同一批资源的精确文本与路径。
  Applied 卡会显示受保护的资源数和 **Restore this task**。
- 点击恢复时会先检查完整 `book_session_id`，并一次性比较每个受影响资源的当前路径和
  文本。若任务之后其中任一资源被人工或其他任务修改，恢复以
  `TASK_RESTORE_CONFLICT` 拒绝，**任何资源都不会先被改回**；未受该任务影响的后续编辑
  不参与恢复，也不会被覆盖。处理冲突并使目标内容回到该任务提交后的状态后可以重试。
- 成功恢复只改回该任务涉及的文本资源，使用宿主的 undoable text edit，Book revision
  递增且 EPUB 重新处于未保存状态；同一恢复点只能成功使用一次。运行中、存在未提交
  事务或已经切换书籍时不会恢复。
- 含书籍结构变更的 commit 会明确显示未创建任务恢复点，继续只在可用处依赖 Sigil Undo
  或提交前另行建立的宿主 Checkpoint。任务恢复点只存在于当前打开书籍的内存会话，不能
  代替保存、完整 EPUBCheck、进程崩溃恢复或整包结构快照。自动任务恢复点不能通过模型的
  普通 `checkpoint.restore` 绕过上述冲突检查。

因此，Agent 回答“完成”不能替代用户保存 EPUB，也不能替代完整 EPUBCheck。会话的
Conversation Markdown 导出保留同样的 Plan review / Preview / Applied / Rollback 状态边界。
段落计划导出还会说明可独立选择的 XHTML 组数；计划导出保留可读的资源、源码片段和 TOC
结构摘要，不重复输出 plan/digest。事务成功/失败也只输出一份可读资源结果，不重复原始
completion JSON；完整绑定、`resource_outcomes` 和实际批准的 `selected_resource_ids` 仍可
在脱敏 Debug JSON 的对应事件中审计。模型最终回答被要求复述这些精确结果，不能从旧的
`applied_changes` 操作数推测资源数。

## 工具（模型不能直接写 EPUB ZIP）

只读：`book.summary`、`book.resources`、`book.spine`、`book.toc`、`book.metadata`、`book.search`、`book.search_regex`、`book.check`、`resource.read_fragment`、`style.stylesheets`、`font.inventory`、`book.validate`、`manuscript.parse`、`paragraphs.analyze` / `paragraphs.plan`、`toc.inspect_hierarchy` / `toc.plan_transform`、`session.recall` / `session.tasks`。

其中 `book.resources`、`book.spine`、`book.toc` 和 `style.stylesheets` 使用 `offset` / `limit`
分页，返回原数组键以及 `total_count`、实际 `offset` / `limit`、`returned_count`、`has_more`；
尚有下一页时还返回 `next_offset`。前三者默认 100 项、每页最多 200 项；样式表包含每份 CSS
的受限正文片段，默认 12 项、每页最多 50 项。`limit` 超界会夹紧，`offset` 超过结尾会返回
稳定空页。模型在 `has_more=true` 时必须沿 `next_offset` 继续，不能把首页当作完整清单。

`font.inventory`、`book.validate` 和 `book.check` 也支持 `offset` / `limit`，默认 100、最多
200。它们一次包含多个数组，因此同一个 offset 分别切取每个数组，并用 `total_counts` /
`returned_counts` 按原数组键报告计数；`has_more` 与 `next_offset` 以最长数组为准。较短数组
可能在后续页为空，这是正常的终止状态，不表示其他数组也已读完。原来的 `embedded_fonts`、
`css_families`、`declared_families`、`issues`、`unused_images` 和 `wellformed` 键保持不变。

`book.search_regex` 默认最多返回 40 个命中，`max_matches` 可调但硬上限为 50。每个命中的
`match` 预览最多 240 个 UTF-16 单元，只返回前 8 个捕获组且每组最多 160 个 UTF-16 单元；
完整的资源 ID、offset、匹配 length、line、捕获组总数和已返回捕获组的原始长度仍会保留。
`match_truncated`、`captures_truncated` 和汇总的 `preview_truncated` 明确表示预览是否有删节；
需要核对或修改原文时必须按位置调用 `resource.read_fragment`（超长片段继续分段读取），不能把
搜索预览本身当作补丁来源。`match_limit_reached=true` 表示结果达到本次上限，并不证明后面必然
还有其他命中。

不区分大小写的字面量 `book.search` 默认最多返回 20 个命中，硬上限同样为 50；查询最长
512 个 UTF-16 单元，超出时返回 `SEARCH_QUERY_TOO_LONG`，应改搜更短的独特片段。每项
`snippet` 最多 240 个 UTF-16 单元，同时保留匹配 offset / length、snippet offset / 原始
length 和 `snippet_truncated`。需要精确原文时也必须改用 `resource.read_fragment`。结果达到
50 项后目前不能按 offset 读取后续命中；可用更独特的查询或限定上下文，不要把上限内结果
误报为全书无遗漏。

写入（先暂存，再预览，再提交）：`transaction.begin` / `preview` / `commit` / `rollback`、`resource.create` / `copy` / `delete` / `rename` / `replace_text` / `patch_fragment`、`content.replace_body` / `insert` / `wrap` / `replace_regex` / `wrap_plain` / `split` / `merge`、`image.insert`、`spine.set` / `spine.sort`、`style.link`、`toc.generate` / `toc.apply_transform`、`css.update_rules`、`metadata.update`、`content.fill_section`、`content.typeset_from_manuscript`、`paragraphs.apply`、`checkpoint.create` / `list` / `restore`。

立即作用于活书（不走 Agent 暂存事务；Plan 模式禁用）：`python.run`。

会话级（不改书，New Session 会清空）：`session.remember`、`session.task_add`、`session.task_update`。

会话记忆最多 64 条，key 最长 64、value 最长 2,048 个 UTF-16 单元；任务最多 128 项，title
最长 256、note 最长 2,048，status 只接受 `pending` / `in_progress` / `done` / `cancelled`。
容量满后仍可更新已有记忆和任务，继续新增则需 New Session。`session.remember` 与
`session.task_add/update` 只返回刚变更的项和总数，不再随每次写入回显完整状态。

无 key 的 `session.recall` 默认按最近写入顺序分页 16 条、最多 32 条；指定 key 仍可精确读取。
`session.tasks` 按创建顺序默认分页 20 项、最多 50 项。两者都提供 `total_count`、offset、limit、
`returned_count`、`has_more` 和可用时的 `next_offset`。每次模型请求的自动上下文只携带最后
创建的 8 个任务和最后写入的 8 条记忆；task note 与字符串 memory value 在自动上下文中最多
预览 512 个 UTF-16 单元，并明确报告省略或截断。完整项须按 key 或分页工具读取。

长文本必须已经在书里（拖进 Book Browser 的 TXT/HTML）。`content.wrap_plain` 用你提供的 heading/illustration **正则**套标签；`content.split` 按标题拆章；`content.replace_body` 用 `source_resource_id` 搬运整段 body。不要把小说正文贴进 `patch_fragment` / `replace_text`（后两者对模型有大小上限）。

批量套标签用 `content.wrap`，全书查找替换用 `content.replace_regex`（`$1` 捕获组）。图片必须先拖进 Images，再用 `image.insert` 按锚点插入 `<img>`。`book.check` 会分页报告损坏的图片链接、未引用图片和 XHTML 良构性。

`spine.set` 重排阅读顺序；`spine.sort` 按路径字母数字排序；`resource.rename` 可改文件名或换目录（提交时改 href）；`style.link` 重写 XHTML 的 stylesheet `<link>`；`resource.delete` 不能删 OPF/NCX/Nav 或最后一份 XHTML；`toc.generate` 按标题正则生成 TOC；`metadata.update` 支持任意 DC 字段，`_remove` 删除字段。

整理伪段落时使用 `paragraphs.analyze` → `paragraphs.plan` → `paragraphs.apply` →
`transaction.preview` → `transaction.commit`。这条流程复用菜单功能的保守 DIV/CSS
引擎；不要用正则逐段改写，也不要在 `paragraphs.apply` 前调用
`transaction.begin`。`apply` 会核对计划 ID、摘要、书籍/XHTML/CSS 修订并自行创建
独占暂存事务；Edit 模式批准卡可只勾选计划内的一部分独立 XHTML 组，省略选择则保持全量
计划语义。成功也不代表已写入活书。完整协议、错误和证据见
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

刷新在后台执行，设置页显示 **Loading models…**，完成后才以启动请求时冻结的 provider
写入内存缓存。刷新与连接测试不会并发；关闭设置窗口会取消仍在等待的目录请求。HTTP
错误可显示状态和服务端说明，但其中回显的当前 API Key 会替换为 `[redacted]`。

若要验证所选模型能否真正接受对话请求，使用同一页的 **Test Chat Completions**，不要把
模型列表刷新成功当作鉴权/模型可用证明。探测不会发送当前书籍或 Agent 工具定义。

请求体按提供商区分：

- DeepSeek：`thinking: {type: enabled|disabled}` 和 `reasoning_effort`
- OpenRouter：`reasoning: {effort, exclude: false}`（且只在该模型宣称支持 reasoning 时发送）；并带 `HTTP-Referer` / `X-Title`
- OpenCode Go / 其他：不发送 DeepSeek 的 `thinking` 字段，避免 400

协议仍是 OpenAI 兼容 Chat Completions。流式响应里 `reasoning_content`（以及 OpenRouter 的 `reasoning`）、`content`、`tool_calls` 分开解析。当某次请求带了 `tools` 时，同一会话后续请求必须回放助手的 `reasoning_content`（否则部分推理模型会返回 400）；不带 `tools` 的请求可以省略先前的思维链。

### 流式界面刷新

Agent 仍逐段接收并记录模型输出，但停靠栏最多约每 33 ms 合并刷新一次 Thinking 和
Answer，避免长回复为每个 token 重新排版整个标签。请求完成、失败、取消、工具调用、换轮
或其他非增量事件到达前会同步冲刷尚未显示的文本；完整 assistant 消息还会校准最终正文，
因此合并刷新不会截断尾段或改变会话事件顺序。New Session 与 transcript 重置会丢弃属于旧
会话、尚未显示的缓冲，防止延迟计时器把旧回复写进新会话。

Native Agent 不调用 MCP，也不把 MCP 当作内部 RPC。`paragraphs.*` 与原生
`toc.inspect_hierarchy` / `toc.plan_transform` / `toc.apply_transform` 当前是 Native
Agent 专用工具，不在公共 `sigil.*` MCP catalog 中。

## 导出

停靠栏 **Export** 菜单：

- **Conversation**：当前会话的 Markdown（用户 / Thinking / Answer / 工具），供阅读或贴给别人。
- **Debug log**：JSON，含会话事件（不含逐 token 的 `assistant_delta`，只保留完整 assistant/tool 事件；请求终态保留本地响应延迟，完成事件还保留服务端报告的 token 用量）、提供商（不含密钥）、以及跨轮保留的 HTTP 追踪（请求体最多 64KB，响应保留头尾，并记录同一组安全的响应延迟整数）。API Key 和 `sk-…` 会被替换成 `[redacted]`。

## Harness

Agent 循环在 `AgentRunner` 里，不在 UI 类中：

1. 组装上下文与历史
2. 流式请求模型
3. 按权限 allow / ask / deny 处理工具
4. ask 时先发批准卡片，批准后才执行
5. deny / 用户拒绝 / 取消时仍写入一条 tool-role 结果（`PERMISSION_DENIED` 或 `CANCELLED`），避免下一次请求因缺 tool 消息而 400
6. 为整轮生成 `run_id`，在终态记录覆盖安全收尾的总耗时、模型请求数、工具调用数和带请求覆盖率的 token 汇总
7. 用 `request_id` 配对模型请求开始/成功/失败/取消事件，记录真实流式调用耗时、首字节/首个有意义模型事件和可用的服务端 token 用量
8. 在模型与工具边界复核运行开始时冻结的 `book_session_id`
9. 对符合条件的纯文本 commit 在写入前创建任务快照、写入后封存冲突基线
10. 把事件追加到会话日志（transcript 的唯一来源）

书籍写入走现有 Book / 事务 / 撤销机制。
