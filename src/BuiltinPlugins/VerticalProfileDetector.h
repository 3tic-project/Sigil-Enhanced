/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once
#ifndef VERTICALPROFILEDETECTOR_H
#define VERTICALPROFILEDETECTOR_H

#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

/**
 * 竖排 EPUB 制作模板识别。
 *
 * 输入：样式表基名、样式表源码、生成器 metadata；输出一个带置信度的
 * profile 结论。核心逻辑是纯函数，便于单元测试。
 *
 * 覆盖 PRD 第 6.3 节 DPFJ/EBPAJ、AozoraEpub3 与 Generic 三类 profile。
 */
class VerticalProfileDetector
{
public:
    struct Detection {
        QString profileName;          // "DPFJ/EBPAJ" / "AozoraEpub3" / "Generic" / ""
        double confidence = 0.0;      // 0.0 ~ 1.0
        bool pairedHltr = false;      // .vrtl 与 .hltr 成对存在
        bool canSwitchHltr = false;   // 可安全切换 html/body 的 .vrtl -> .hltr
        QStringList reasons;
    };

    static Detection detect(const QStringList& stylesheetNames,
                            const QStringList& cssTexts,
                            const QStringList& generatorMetadata);

    // 已识别 vertical writing-mode 时返回 true（无论是否命中已知模板）
    static bool hasVerticalSignals(const QStringList& cssTexts);
};

}

#endif // VERTICALPROFILEDETECTOR_H
