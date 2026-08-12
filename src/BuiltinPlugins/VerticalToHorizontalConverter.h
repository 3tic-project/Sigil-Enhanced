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
#ifndef VERTICALTOHORIZONTALCONVERTER_H
#define VERTICALTOHORIZONTALCONVERTER_H

#include <QList>
#include <QString>
#include <QStringList>

#include "BuiltinPlugins/VerticalCssTransformer.h"
#include "BuiltinPlugins/VerticalLayoutAnalyzer.h"
#include "Misc/ValidationResult.h"

class Book;
class TextResource;

namespace BuiltinPlugins
{

/**
 * 竖排转横排整书编排器。
 *
 * 负责在 Book 上：整书分析（Book/Document 级检测 + profile 判定 + 风险评分）、
 * 转换计划构建、批量写回前的 stale-source 校验与不变量校验，以及写回资源。
 * Checkpoint 由调用方（MainWindow）在调用 convert() 之前创建，与现有
 * 中文转换 / 字体子集化批处理保持一致。
 */
class VerticalToHorizontalConverter
{
public:
    using Options = VerticalCssTransformer::Options;
    using PageKind = VerticalLayoutAnalyzer::PageKind;

    struct FileAnalysis {
        QString bookpath;
        PageKind kind = PageKind::ParseError;
        int riskScore = 0;
        double profileConfidence = 0.0;
        QString profileName;
        QStringList reasons;
        QStringList plannedChanges;
        VerticalLayoutAnalyzer::WritingMode writingMode =
            VerticalLayoutAnalyzer::WritingMode::Unknown;
        bool generatedByV2h = false;
        bool generatedByH2v = false;
        bool generatedByV2hProfile = false;
        bool generatedByH2vProfile = false;
        bool canSwitchLayoutClass = false;
        bool fixedLayoutFromOpf = false;
    };

    struct Analysis {
        bool ok = false;
        QString epubVersion;
        QStringList languages;
        QString detectedWritingMode;      // vertical-rl / horizontal-tb / mixed / unknown
        QString pageProgression;          // rtl / ltr / default / absent
        QString profileName;              // DPFJ/EBPAJ / AozoraEpub3 / Generic / ""
        double profileConfidence = 0.0;
        bool canSwitchHltr = false;
        bool fixedLayoutBook = false;
        bool hasFixedLayoutItems = false;
        bool restoringGeneratedConversion = false;
        QStringList fixedLayoutBookPaths;
        QList<FileAnalysis> files;
        int verticalCount = 0;
        int horizontalCount = 0;
        int safeCount = 0;
        int reviewCount = 0;
        int skippedCount = 0;
        QStringList reasons;
    };

    struct Result {
        bool ok = false;
        bool modified = false;
        bool bookBrowserRefreshRequired = false;
        QStringList changedBookPaths;
        QList<ValidationResult> validationResults;
        Analysis before;
        Analysis after;
    };

    explicit VerticalToHorizontalConverter(Book* book);

    Analysis analyze(const Options& options) const;
    Result convert(const Options& options);

private:
    struct PageContext {
        VerticalLayoutAnalyzer::CssAnalysis css;
        VerticalLayoutAnalyzer::XhtmlAnalysis xhtml;
        PageKind kind = PageKind::ParseError;
        VerticalLayoutAnalyzer::WritingMode writingMode =
            VerticalLayoutAnalyzer::WritingMode::Unknown;
        int riskScore = 0;
        bool vertical = false;
        QStringList reasons;
    };

    PageContext analyzePage(TextResource* resource,
                            const QString& bookpath,
                            const QHash<QString, QString>& cssTextCache,
                            const Analysis& bookLevel,
                            const Options& options) const;
    void classifyPage(PageContext& context, const Analysis& bookLevel,
                      const QString& bookpath) const;
    QStringList linkedStylesheetBookPaths(const QString& xhtmlSource,
                                          const QString& bookpath,
                                          const QHash<QString, QString>& cssByPath) const;
    QStringList generatorMetadata(const QString& opfSource) const;

    void addResult(Result& result, ValidationResult::ResType type,
                   const QString& bookpath, const QString& message) const;
    void addResult(Result& result, ValidationResult::ResType type,
                   const QString& bookpath, int line, int charoffset,
                   const QString& message) const;

    Book* m_Book;
};

}

#endif // VERTICALTOHORIZONTALCONVERTER_H
