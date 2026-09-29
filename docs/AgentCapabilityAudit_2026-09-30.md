# Agent 能力审计与首轮优化记录

日期：2026-09-30。审计基线：`master@1487c4912`；实施分支：
`feat/agent-capability-p0a`。研究输入为本地
`todo/Sigil_Agent_Research_2026-09-26` 的七份文档，其原始基线为
`ed479654ea173119666bdd844e8667f454c944ea`。本记录区分源码事实、已交付切片和未来提案。

当前范围：**只优化 Native Agent，MCP 暂不维护**。MCP 条目仅为本次只读审计事实，
不安排适配器修改、工具暴露、相关文档同步或 MCP 回归；研究稿中的 MCP 交付步骤暂缓。

## 1. 审计发现

| 优先级 | 发现与证据 | 本轮处置 |
| --- | --- | --- |
| P1 | [AgentSkills.cpp](../src/Agent/Core/AgentSkills.cpp) 的旧解析器先去掉缩进，丢失多行字段边界；折叠的 `allowed-tools` 未保留完整工具列表。 | 已修复；新增折叠字段、CRLF、引号、列表、重复字段和畸形元数据回归。 |
| P1 | [PromptAssembler.cpp](../src/Agent/Core/PromptAssembler.cpp) 原先无条件列出 Skill，并按关键词注入正文，没有对照实际注册且当前模式允许的工具；部分旧测试在缺少原生工具时仍期待配方注入。 | 已接入同一 `PermissionPolicy` 过滤后的工具集；缺少声明能力时隐藏目录项和完整配方，并记录原因。 |
| P2 | Skill 路由缺少声明式触发条件与决策证据；添加专业 Skill 时难以解释为何命中。 | 已支持触发词、显式名称选择、禁用自动触发和结构化诊断；尚无 UI 配置或通用优先级系统。 |

`allowed-tools` 不是执行白名单，也不是权限来源。实际调用仍经过注册表、
`PermissionPolicy`、审批及宿主校验；伪造声明不能注册工具或扩大模式权限。

## 2. 当前能力矩阵

| 能力 | 当前事实 | 后续边界 |
| --- | --- | --- |
| Native 编辑底座 | 已有模式、范围、会话/资源 revision、审批、暂存预览、提交/回滚及任务恢复。 | 本轮不改变执行权限、提交协议或保存语义。 |
| 专业原生流程 | [Controller](../src/Agent/Core/AgentController.cpp) 注册通用、段落和 TOC 工具；另有 `proof.*`、文稿解析与模板排版。 | TOC 重挂不等于 XHTML 标题同步；校对候选不等于语义错误。 |
| 本地诊断 | [BookTools.cpp](../src/Agent/Tools/BookTools.cpp) 提供 `book.check`、结构性 `book.validate` 和字体清单。 | 不是完整 EPUBCheck、Ace 或阅读器验收。 |
| 公共 MCP | [catalog.py](../examples/live_plugins/SigilMcpServer/sigil_mcp/catalog.py) 列出 38 个 `sigil.*` 工具，覆盖 Book/编辑器读取和通用事务。 | 没有 Native 的 `proof.*`、原生段落/TOC 计划服务；本轮没有新增 MCP 工具。 |
| P0b/P0c | 研究中的 `preflight.*`、`structure.inspect_roles`、`style.inspect_usage` 尚无对应 Native typed tool。 | 需宿主服务与只读检查先行，不能仅靠 Skill 文案冒充实现。 |
| P1/P2 | `nav.inspect`、`a11y.*`、`terminology.*`、跨卷参考库及校样 sidecar 仍属提案。 | 不否认现有手工编辑能力，但没有交付这些一等工作流。 |

## 3. 本轮 P0a 切片

- 元数据支持版本、触发词、能力依赖、工具列表和书籍依赖；兼容旧 `sigil.*` 字段。
- 三个内置 Skill 已补声明式触发词；英文词边界避免 `DIV` 命中 `dividend`。
- 显式 Skill 名称优先于其他自动触发；可用 `auto_trigger: false` 禁止自动注入。
- Skill 能力检测直接复用请求工具目录的权限过滤结果，不另写授权规则。
- `model_request_started.tool_context.skills` 保留加载/可用数量及每项路由原因。
- 缺少任一声明工具时保守隐藏整个配方，包括 Ask/Plan 下需要 commit 的完整编辑配方。

元数据格式与限制见 [Native Agent](NativeAgent.md)。这是 P0a 首个切片，
不是整个研究路线完成：全局系统提示仍含静态工具说明，没有实现分模式的部分配方、
Skill 管理 UI、完整 YAML、通用优先级或研究样例中的全部策略字段。

## 4. 验证证据

```sh
/Users/parsle/Qt/Tools/Ninja/ninja -C cmake-build-debug -j 2 \
  Sigil agent_harness_test agent_prefix_cache_test agent_launch_test agent_typeset_test
ctest --test-dir cmake-build-debug -R '^agent_' -j 2 --output-on-failure
```

- 正式 Sigil 与四个受影响的核心测试目标构建成功，捆绑 Python 的构建门禁通过。
- Agent 全组 **17/17 通过**，耗时 **16.75 秒**。
- [排版测试](../tests/agent_typeset_test.cpp) 覆盖解析、版本拒绝、依赖缺失、显式选择、触发禁用、名字边界和伪造工具声明。
- [真实宿主测试](../tests/agent_workspace_package_integration_test.cpp) 使用临时 EPUB、MainWindow、SigilBookWorkspace 与生产同样的会话/原生工具注册，检查三个内置 Skill 的四种模式。
- 宿主日志：`Live skills: 3, modes=4, selected requests=6, book unchanged`。
- 初次宿主链接因正式构建未完成而失败，构建完成后重跑；随后修正测试缺少会话工具的装配差异，最终门禁通过，未绕过断言。

单元测试中的 MemoryBookWorkspace 和模拟 Provider 仅用于确定性回归；真实宿主测试
不调用外部模型。本轮外部 LLM 请求为 **0**，未测模型质量、token、生产书籍全量任务
或跨阅读器视觉效果；这些不能由上述通过结果推断。

## 5. 调研补充与下一步

已重新核对 [EPUBCheck 说明](https://www.w3.org/publishing/epubcheck/docs/)
及 [官方 CLI](https://www.w3.org/publishing/epubcheck/docs/cli/)：支持完整 EPUB 与单文件检查，
单文件只执行部分规则；支持 JSON/XML 报告。JSON/XML 默认每类消息最多记录 25 项，
P0b 应显式使用 `--maxOfEachMessage unlimited` 或报告截断，不能误报全量检查覆盖。
本轮未安装或运行 EPUBCheck。Adobe 预检资料页面未能提取正文，原研究中的产品说明
保持历史参考，不标为本轮重新核实的事实。

下一步仅在 Native Agent 做 P0b：从当前内存 Book 导出受控临时快照，记录检查器版本、退出状态、
资源定位、分页和 `not_run`；不得把磁盘旧书或局部校验当完整出版验收。
随后再做 P0c 的角色、样式和标题/TOC 差异诊断；分模式配方完善可在同一底座继续推进。