# Live Python 插件系统安全与完成度复审

复审日期：2026-07-15  
范围：Live v2 宿主、Python SDK、v1 兼容适配、插件管理器运行时选择、OpenRPC 与示例。

## 结论

本轮指定的 1、3、5、6、8 已实现；2 已提供保存源 EPUB 和导出到其他绝对路径两种模式；
方法级插件权限系统已取消；9 的协议、SDK、示例、输入校验和单写者测试已补强。
当前未发现可由未认证本地客户端直接调用 Live RPC 的路径，也未发现插件提交失败后继续
保留 writer lease 的路径。

Live 插件仍是**可信本地代码**，不是沙箱。插件进程与 Sigil 使用同一系统账户，可绕过 RPC
直接访问该账户可访问的文件、网络和进程。取消方法级权限后，这一信任边界必须由安装来源、
代码审查和发布签名承担，不能把 local socket token 当作恶意插件隔离机制。

## 九项复审

| # | 状态 | 实现与证据 |
| ---: | --- | --- |
| 1 事务原子性 | 已修复，仍有崩溃窗口 | commit 保存文本、二进制、archive、结构和 modified 状态的回滚日志；任一步失败执行补偿回滚并释放全局 writer。`7e033e7f6`。 |
| 2 output 写回 | 已完成 | `save_source()` 走原生 Save；`export_epub(path)` 可导出任意绝对 `.epub` 副本，返回 `mode=source/copy`。`1b639c2c2`。 |
| 3 input 安全 | 已修复 | 上传结束后校验 ZIP、mimetype、container、规范化 OPF 路径及 package 必需段；失败删除上传且不替换当前 Book。`291469a82`。 |
| 4 权限系统 | 已取消 | dispatcher 不再读取或检查 permissions，握手不再返回权限；旧 XML 字段仅为清单兼容。示例已删除该字段。`1b639c2c2`。 |
| 5 运行入口一致性 | 已修复 | 菜单、QuickLaunch 和 Automate 统一通过 `ExecutePluginByName` 选择 v1/v2；Automate 等待 live command 完成并拒绝不适用的 book-session。`291469a82`。 |
| 6 单写者 | 已修复 | `PluginSessionManager` 持有跨 Session writer lease，事务 begin 获取，commit/rollback/finish/destructor 释放。`7e033e7f6`。 |
| 7 事件与资源压力 | 已修复主要边界 | 高频事件按 50/100ms 和资源 ID 合并；4 MiB socket backlog 丢弃并报告；限制请求率、读写流、上传、临时文件、聚合快照与控制台。`f57a653b0` 及本轮配额补充。 |
| 8 缺失 API | 已完成 | `materializeTemporary`、readMany continuation、分块二进制写、结构化 metadata/spine 更新均已接入宿主、SDK、OpenRPC 和示例。`8904fd1b0`。 |
| 9 测试不足 | 已改善，未达到全 GUI 自动化 | 20 个 CTest 目标；OpenRPC/dispatcher 集合锁定；SDK 聚焦测试；中英文文档与示例公共 SDK 方法覆盖清单；输入、OPF 结构更新和全局 writer 有 C++ 测试。 |

## 防护边界

- socket 使用一次性 token，只接受一个客户端；握手后清除 token 并关闭 listener。
- 正常帧上限 8 MiB；大二进制使用最多 2 MiB 的 chunk。
- 每 Session 最多 512 请求/秒、8 个读快照且合计 1 GiB、2 个二进制写上传、1 个 input
  上传、16 个物化文件且合计 512 MiB；单个事务二进制最多 256 MiB。
- `materializeTemporary` 不接受目标路径，目录由 Session 独占并在结束时删除；文本来自内存，
  临时文件修改不会自动导回。
- archive 路径要求 canonical Book path，拒绝 symlink、越界路径和受保护 EPUB 文件。
- input EPUB 在替换 Book 前完成结构校验；加载失败保留原 Book。
- 所有写入要求 revision 或 SHA-256 并发令牌；跨 Session 同时只允许一个事务。

## 测试证据

```sh
cmake --build cmake-build-debug -j4
ctest --test-dir cmake-build-debug --output-on-failure
```

当前环境结果为 20/20。关键自动化资产：

- `safe_archive_extractor_test`：归档预算、路径与 input EPUB 验证。
- `plugin_protocol_test`、`plugin_transport_integration_test`：帧边界与真实 socket。
- `plugin_launcher_integration_test`：launcher 握手。
- `plugin_text_edit_test`、`plugin_text_transaction_test`：UTF-16 patch、staging、rollback。
- `plugin_package_update_test`：metadata/spine 命名空间、转义、属性、顺序和负向输入。
- `plugin_writer_lock_test`：跨 Session writer lease。
- `python_live_sdk_test.py`：分页、流、hash、结构化事务、输出模式和自源过滤。
- `plugin_openrpc_contract_test.py`：dispatcher 与 OpenRPC 方法集合、引用、错误码。
- `live_plugin_examples_test.py`：可安装示例语法与全部公共 SDK 方法覆盖。
- `live_plugin_docs_test.py`：从 SDK 提取公共方法，锁定中英文 API 手册覆盖及关键安全边界。

## 仍未完成或不可宣称完成

1. **进程崩溃/断电级原子提交**：当前是进程内补偿回滚，不是跨多个文件的持久化 WAL。
   Sigil 在 commit 中途被杀死时无法执行补偿。发布前应增加故障注入集成测试；若要求崩溃一致性，
   需设计 staging tree + 原子目录切换或可重放 journal。
2. **GUI 端到端自动化**：source save、copy export、失败 input 保留当前 Book、Automate 等待和
   插件管理器 v1/v2 切换已有实现与构建覆盖，但尚无可重复的 GUI 驱动集成测试。
3. **大二进制内存峰值**：chunk 在传输阶段落临时文件，但 End 阶段和事务对象仍把最多
   256 MiB 的新旧数据读入内存。上限阻止无界增长，但后续可把 `StagedBinaryChange` 改为文件后端。
4. **恶意可信插件**：没有 OS sandbox、网络隔离、文件系统 allowlist 或首次运行信任提示。
   这是取消权限系统后的明确产品风险，不应在文档或 UI 中描述为已隔离。
5. **平台矩阵**：本轮只在当前 macOS Debug 构建验证；Windows/Linux socket 权限、路径身份和
   保存行为仍需 CI/人工矩阵。
6. **设计稿中的扩展事件/便捷别名**：OpenRPC 是当前实现契约。设计稿曾列出的
   `book.resourceMoved/resourceRenamed/revisionChanged/selectionChanged`、
   `editor.opened/closed/getCursor` 和 `session.cancel` 等细粒度通知或别名尚未作为公开 RPC；
   当前资源移动/重命名会产生既有 resource/structure 变化，取消由宿主控制台和进程生命周期处理。
   若插件确实需要区分这些原因，应在后续 API minor version 中增加，而不是推断现有事件。

因此，“API 方法存在与 SDK 公共面覆盖”已完成；“所有平台、GUI 流程、崩溃恢复 100% 自动化
验证”尚未完成，不能作为当前发布声明。
