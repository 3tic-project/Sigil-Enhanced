# 2026-09-05 PRD 验收复核

复核日期：2026-09-19。当前代码分支为 `integration/prd-2026-09-05`；本轮不再
扩展 Native Agent，只检查 OPF、文本选择、TOC、Clips、BookLive/Cmoa 和共用事务能力。
真实 Cmoa EPUB 仅从本地私有路径读取，不加入仓库，也不写回原文件。

## 自动化结果

macOS 15.7.7、Qt 6.7.3、AppleClang 17、Ninja Debug 下，29 项非 Agent 定向 CTest
在最终 OPF 修复后再次全部通过，耗时 65.22 秒：

- OPF 原文/字节、普通导出、缺 nav 修复、插件 package 更新、Live package 事务和
  故障补偿；
- 正文单元/句子选择策略及真实 CodeView 双击；
- TOC 纯树变换及真实 EditTOC/Nav/NCX 集成；
- Clips 实际 QAction、快捷键、工具栏与插入链；
- BookLive 兼容预设、独立 Cmoa 预设、菜单隔离、DIV 对话框与 20 MiB 性能；
- 简中、繁中、日文目录严格覆盖。

随后新增 3 项真实 MainWindow 无操作回归，分别覆盖有效 EPUB 2、有效 EPUB 3 和缺 nav
的私有 Cmoa 样本。测试通过真实 `Save` 动作、真实 `QFileDialog` 驱动 `Save As` 与
`Save A Copy`，三份输出均与输入整包字节相同；当前路径、Book modified 状态正确。
同一矩阵打开 Metadata Editor 后走真实 reject 槽，确认 OPF 源码、Book 状态和磁盘 EPUB
均未改变，关闭 O02/O12 的本机自动化缺口。

另有 2 项 EPUB 2/EPUB 3 MainWindow OPF 完成矩阵通过（17.91 秒）：资源改名后的
ZIP 路径、manifest 和 Nav/NCX 目标一致；外部改写源 EPUB 时真实 Save 拒绝覆盖；
目标为目录时真实 Save A Copy 失败且源/目标均完整；正文编辑精确 Undo（包括内容已写入
临时工作目录的边界）恢复无修改和原包复制资格，同时不会清除既有结构修改。

真实样本测试显式绑定 SHA-256
`cc95eccdc564e3a55b076966d99116dc7690ca8b88058cb6af8951b7b95179a9`。结果为 23 个
XHTML、原始 `div=1211`、`p=2`、`h1=12`、`ruby=250`、`rt=250`、`br=86`；其中
12 个正文文件可应用 958 个 DIV-to-P 源码范围补丁，5 个文件保持仅供复核。计划不改
live Book 或原 EPUB，第二次计划为零改动。

最新构建下，扩大后的 35 项非 Agent 定向 CTest 全部通过（最终复跑 100.04 秒），含真实 Cmoa
导出和 Sigil Preview 视觉集成。导出 ZIP 成员集合不变；逐成员比较只有计划内 12 个
XHTML 与 OPF 修改时间变化，CSS、NCX、`container.xml` 和其余资源字节相同。
修复了原导出路径在加载未编辑文本时重写换行符、NCX 构造时覆盖已有源文件以及创建
OPF 资源时无条件重写 `container.xml` 的问题。旧 BookLive 正规化器与兼容预设没有改动。

D06 本机代表章节使用 Sigil Preview（QtWebEngine、1200×869 内容视口、
`Hiragino Mincho ProN`、纵排）比较 `p-002`、`p-007`、`p-013` 转换前后所有
215 个目标块的关键计算样式和几何位置，均一致。Firefox 155.0.1 使用样本原有
`serif-ja-v, serif-ja, serif` 字体栈和纵排样式，在 1200×900、4000×900 两个
固定视口截取同三章，6 对 PNG 逐字节相同；这是独立 Gecko 引擎的代表视口证据，
不等于所有 EPUB 阅读器或整书每个滚动位置均已人工检查。

完整 EPUBCheck 5.4.0 对原书和临时规范化导出均已运行：原书为 6 类报错、
31 个错误位置；导出为 5 类报错、30 个错误位置。按错误 ID、正文、资源路径和
行列逐项比较，新增错误位置为 0，消失的是原包 mimetype 顺序错误 `PKG-006`。
原书的缺 nav、NCX 标识不一致、悬空引用和非法属性等既有错误仍在，因此两份 EPUB
都不能宣称完整合规。原私有文件 SHA-256 复核未变。

## 分功能结论

| PRD | 当前结论 | 已有证据 | 仍未关闭 |
|---|---|---|---|
| OPF O01–O12 | 本机核心通过 | O01–O12 均有源码字节或原生入口证据；O02/O12 覆盖 EPUB 2、EPUB 3、缺 nav 私有样本；O05/O09–O11 另覆盖 MainWindow 改名、外部冲突、磁盘失败和跨工作目录写入的精确 Undo；真实 Cmoa 导出无关成员逐字节不变 | 强杀/断电、Windows/Linux、独立 EPUB 阅读器不在本机证据内；完整 EPUBCheck 已运行但原样本不合规 |
| 选择 S01–S12 | 本机自动化基本通过 | Unicode、Ruby、实体、同行/跨行、属性/危险结构回退、修饰键、设置持久化、真实鼠标坐标、1 MiB/20,000 标签预算均有测试 | 右键/三击/拖拽、分屏和 IME/辅助技术仍需发布环境人工回归；Windows/Linux 未执行 |
| TOC T01–T12 | 本机自动化基本通过 | 单/多选、接管规则、边界、稳定 ID、Undo/Redo、Nav/NCX 写回、取消、双导航选择及 10,000 项性能已覆盖 | Windows/Linux、真实大型书、EPUBCheck/独立阅读器、强杀提交恢复未执行 |
| Clips C01–C10 | C01–C07 通过，C08 部分，C09/C10 未关闭 | 实际 QAction 绑定、自定义/清空、固定槽位、不重编号、HTML/Ruby tooltip、溢出菜单、插入链和 100%–200% 离屏缩放均有测试 | 三平台原生快捷键显示、深浅/高对比度人工视觉和屏幕阅读器/键盘焦点需人工验收 |
| DIV D01–D14 | 本机代表样本基本通过 | D01–D05、D07–D11、D13/D14 有真实样本或合成证据；D06 的 Sigil Preview 计算样式/几何和 Firefox 双视口截图对照通过；BookLive 动作/预设保持不变，Cmoa 独立入口；复杂 CSS 失败关闭；源码范围补丁和幂等通过 | D06 不覆盖所有阅读器/整书人工检查；D12 已覆盖取消、revision/CSS 冲突和进程内批次边界，但未证明断电级原子性 |
| Agent A01–A12 | 本轮不扩展、不重新宣称完成 | 既有实现和测试保留 | G8 仍需三项真实用户故事的完整人工演示；不能用大量协议/单元测试替代 |

## 全局发布门槛

| 门槛 | 状态 | 结论 |
|---|---|---|
| G1 打开不改变 | 本机核心通过 | 有效 EPUB 与缺 nav 合成/真实 Cmoa 路径均不因诊断变脏；仍缺三平台真实样本矩阵 |
| G2 无操作保存 | 本机自动化通过 | EPUB 2、EPUB 3、缺 nav 私有样本的 MainWindow Save/Save As/Save A Copy 均整包字节相同；Metadata Editor 取消也不破坏原样保存资格 |
| G3 精准修改 | 本机核心通过 | 正文、metadata/package 局部变化及无关成员保留有原生证据；真实 Book Browser 改名在 EPUB 2/3 输出中同步 ZIP、manifest 与 Nav/NCX；Cmoa 实书导出只改 12 个正文成员和 OPF 修改时间 |
| G4 导航稳定 | 本机核心通过 | 树不变量、稳定 ID/target、Nav/NCX 原节点写回及性能通过；跨平台/阅读器未验 |
| G5 DIV 保真 | 本机代表样本通过，发布门槛仍部分 | 真实 Cmoa 结构、Ruby、标题、BR、幂等和源码范围通过；Sigil Preview 全目标块几何/样式及 Firefox 6 对代表视口截图一致；完整 EPUBCheck 无新增错误 |
| G6 事务 | 部分 | 取消、过期计划、多提交阶段故障注入、真实磁盘失败和进程内补偿通过；强杀/断电级持久化原子性不成立 |
| G7 UI | 未关闭 | 当前只有 macOS offscreen/原生集成；缺 Windows、Linux、主题/DPI/辅助技术人工矩阵 |
| G8 Agent | 本轮未验收 | 遵循 PRD：仍需三项端到端用户故事，不继续堆叠 Agent 功能代替验收 |

## 发布判断

代码层面不存在本轮发现的阻断性回归，35 项非 Agent 定向检查全部通过；BookLive 与
Cmoa 已按独立产品入口和独立预设收敛，OPF O01–O12 的本机核心路径已有可重复证据。
整份 PRD 仍不能标记“全部完成”：G6 的断电级恢复、G7 的 Windows/Linux/主题/DPI/
辅助技术矩阵、G8 的三项真实 Agent 用户故事，以及独立 EPUB 阅读器的整书人工检查
尚未验收。完整 EPUBCheck 已执行但输入本来不合规，应表述为“转换无新增错误”，
不能写成“EPUB 合规通过”。
