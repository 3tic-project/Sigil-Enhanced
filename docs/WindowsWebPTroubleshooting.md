# Windows 上 WebP 打不开时怎么查

Sigil 双击图片走的是 `AdjustImage` + Qt `QImage`，**不是** Preview 里的 Chromium。  
所以会出现：macOS 预览/浏览都正常，Windows 一打开 `OEBPS/Images/*.webp` 却弹出：

```text
C:/Users/.../sigil/workspace/Sigil-xxxxx/OEBPS/Images/line1.webp を読み込めません。
```

这就是 `Adjust Image` / `Cannot load %1.` 的日文翻译。路径是解压后的工作区，说明 EPUB 已经解开，失败发生在 **Qt 读图**。

`logo.webp` / `line1.webp` 本身是合法有损 WebP（VP8X + 透明 ALPH + VP8）。  
`logo.webp` 由 Photoshop CC 2014（Windows）导出，带 42KB XMP；`line1.webp` 末尾还有 Photoshop 私有 `PSAI`/`8BIM` 块。`dwebp` 和 macOS 的 `libqwebp` 都能解；Windows 上最常见的是下面两个原因叠在一起：

1. 安装目录里没有 `imageformats\qwebp.dll`（`windeployqt` 漏拷，或 Qt 编的时候没开 WebP）。
2. 有插件，但该版本的 Qt WebP 对 VP8X+alpha 或未知 RIFF 块更挑剔。

## 先确认两件事

1. **是图片标签页报错，还是 Preview 里也裂图？**
   - 只有双击图片报错、Preview 正常：几乎一定是 Windows 包里的 Qt **qwebp.dll** 没带上，或插件加载失败。
   - Preview 也裂：再查文件本身、工作区是否拷残、或 WebEngine 版本。
2. **工作区里的文件是不是完整的？**  
   资源管理器打开上面那个 `workspace\Sigil-...\OEBPS\Images\`，对比 `line1.webp` / `logo.webp` 字节数是否和导入前一致（本机样本分别是 14864 和 479802）。

## 在 Windows 上逐步查

### 1. 安装目录里有没有 WebP 插件

PowerShell：

```powershell
$root = (Get-Process Sigil -ErrorAction SilentlyContinue | Select-Object -First 1).Path
if (-not $root) { $root = "${env:ProgramFiles}\Sigil\Sigil.exe" }
$dir = Split-Path $root
Get-ChildItem "$dir\imageformats\q*.dll" | Format-Table Name, Length
```

应能看到：

```text
imageformats\qwebp.dll
```

没有的话，`QImage` 不认 `.webp`。macOS 包会带 `PlugIns/imageformats/libqwebp.dylib`，Windows 依赖 `windeployqt` 扫动态插件；主程序并不链接 `qwebp.dll`，漏拷就会只在 Windows 炸。

自己编的 Qt 还要确认配置里有 WebP：

```text
<qt>\plugins\imageformats\qwebp.dll
```

官方 aqt 装的 MSVC Qt 一般有这个文件。自己 `-skip qtimageformats` 或 `-no-webp` 编出来的 Qt 没有。

有 dll 但仍加载失败时，用插件调试日志看缺不缺依赖：

```bat
set QT_DEBUG_PLUGINS=1
"C:\Program Files\Sigil\Sigil.exe"
```

控制台里搜 `qwebp`：`QFactoryLoader` 找不到文件，或 `cannot load library` / 缺 `libwebp*.dll`，都是插件问题。

### 2. 用 Sigil 自己的报错看插件列表

新版本失败对话框会带上：

- Qt 读图错误原文
- 是否提示 `The Qt WebP plugin is not available.`
- `Supported formats:` 列表

若列表里没有 `webp`，就是插件没加载，不是书坏了。

### 3. 看文件是不是 Photoshop 的扩展 WebP

这两个样本是：

| 文件 | 格式 | 额外块 |
|---|---|---|
| `logo.webp` | VP8X + 透明 ALPH + 有损 VP8 + 42KB XMP | Photoshop CC 2014 (Windows)，源是 PNG |
| `line1.webp` | VP8X + 透明 ALPH + 有损 VP8 | 未知 `PSAI` 块（`8BIM` 图像资源；webpinfo 会警告） |

用 [libwebp](https://developers.google.com/speed/webp/download) 的工具：

```bat
webpinfo line1.webp
dwebp line1.webp -o line1.png
webpinfo logo.webp
dwebp logo.webp -o logo.png
```

`dwebp` 能解、`QImage` 不能：优先查 qwebp 版本/是否存在。  
`dwebp` 也不能解：文件本身坏了。

可先转成 PNG 再放进 EPUB 做对照：

```bat
dwebp line1.webp -o line1.png
```

PNG 能开、WebP 不能，就是解码器问题。

### 4. 确认不是路径或权限

工作区路径里有日文/中文时，用资源管理器直接双击该 webp：

- 资源管理器能开、Sigil 不能：仍是 Sigil/Qt 插件。
- 资源管理器也不能开：系统解码器也吃不消（少见），用 `dwebp` 验证。

不要改用 `\` / `/` 手工猜路径；Qt 对 `C:/Users/...` 是认的。这两张样本的工作区路径也全是 ASCII，不是编码问题。

## 和 Preview 的差别

| 界面 | 解码器 |
|---|---|
| Preview / Image 用 WebEngine 画的 `<img>` | Chromium 自带 libwebp |
| 双击图片打开的调整窗口、Book Browser 悬停预览 | Qt `qwebp` 插件 |

所以“Preview 看得到、双击报 Cannot load”是预期中的分裂，不是 EPUB 路径写错。

## 本仓库已做的加固

- 读图统一走 `LoadRasterImage`：先普通 `QImage`，失败再丢掉 `PSAI` 这类非标准块重试。
- 失败时写出是否缺少 WebP 插件，以及当前 Qt 认的格式列表。
- Windows 打包在 `windeployqt` 之后再强制拷一次 `qwebp.dll`。
