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
#ifndef VERTICALCSSTRANSFORMER_H
#define VERTICALCSSTRANSFORMER_H

#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

/**
 * 竖排转横排的 CSS / XHTML / OPF 纯变换器。
 *
 * 所有入口都是纯函数（输入文本，输出文本 + 结构化结论），不依赖 Book，
 * 便于单元测试。Converter 负责在 Book 上编排这些变换与 Checkpoint。
 *
 * 覆盖 PRD 第 7、8、9、11 节：兼容覆盖注入、.vrtl -> .hltr 类切换、
 * inline style 改写、CSS vertical-only 属性中和、OPF page progression。
 */
class VerticalCssTransformer
{
public:
    enum class ConversionMode {
        CompatibilityOverride,
        ProfileAwareRewrite
    };

    // 转换方向：竖排→横排（PRD 主线）或横排→竖排（复用同一 Layout Axis Transformer）
    enum class ConversionDirection {
        VerticalToHorizontal,
        HorizontalToVertical
    };

    struct Options {
        ConversionMode mode = ConversionMode::CompatibilityOverride;
        ConversionDirection direction = ConversionDirection::VerticalToHorizontal;
        bool updatePageProgression = true;
        bool preserveHorizontalSubflows = true;
        bool neutralizeVerticalTextProperties = true;
        bool autoSelectHltrClass = true;
        bool reportVerticalUnicodeForms = true;
        bool validateAfterConversion = true;
        bool dryRun = true;
        QStringList selectedBookPaths;
    };

    struct TransformResult {
        bool ok = true;
        bool changed = false;
        QString text;
        QStringList messages;
    };

    // 兼容覆盖样式表内容（按方向生成；overrideClass 为空时用默认类名）
    static QString buildOverrideCss(
        ConversionDirection direction = ConversionDirection::VerticalToHorizontal,
        const QString& overrideClass = QString());

    // 组合式 XHTML 变换：按 mode/direction 注入 override 或切换 .hltr/.vrtl、改写 inline writing-mode
    static TransformResult transformXhtml(const QString& source,
                                          const Options& options,
                                          bool switchToTargetClass = false);

    // 在 <html> 上追加 override class（幂等）
    static TransformResult addRootOverrideClass(const QString& source,
                                                const QString& overrideClass = QString());

    // 在 <head> 末尾注入 <style>（内容为 overrideCss，幂等）
    static TransformResult injectOverrideStyle(const QString& source, const QString& cssText);

    // 切换 html/body 上的成对 layout class：V2H 把 vrtl 换成 hltr；H2V 反向
    static TransformResult switchLayoutClass(const QString& source,
                                             ConversionDirection direction);

    // 把元素级 inline style 中源方向 writing-mode 改写为目标方向值
    static TransformResult transformInlineWritingMode(
        const QString& source,
        ConversionDirection direction = ConversionDirection::VerticalToHorizontal);

    // 结构化改写单个 CSS 缓冲区：
    //   源方向 writing-mode 值 → 目标方向；V2H 时移除 text-orientation / text-combine-*，
    //   从 font-feature-settings 中移除 vert/vrt2 但保留其它 feature。
    static TransformResult transformCss(const QString& css, const Options& options);

    // OPF spine page-progression-direction -> toLtr（缺省时补写）
    static TransformResult transformOpfProgression(const QString& opf, bool toLtr);
};

}

#endif // VERTICALCSSTRANSFORMER_H
