# Native Agent

Sigil-Enhanced 内置的 **Native Agent** 是当前打开书籍的 EPUB 助手。它理解这本书的结构、资源、样式、字体、Spine、目录和元数据，可以用自然语言查询、规划，并在你批准后做可撤销的修改。

它不是编程 Agent，也不会操作整台电脑。它不调用 MCP，也不走 Python 插件。

## 打开方式

在主窗口 **查看** 菜单中打开 **Agent** 停靠栏（与 Book Browser、Preview 同类）。停靠栏包含：

- 模式：**Ask** / **Plan** / **Edit**
- 当前模型（只读，来自偏好设置）
- 上下文芯片：当前书、当前文件、选区（可开关，范围会显示在状态行）
- 输入框：**Enter 发送**，Shift+Enter 换行
- **Stop**（会立刻中止正在等待的 HTTP，不只在收到首包之后）、**New Session**
- **Export**：导出当前对话（Markdown）或完整调试日志（JSON，已脱敏）
- 事件卡片：每一轮独立的用户 / 折叠 Thinking / 回答；工具卡片会更新运行状态；批准带影响说明；预览列出暂存资源；错误

Thinking（模型的 `reasoning_content`）不是给用户看的最终答案；答案只在 Answer 卡片里。新的一轮不会覆盖上一轮的回答。

## 模式

| 模式 | 能做什么 |
|---|---|
| **Ask** | 只读。可摘要、搜索、读片段、列字体、校验。不能改书。 |
| **Plan** | 可以 `transaction.begin`、暂存 patch/CSS/metadata，并 `preview`。不能 `commit` / `restore`。活书保持不变。 |
| **Edit** | 可通过工具改书。可逆编辑默认要你点 **Approve**。提交后可用 Sigil 的撤销。 |

**Stop** 会取消当前轮次，并回滚尚未提交的暂存事务。已经 commit 的步骤仍可撤销，并标为 Applied。

## 工具（模型不能直接写 EPUB ZIP）

只读：`book.summary`、`book.resources`、`book.spine`、`book.toc`、`book.metadata`、`book.search`、`resource.read_fragment`、`style.stylesheets`、`font.inventory`、`book.validate`。

写入（先暂存，再预览，再提交）：`transaction.begin` / `preview` / `commit` / `rollback`、`resource.patch_fragment`、`css.update_rules`、`metadata.update`、`checkpoint.create` / `list` / `restore`。

`resource.patch_fragment` 用 **当前原文子串** 定位，不要让模型数字符偏移。必填 `expected_text`（从 `read_fragment` 的 `text` 原样复制，可以是一整行）。子串出现多次时用 `start_line`（来自 `read_fragment.lines` 的 1-based 行号）消歧。区间不能切到半个标签（例如把 `</title>` 切成 `</titl`）。预览会带 staged excerpt。

发给模型的 function 名会把点换成下划线（`book.summary` → `book_summary`），因为 DeepSeek/OpenAI 只接受 `^[a-zA-Z0-9_-]+$`。内部仍用带点的名字。

没有 shell，没有技能脚本，不会把字体二进制或整本书 XHTML 送给模型。若提交时的 `expected_revision` 与当前书籍 revision 不一致，会返回 `BOOK_REVISION_CONFLICT`，不会改书。

HTTP 出错时会带上状态码和服务器返回的 `error.message`（不会把 API Key 写进 transcript）。

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

Native Agent 不调用 MCP，也不把 MCP 当作内部 RPC。

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
6. 把事件追加到会话日志（transcript 的唯一来源）

书籍写入走现有 Book / 事务 / 撤销机制。
