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

## 2. 真实会话复盘（第二轮）

以 `todo/Sigil_Agent_Research_2026-09-26` 为 PRD，另用本机 6 份真实调试导出
（2026-09-25/26，`deepseek-flash`，Auto 模式）核对失败现场。先确认已在 HEAD 修复的旧问题
（当前轮上下文压缩、`book.search` 全书分页、零宽正则、`python.run` 输出与只读
`python.inspect`），不重复处理；其余按根因修复：

| 真实现场 | 根因 | 处置 |
| --- | --- | --- |
| 模板排版运行 1141 s 后取消：`manuscript.parse` 7 次、`read_fragment` 105 次 | 默认解析只认紧凑写法，`序　章/幕　間/終　章/後　記` 与 `1.标题`、`特別篇` 式目录章节被漏掉（9 章识别 5 章、12 章识别 2 章） | 识别带空白的特殊标题（有分隔符与长度保护），并把目录中列出、正文独立重复的行当章节 |
| 同一会话 `unplaced_images` 007–012 | 只认 `［插图：name］`，不认 `（插圖001）`；解析器与排版引擎各有一份正则 | 共享一个标记规则，支持方/圆/尖括号与无冒号编号 |
| 排版结果 `alt="001"`、`alt="co1"` | 引擎把标记/文件名写成替代文本 | 放置的图片写 `alt=""`（封面“封面”），返回 `alt_review_required` |
| 删除 illus1/2 后 NCX 仍有“彩页”死条目，`book.check/validate` 均报 ok，模型整份重写 NCX | Agent 删除与 Sigil 原生删除一样不改导航，但结果不提示 | `resource.delete` 返回 `toc_references`/`toc_warning`；预检报 `TOC_TARGET_MISSING` |
| 双语对照 253 次逐段 `patch_fragment`，最终 HTTP 400 | 缺少“多块后各插一段”的批量原语 | `content.insert_after_blocks`（同文件 200 项一次暂存，整批原子） |

## 3. 当前能力矩阵

| 能力 | 当前事实 | 后续边界 |
| --- | --- | --- |
| Native 编辑底座 | 模式、范围、会话/资源 revision、审批、暂存预览、提交/回滚及任务恢复。 | 本轮未改执行权限模型、提交协议或保存语义。 |
| Skill 底座（P0a） | 结构化元数据、能力检测、声明式触发、显式选择、路由诊断；内置 4 项 Skill。 | 不支持完整 YAML、分模式部分配方、Skill 管理 UI。 |
| 出版预检（P0b） | `preflight.profiles/run/issues`：本地 19 条规则，工作文本（含未保存/暂存），快照绑定、分页、过期拒绝、可取消；EPUBCheck/Ace 固定 `not_run`。 | 未接入 EPUBCheck/Ace 适配器，未从内存 Book 导出临时 EPUB。 |
| 结构诊断（P0c） | `structure.inspect`：Spine 角色候选 + 证据/置信度、首标题、TOC 条目及 5 类标题/TOC 差异；Ruby 与全角空格正确处理；按快照缓存。 | 不含 `style.inspect_usage`、计算样式或样式迁移计划。 |
| 审阅式修复（P0d/P1b 局部） | `preflight.apply_fixes`：仅机械问题（路径大小写、缺 lang）与编辑决定的 `image_alts`；Auto/Ask 禁用；`release-preflight` Skill。 | 不生成 alt 文字；标题/TOC 同步计划、landmarks/page-list 尚未实现。 |
| 批量写入 | `content.insert_after_blocks`（译文、图注等块后插入）。 | 双语对齐 QA（漏段/重复段）仍是提案。 |
| 公共 MCP | [catalog.py](../examples/live_plugins/SigilMcpServer/sigil_mcp/catalog.py) 38 个通用工具。 | **暂不维护**，本轮零改动。 |
| P1/P2 其余 | `nav.inspect`、`a11y.audit/decide`、`terminology.*`、跨卷参考库、校样 sidecar。 | 仍属提案。 |

## 4. 交付清单（分支提交）

1. `feat(agent): route skills by declared, policy-available capabilities`
2. `docs(agent): document skill metadata contract and capability audit`
3. `fix(agent): detect spaced special and TOC-listed manuscript headings`
4. `fix(agent): recognize parenthesized and colon-less illustration markers`
5. `feat(agent): add read-only release preflight tools`
6. `docs(agent): document preflight rules and manuscript heading coverage`
7. `fix(agent): stop generating file-name alt text for placed images`
8. `feat(agent): add release-preflight skill and finish typeset with preflight`
9. `fix(agent): report TOC entries left pointing at a deleted resource`
10. `feat(agent): add read-only structure.inspect for roles and heading/TOC drift`
11. `docs(agent): route structure questions to structure.inspect`
12. `feat(agent): stage reviewed preflight fixes and editor alt decisions`
13. `feat(agent): add content.insert_after_blocks for batched per-block inserts`
14. `perf(agent): bound whole-book preflight and structure scans`

工具语义、限制与状态字段见 [Native Agent](NativeAgent.md)。

## 5. 验证证据

```sh
/Users/parsle/Qt/Tools/Ninja/ninja -C cmake-build-debug
ctest --test-dir cmake-build-debug -R '^agent_' -j 2 --output-on-failure
SIGIL_TYPESET_SAMPLE_EPUB=todo/Sigil-Enhanced-Native-Agent-PRD/test/轻小说模板.epub \
  /usr/local/opt/python@3.11/bin/python3.11 tests/run_agent_workspace_package_integration.py cmake-build-debug
```

- Agent 全组 **18/18 通过**（新增 `agent_preflight`）；全套 ctest 170 项中 1 项
  `code_view_selection_policy` 在 `-j 4` 下偶发失败，串行重跑 3/3 通过，分支未改非 Agent 源码。
  翻译覆盖与 `ci_packaging` 通过；3 项 `cmoa_*` 因缺样例按设计跳过。
- **真实原稿解析**（本机 17 份 TXT 中 7 份小说）：《聖女老師》5→9 章、前置/正文插图 0→5/12；
  《不才惡女》2→12 章；其余 4 份章节不变。
- **真实模板端到端**（`SIGIL_TYPESET_SAMPLE_EPUB`，真实 MainWindow/SigilBookWorkspace）：
  `chapters=9, body images=12, missing=[], broken images=0`；预检发现模板 CSS 引用的 3 张
  不存在图片（`book.check` 从未检查 CSS url）；`IMG_ALT_FILENAME` 18→1（剩余为未填充
  占位页），经 `image_alts` 决定后提交清零；`structure.inspect` 22 页角色全部正确、差异 0；
  删除 TOC 页报告 `toc_references=1`；20 段译文一次 `insert_after_blocks` 提交，章节仍良构。
- **合成大书** 3000 XHTML（Debug）：`preflight.run` ≈0.57 s、翻页 ≈0.12 s；
  `structure.inspect` 2.2 s→≈0.4 s，续页 ≈0.12 s；Stop 可中断两类扫描。
- 宿主 Skill 路由：`Live skills: 4, modes=4, selected requests=10, book unchanged`。

MemoryBookWorkspace 与模拟 Provider 仅用于确定性回归。本轮外部 LLM 请求为 **0**：
未重跑真实模型会话，因此“排版耗时/请求数下降”是由工具结果变化推断的预期，**未经真实
模型复测**；也未运行 EPUBCheck/Ace 或跨阅读器验收。

## 6. 调研补充与剩余边界

已核对 [EPUBCheck 说明](https://www.w3.org/publishing/epubcheck/docs/) 与
[官方 CLI](https://www.w3.org/publishing/epubcheck/docs/cli/)：JSON/XML 报告默认每类消息
最多 25 项，接入时须用 `--maxOfEachMessage unlimited` 或报告截断。本机有 OpenJDK 但无
epubcheck，本轮未接入。Adobe 预检页面未能提取，相关描述保持为历史参考。

建议下一步（按优先级）：

1. **真实模型复测**：用同一本《聖女老師》原稿与模板，以 Auto/Edit 跑一次“排版 + 预检”，
   记录请求数、token、耗时，与 9/26 会话（32 请求、1141 s、取消）对比。
2. **EPUBCheck 适配器**（P0b 余项）：从内存 Book 导出受控临时 EPUB，记录版本/退出码/
   截断，路径映射回资源 ID；缺失时保持 `not_run`。
3. **标题/TOC 同步计划**（P0d 余项）：基于 `structure.inspect.differences` 生成可选择的
   计划（以标题为准或以 TOC 为准），复用 plan/apply 协议。
4. P1：`nav.inspect`/landmarks、双语对齐 QA、跨卷术语（需显式只读参考源与 sidecar）。
