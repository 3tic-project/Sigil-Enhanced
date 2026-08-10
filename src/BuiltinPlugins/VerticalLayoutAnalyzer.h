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
#ifndef VERTICALLAYOUTANALYZER_H
#define VERTICALLAYOUTANALYZER_H

#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

/**
 * 竖排 (vertical-rl) 布局分析器。
 *
 * 本类只做“分析”，不做任何写回。所有入口都是纯函数（输入文本，
 * 输出结构化结论），便于单元测试，也让 UI/Converter 层与解析逻辑解耦。
 *
 * 覆盖 PRD 第 6 节的 Book-level / Document-level 检测、PageKind 分类
 * 与风险评分，以及第 8 节 CSS 排版轴检测所需的基础信息。
 */
class VerticalLayoutAnalyzer
{
public:
    enum class WritingMode {
        Unknown,
        Horizontal,
        Vertical,
        Mixed
    };

    enum class PageKind {
        ReflowVerticalSafe,
        ReflowVerticalReview,
        AlreadyHorizontal,
        MixedWritingMode,
        TocOrNav,
        TitleOrColophon,
        ImageOnly,
        FixedLayout,
        SvgTextLayout,
        ScriptDriven,
        ParseError
    };

    struct CssAnalysis {
        bool ok = false;
        int parseErrorCount = 0;

        // writing-mode 相关
        bool hasVerticalWritingMode = false;
        bool hasHorizontalWritingMode = false;
        int verticalWritingModeCount = 0;

        // 典型 class
        bool hasVrtlClass = false;
        bool hasHltrClass = false;
        bool hasPairedVrtlHltr = false;
        bool hasVerticalClass = false;

        // 纵排专属特性
        bool hasTcy = false;
        bool hasUpright = false;
        bool hasTextCombine = false;
        bool hasTextOrientation = false;
        bool hasVertFeature = false;   // font-feature-settings: "vert"/"vrt2"

        // 风险信号
        bool hasAbsolutePositioning = false;
        bool hasTransformRotate = false;
        bool hasFixedViewport = false;
        int physicalSideUtilityCount = 0; // .start-* / .end-* 之类物理边 utility
        QStringList reasons;
        int riskScore = 0;
    };

    struct XhtmlAnalysis {
        bool ok = false;
        PageKind pageKind = PageKind::ParseError;
        int riskScore = 0;

        bool hasVerticalWritingMode = false;
        bool hasHorizontalWritingMode = false;
        bool htmlHasVrtlClass = false;
        bool htmlHasHltrClass = false;
        bool bodyHasVrtlClass = false;
        bool bodyHasHltrClass = false;
        bool hasV2hOverrideClass = false;
        bool hasH2vOverrideClass = false;
        bool hasInlineVerticalStyle = false;

        bool hasRuby = false;
        int rubyCount = 0;
        bool hasTable = false;
        int tableCount = 0;
        bool hasSvg = false;
        bool hasSvgText = false;
        bool hasImage = false;
        int imageCount = 0;
        bool hasScript = false;
        int scriptCount = 0;
        int absolutePositionCount = 0;
        int transformRotateCount = 0;
        int visibleTextLength = 0;
        bool fixedViewport = false;
        bool isNavDocument = false;

        QStringList linkedStylesheets;
        QStringList reasons;
    };

    struct OpfAnalysis {
        bool ok = false;
        QString epubVersion;
        QStringList languages;
        QString pageProgression;        // rtl / ltr / default / absent
        QString renditionLayout;        // reflowable / pre-paginated / absent
        int spineItemCount = 0;
        int verticalCandidateCount = 0;
        int fixedLayoutCount = 0;
        int imageOnlyCount = 0;
        QStringList reasons;
    };

    static CssAnalysis analyzeCss(const QString& css);
    static XhtmlAnalysis analyzeXhtml(const QString& source);
    static OpfAnalysis analyzeOpf(const QString& opfSource);

    // 合并 CSS 与 XHTML 信号，产出单页综合风险分（0~100），见 PRD 6.4。
    // profileHighConfidence 为 true 时（DPFJ/EBPAJ 高置信度）会应用 -20 折扣。
    static int combinedRiskScore(const CssAnalysis& css,
                                 const XhtmlAnalysis& xhtml,
                                 bool profileHighConfidence);

    // Resolve the effective page direction. Root conversion/layout classes
    // take precedence over stylesheet-wide signals so a shared DPFJ sheet
    // containing both .vrtl and .hltr does not mark every page as vertical.
    static WritingMode effectiveWritingMode(const CssAnalysis& css,
                                            const XhtmlAnalysis& xhtml);

    static QString pageKindName(PageKind pageKind);
    static QString riskLevelName(int riskScore);
};

}

#endif // VERTICALLAYOUTANALYZER_H
