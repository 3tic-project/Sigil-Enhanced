# Sigil MCP EPUB 排版 Skill

`sigil-epub-layout` 是独立的 Codex Skill，用于在用户明确指定时，通过 Sigil MCP 将本地
TXT/XHTML、图片和元数据排版成可重排 EPUB 3。它不会改变 Sigil MCP 的默认提示，也不会在普通
EPUB、CSS 或编辑请求中自动启用。

## 安装与触发

仓库源码位于：

```text
examples/codex_skills/sigil-epub-layout/
```

安装或链接到 `~/.codex/skills/sigil-epub-layout` 后，需要启动新的 Codex 会话更新 Skill 清单。
`agents/openai.yaml` 明确设置：

```yaml
policy:
  allow_implicit_invocation: false
```

因此必须显式写出 Skill 名称，例如：

```text
$sigil-epub-layout 使用 /absolute/source/path 的 TXT 和图片，按 standard-lightnovel-horizontal
排版当前 Sigil 空白书；语言 zh-TW，保存到 /absolute/output/book.epub。
```

未写 `$sigil-epub-layout` 时，Codex 不应加载或执行该流程。

## 内容

| 路径 | 用途 |
| --- | --- |
| `SKILL.md` | 输入要求、执行流程、质量门槛和失败处理。 |
| `references/layout-rules.md` | 从 `todo/test_file/ref` 七本 EPUB 提炼的通用排版规则。 |
| `references/mcp-workflow.md` | Sigil MCP 单事务建书、回滚和保存前验收流程。 |
| `assets/reflowable-horizontal.css` | 最小横排可重排样式。 |
| `assets/*.xhtml` | 章节、整页插图和 EPUB 3 nav 骨架。 |
| `scripts/inspect_source.py` | 清点编码、插图/Ruby/脚注标记和图片尺寸，不输出正文。 |
| `scripts/inspect_epub.py` | 分析参考 EPUB 的包结构和 CSS 特征，不提取正文。 |
| `scripts/validate_epub.py` | 校验 ZIP、OCF、OPF、XML、nav、引用和非空文本。 |

参考 EPUB 只作为分析输入，不打包进 Skill，也不会复制其正文、图片、字体、伪 ISBN、厂商扩展
或 JavaScript。默认模板不内嵌字体，不使用脚本，不启用 `duokan-*`、`zy-*` 等私有语义。

## 执行边界

- 只操作当前打开的 Sigil Book，并先读取实际 package、资源和 revision。
- 新书采用一个事务：先暂存全部文本/图片，再更新 metadata 和最终 spine；宿主会把此前暂存的
  manifested 新资源合并进 staged OPF。
- 每次事务都必须先 preview、validate；commit 直接应用，不再显示 Sigil 确认对话框。
- commit 后立即批量或分段回读新增文本，拒绝空文件、截断或生成内容不一致。
- 长 XHTML/CSS 使用独立的 range/chunk MCP 工具：UTF-16 offset 只用于读取，写入 offset
  必须按 UTF-8 字节累计；单文档不得超过 64 MiB。
- 单个 `add_binary_resource` 解码后最大 5 MiB，超限时停止并报告，不静默压缩。
- 当前公共 MCP 工具不提供 Save As；由用户保存到新的 EPUB 路径，再运行磁盘验收。
- bundled validator 不是 EPUBCheck 的完整替代；未实际运行 EPUBCheck 时必须明确说明。

## 本地检查

```sh
python3 ~/.codex/skills/sigil-epub-layout/scripts/inspect_source.py /absolute/source --pretty
python3 ~/.codex/skills/sigil-epub-layout/scripts/inspect_epub.py /absolute/reference.epub --pretty
python3 ~/.codex/skills/sigil-epub-layout/scripts/validate_epub.py /absolute/output.epub --strict-layout
```

Skill 结构验证：

```sh
python3 ~/.codex/skills/.system/skill-creator/scripts/quick_validate.py \
  examples/codex_skills/sigil-epub-layout
```
