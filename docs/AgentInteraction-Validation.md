# Agent 滚动、翻译并发与窗口交互验证

分支 `fix/agent-scroll-concurrency-dialog-isolation`。使用合成 EPUB、离线模型、回环 HTTP 和隔离设置，不写入私人书籍，不修改用户偏好。

## 对话跟随落后

原 `appendCard()` 在布局更新前读取滚动条旧 maximum；已有答案持续变长时不再滚动，表现为停在上一张卡片。新增回归在旧代码上明确失败。

改为在布局／滚动范围更新后合并一次跟随操作，覆盖流式答案、工具卡片及最终 Markdown。保留 33 ms 的文字合并刷新；用户上翻时不强行回到底部，回到底部或发送新问题后恢复跟随。新会话取消旧的跟随定时器并重置位置。

## Active 只有 1

执行器已有按配置并行派发的能力，默认 4、范围 1–8。复现了两次 429 将上限从 4 降到 1，而后续请求再成功也不恢复的问题。

加入逐级恢复：连续三次真实请求完整成功就恢复一路，受到当前配置上限约束；新的失败清零恢复计数，本地缓存命中不作为网络恢复依据。网络等待不占用 Book 事务；多个响应的定位检查和写回仍在 GUI 线程串行进行。

任务卡分别显示 Active、当前派发上限／配置上限、排队数及重试。只有一个剩余批次或单个术语合并任务时给出说明；不会为了显示更多 Active 重复执行同一任务。状态增加 configured_concurrency 和 peak_in_flight，沿用 effective_concurrency。

测试以阻塞模型同时保持四个工作请求，验证没有书籍事务跨越网络等待，随后 600/600 段完整写入；1000 段的限流样例验证降至 1 后恢复至 4，内容完整。真实模型适配器经回环 HTTP 在 224 个阅读单元／7 次请求中测得峰值 3 个同时请求。该样例用于验证并发行为，不用于预测真实模型的整书吞吐。

## 其他窗口的对话框／弹窗被收起

`ContentTab::ContentChangedExternally()` 主动调用 activateWindow，`FlowTab::ReloadTabIfPending()` 主动 setFocus。后台编辑后进入这些刷新路径，会把写入窗口带到前台。在 macOS Cocoa 上实际复现了另一书籍窗口的下拉弹窗被收起；普通设置／文件对话框在本次复现中没有被销毁。

删除刷新路径里的窗口激活／抢焦点动作，沿用资源文档对编辑器的更新并请求重绘。显式打开资源、切换窗口等用户操作仍使用原入口。

新增原生集成测试：两个 MainWindow、真实 Agent 资源工具和事务提交；另一窗口分别打开普通对话框、设置、文件选择及下拉弹窗，验证不关闭、不抢焦点、不改另一书内容。另启动一个隔离的 Sigil 测试进程，保持其设置与弹窗打开，验证后台刷新不将其收起。Cocoa 模式还检查系统原生文件选择面板。

## 复跑

先构建 Sigil 和相关单元测试。默认离屏运行窗口隔离用例：

```sh
python3 tests/run_agent_window_isolation_integration.py cmake-build-debug
```

在 macOS 桌面上验证真实窗口行为：

```sh
SIGIL_NATIVE_TEST_PLATFORM=cocoa python3 tests/run_agent_window_isolation_integration.py cmake-build-debug
```

测试使用临时设置和书籍副本；独立子进程是本测试程序，不连接已有 Sigil 用户进程。最终主程序构建成功，19/19 项 CTest 通过（73.06 秒）；额外的 Cocoa 原生窗口／原生文件选择／独立进程验证通过。构建及回归结果见 [翻译验证记录](BuiltinTranslation-Validation.md)。
