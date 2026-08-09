# 高级正则工作台

高级正则工作台用于把多条正则规则组成一个可预览、可保存、可撤销的批处理方案。它支持二级正则、递归替换和跨匹配变量，并在写入 EPUB 前先在内存中完成整批计算与 XML 校验。

## 打开与基本流程

从 `Enhancement > Advanced Regex Workbench...` 打开工作台。

推荐流程：

1. 选择作用域并编辑规则；规则按列表从上到下执行。
2. 点击 **Dry Run**。结果和变量只显示在工作台中，不修改书籍，也不发布 Session 变量。
3. 检查“替换前/替换后”、迭代次数和变量变化。
4. 点击 **Apply**，确认目标资源数。工作台会重新抓取书籍快照并从头运行，不会复用可能过期的 Dry Run 结果。
5. 全部规则与 XML 校验成功后，Sigil 创建恢复检查点，再把每个有变化的资源写入一次。

Apply 后对话框保持打开，可继续调整方案。运行期间可以点击 **Cancel Run**；取消会在当前受限正则操作结束后生效，书籍文本和变量都不会发布部分结果。

## 规则字段

| 字段 | 说明 |
| --- | --- |
| Enabled | 跳过或启用该规则。 |
| Secondary mode | `None`、`PreSearch range`、`Accept when secondary matches` 或 `Reject when secondary matches`。 |
| Secondary regex | 二级正则；`None` 模式不保存此字段。 |
| Find regex / Replacement | PCRE2 查找表达式和替换文本。原有 `\g{name}` 仍表示当前匹配的命名捕获组。 |
| Repeat until no matches remain | 反复执行同一条规则，直到再次探测时没有匹配。达到迭代/增长/数量上限、停滞或循环均视为失败，不提交部分结果。 |
| Allow zero-length matches | 仅递归规则可用；默认关闭。枚举器会按 Unicode 与 CRLF 边界安全前进。 |
| Capture variables only | 只枚举被接受的匹配并存储命名捕获组，不展开 Replacement，也不修改文本。此模式不能与递归、零长度递归或替换变量展开同时使用。 |
| Expand `${var:name}` | 在替换文本中读取工作台变量。未定义变量会使本次运行失败。它不改变 `\v` 或 `\g{name}` 的既有语义。 |
| Store all named captures | 将主正则和已接受 Filter 的全部命名捕获组写入变量。 |
| Capture variables | 逗号或空白分隔的命名捕获允许列表；只存储指定名称。 |

### 二级正则的两种语义

`PreSearch range` 先用二级正则找外层范围，再只在其第一个捕获组中执行主正则。例如，只清理正文段落内的重复中文逗号：

```text
Secondary mode: PreSearch range
Secondary regex: <p class="body">([\s\S]*?)</p>
Find regex: ，+
Replacement: ，
```

`Accept/Reject when secondary matches` 先枚举主匹配，再在每个主匹配的文本内部运行二级正则，用于保留或排除候选。被 Reject 的候选不会写入变量。

### 递归替换

递归适合必须多轮才能收敛的规则。例如把任意长度的重复空格逐轮折叠：

```text
Find regex: [ ]{2}
Replacement: " "（实际填写一个空格，不含引号）
Repeat until no matches remain: on
Maximum iterations: 32
```

如果替换后仍有匹配但文本没有前进，或状态回到已见过的文本/变量组合，工作台会报错并丢弃整批暂存结果。

### 命名捕获变量

PCRE2 支持 `(?<name>...)` 和 `(?P<name>...)` 命名捕获。启用捕获存储后，后续匹配或规则可以通过 `${var:name}` 引用值。例如：

```text
规则 1
Find regex: <h1>(?<chapter>[^<]+)</h1>
Capture variables only: on
Capture variables: chapter

规则 2
Find regex: <title>[^<]*</title>
Replacement: <title>${var:chapter}</title>
Expand ${var:name} in replacement: on
```

变量作用域：

- `Resource`：每个 bookpath 有独立变量帧。
- `Batch`：同一次运行内跨资源、跨规则共享。
- `Session`：同一次工作台对话框打开期间，多次成功 Apply 之间保留；关闭工作台即清除。

写入策略：`Last value wins` 覆盖旧值，`Keep first value` 保留首次值，`Append values` 保存全部值；变量展开读取最后一个值。

仅捕获规则必须启用“存储所有命名捕获组”或填写“捕获变量”允许列表。它的匹配会出现在 Dry Run 明细和匹配总数中，但替换数、变更资源数均保持为零。Apply 可以发布捕获到的工作台变量；若整批没有文本变化，则不会创建无意义的恢复检查点。

## 作用域、结果和导航

可选择当前文件、Book Browser 中选中的文本文件、全部 XHTML、全部 CSS 或全部文本资源。工作台只在 GUI 线程抓取资源快照和最终提交；实际正则计算只接触内存中的 `QString`。

Dry Run 表逐条显示规则、文件、行号、替换前后片段、递归轮次和发生变化的变量名。双击结果会打开对应资源、跳到快照中的原文位置，并在坐标仍可精确映射时选中匹配范围。若某次匹配依赖前一轮生成的文本，工作台会退化为跳到最接近的原文行而不误选文本。Apply 成功后的结果使用最终文档坐标；被后续规则覆盖的范围同样只定位到邻近位置。

明细行和片段有硬上限，超过后表格会提示省略行数，但总匹配数和总替换数仍保持准确。

## Recipe 文件

Recipe 使用版本化 JSON 格式，默认目录为 Sigil 首选项目录下的 `regex_workbench/`。工作台支持新建、打开、原子保存，以及从 Search Editor Plus 导入保存的搜索。

导入规则：

- 带 `PS` 控制码和 `prefind` 的搜索映射为 `PreSearch range`。
- 递归、变量展开和零长度匹配默认关闭。
- Python `\F<function>` 替换不受 V1 支持，导入会失败并给出提示。
- Recipe 严格检查版本、未知字段、重复规则 ID、大小与规则数量上限。

## 撤销、恢复与失败语义

- Dry Run 永远不写书。
- Apply 会在确认后重新 snapshot、内存 stage、校验 XHTML/XML，再创建 Checkpoint。
- 提交前后若资源与 snapshot 不一致，整批拒绝写入。
- 每个有变化的文件只写入一次，并形成各自的一步 Undo；跨文件恢复整批请签出本次恢复检查点。
- 正则编译、变量展开、递归安全限制、取消、XML 校验或 Checkpoint 任一失败，均不发布文本或变量。
- Recipe、正则、变量、暂存校验和提交链路的应用级错误会随界面语言翻译；PCRE2、Qt 或操作系统返回的底层诊断原文会保留在已翻译的错误上下文中，便于检索具体错误。

## Automate

Automate List Editor 提供：

```text
RunRegexWorkbenchRecipe <Recipe 名称或绝对路径>
```

命令按文件名（可省略 `.json`）、Recipe 内的显示名称或绝对路径解析。显示名称重名时会失败，避免执行不确定的方案。Automate 固定作用于当前 EPUB 的全部文本资源，复用同一套 snapshot、内存 stage、XML 校验、Checkpoint 和单次写回链路，不弹出交互式工作台。

## 安全边界

- 主正则、二级正则和替换表达式每条规则只编译一次，并在资源间复用。
- 每次 PCRE2 调用有 match、depth、heap 和匹配数量限制。
- 递归有最大迭代、文本增长、总大小和整批替换数量限制。
- 工作台不支持整条 Python `\F<...>` 函数替换，也不支持条件分支或部分勾选提交。
- 紧急关闭 UI：将 `SettingsStore` 键 `enhanced/regex_workbench_enabled` 设为 `false` 后重启 Sigil；默认值为 `true`。
