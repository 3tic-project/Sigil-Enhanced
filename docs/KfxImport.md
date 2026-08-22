# KFX 转 EPUB

把没有 DRM 的 Kindle KFX 转成可以继续编辑的 EPUB。

转换核心来自 [kfx2epub](https://github.com/2778995958/kfx2epub)（基于 John Howell 的 KFX Input / kfxlib）。感谢原作者。许可证与第三方说明见 `src/Resource_Files/python3lib/sigil_kfx_import/THIRD_PARTY_NOTICES.md`。

## 怎么用

菜单：**增强 → 将 KFX 转换为 EPUB...**

选一个 `.kfx` 或 `.kfx-zip` 之后：

- **另存为 EPUB**：转完写入你指定的文件，当前窗口里的书不动。
- **在新窗口打开**：转完当作一本还没保存的新书打开。
- **同时规范化 EPUB 结构**：默认不勾。勾上会在交付前整理 OPF 和目录结构。

也可以把 KFX 拖进主窗口或 Book Browser。会先弹出说明（含 DRM 限制），默认是转换并在新窗口打开。KFX 不会被当成图片/字体加进当前这本书。

## 注意

- 只支持**没有 DRM** 的文件。有 DRM 的会直接拒绝，程序也不会去解密。
- 转换在独立进程里跑，可以取消；失败时当前打开的书不受影响。
- 新窗口第一次保存：如果转换后你没再改过，会直接复制那份结果；改过之后才按普通 EPUB 导出。

## 和「没改过就保存」的关系

打开一本 EPUB、什么都没改就保存：原文件不会被重写。另存为 / 保存副本是复制原来的文件。默认也不再写入 Sigil 版本号；EPUB 3 的修改时间只在书真的改过之后才更新。
