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

#include "BuiltinPlugins/VerticalProfileDetector.h"

#include <QRegularExpression>
#include <QSet>

#include "BuiltinPlugins/VerticalLayoutAnalyzer.h"
#include "Parsers/CSSParser.h"

namespace BuiltinPlugins
{

namespace
{

QString baseName(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return (slash >= 0) ? path.mid(slash + 1) : path;
}

bool containsAny(const QString& text, const QStringList& keywords, Qt::CaseSensitivity cs = Qt::CaseInsensitive)
{
    for (const QString& keyword : keywords) {
        if (text.contains(keyword, cs)) {
            return true;
        }
    }
    return false;
}

bool matchesAny(const QString& name, const QRegularExpression& re)
{
    return re.match(name).hasMatch();
}

// 在全部 CSS 文本里扫描特定 class / 属性特征
struct CssFeatures {
    bool hasTcy = false;
    bool hasUprightClass = false;
    bool hasSerifJaV = false;
    bool hasTategaki = false;
    bool hasYokogaki = false;
    bool hasVerticalWriting = false;
    bool hasVrtlClass = false;
    bool hasHltrClass = false;
    bool hasPairedVrtlHltr = false;
    bool hasVerticalClass = false;
    int physicalSideUtilityCount = 0;
};

CssFeatures scanCssFeatures(const QStringList& cssTexts)
{
    CssFeatures features;

    for (const QString& css : cssTexts) {
        if (css.isEmpty()) {
            continue;
        }
        const VerticalLayoutAnalyzer::CssAnalysis analysis = VerticalLayoutAnalyzer::analyzeCss(css);
        features.hasTcy = features.hasTcy || analysis.hasTcy;
        features.hasUprightClass = features.hasUprightClass || analysis.hasUpright;
        features.hasVrtlClass = features.hasVrtlClass || analysis.hasVrtlClass;
        features.hasHltrClass = features.hasHltrClass || analysis.hasHltrClass;
        features.hasVerticalClass = features.hasVerticalClass || analysis.hasVerticalClass;
        features.hasVerticalWriting = features.hasVerticalWriting || analysis.hasVerticalWritingMode;
        features.physicalSideUtilityCount += analysis.physicalSideUtilityCount;

        // font-family / tategaki / yokogaki 需要额外扫描 property value
        CSSParser parser;
        parser.parse_css(css);
        QString current_property;
        while (true) {
            CSSParser::csstoken token = parser.get_next_token();
            if (token.type == TKN_CSS_END) {
                break;
            }
            if (token.type == TKN_PROPERTY) {
                current_property = token.data.trimmed().toLower();
            } else if (token.type == TKN_PROPERTY_VALUE) {
                const QString value = token.data.trimmed().toLower();
                if (current_property == QStringLiteral("font-family") && value.contains(QStringLiteral("serif-ja-v"))) {
                    features.hasSerifJaV = true;
                }
                current_property.clear();
            }
        }
    }
    features.hasPairedVrtlHltr = features.hasVrtlClass && features.hasHltrClass;

    // .tategaki / .yokogaki 类名存在于任何样式表
    const QRegularExpression class_re(QStringLiteral("\\.[A-Za-z_][A-Za-z0-9_-]*"));
    for (const QString& css : cssTexts) {
        CSSParser parser;
        parser.parse_css(css);
        while (true) {
            CSSParser::csstoken token = parser.get_next_token();
            if (token.type == TKN_CSS_END) {
                break;
            }
            if (token.type != TKN_SELECTOR) {
                continue;
            }
            QRegularExpressionMatchIterator matches = class_re.globalMatch(token.data);
            while (matches.hasNext()) {
                const QString class_name = matches.next().captured(0).mid(1).toLower();
                if (class_name == QStringLiteral("tategaki") || class_name == QStringLiteral("tate")) {
                    features.hasTategaki = true;
                } else if (class_name == QStringLiteral("yokogaki") || class_name == QStringLiteral("yoko")) {
                    features.hasYokogaki = true;
                }
            }
        }
    }

    return features;
}

bool stylesheetNameIsDpfj(const QString& base_name)
{
    static const QRegularExpression dpfj_styles(
        QStringLiteral("^(style-standard|style-advance|style-epub3|book-style|common-style|japanese-book)\\.css$"),
        QRegularExpression::CaseInsensitiveOption);
    return matchesAny(base_name, dpfj_styles);
}

bool stylesheetNameIsAozora(const QString& base_name)
{
    return base_name.contains(QStringLiteral("aozora"), Qt::CaseInsensitive);
}

} // namespace

VerticalProfileDetector::Detection VerticalProfileDetector::detect(
    const QStringList& stylesheetNames,
    const QStringList& cssTexts,
    const QStringList& generatorMetadata)
{
    Detection detection;
    const CssFeatures features = scanCssFeatures(cssTexts);

    QStringList metadata_lower;
    for (const QString& metadata : generatorMetadata) {
        metadata_lower.append(metadata.toLower());
    }
    const QString metadata_joined = metadata_lower.join(QStringLiteral(" "));

    // ---------------- DPFJ / EBPAJ ----------------
    int dpfj_score = 0;
    if (features.hasPairedVrtlHltr) {
        dpfj_score += 35;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：.vrtl 与 .hltr 成对规则");
    }
    for (const QString& name : stylesheetNames) {
        if (stylesheetNameIsDpfj(baseName(name))) {
            dpfj_score += 15;
            detection.reasons << QStringLiteral("DPFJ/EBPAJ：样式表 %1").arg(baseName(name));
            break;
        }
    }
    if (features.hasTcy) {
        dpfj_score += 10;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：存在 .tcy 纵中横 class");
    }
    if (features.hasUprightClass) {
        dpfj_score += 10;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：存在 .upright class");
    }
    if (features.hasSerifJaV) {
        dpfj_score += 10;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：font-family 使用 serif-ja-v 别名");
    }
    if (features.physicalSideUtilityCount > 0) {
        dpfj_score += 10;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：存在 %1 个物理方向 utility class").arg(features.physicalSideUtilityCount);
    }
    if (containsAny(metadata_joined, {
                        QStringLiteral("ebipaj"), QStringLiteral("ebidori"),
                        QStringLiteral("dpej"), QStringLiteral("digital publishers federation of japan"),
                        QStringLiteral("epub3j"), QStringLiteral("vivliostyle")})) {
        dpfj_score += 10;
        detection.reasons << QStringLiteral("DPFJ/EBPAJ：生成器 metadata 命中");
    }
    dpfj_score = qMin(dpfj_score, 100);

    // ---------------- AozoraEpub3 ----------------
    int aozora_score = 0;
    if (containsAny(metadata_joined, { QStringLiteral("aozoraepub3") })) {
        aozora_score += 40;
        detection.reasons << QStringLiteral("AozoraEpub3：生成器 metadata 命中");
    }
    for (const QString& name : stylesheetNames) {
        if (stylesheetNameIsAozora(baseName(name))) {
            aozora_score += 15;
            detection.reasons << QStringLiteral("AozoraEpub3：样式表 %1").arg(baseName(name));
            break;
        }
    }
    if (features.hasTategaki) {
        aozora_score += 15;
        detection.reasons << QStringLiteral("AozoraEpub3：存在 .tategaki class");
    }
    if (features.hasYokogaki) {
        aozora_score += 10;
        detection.reasons << QStringLiteral("AozoraEpub3：存在 .yokogaki class");
    }
    if (features.hasVerticalWriting && !features.hasPairedVrtlHltr) {
        aozora_score += 10;
        detection.reasons << QStringLiteral("AozoraEpub3：直接使用 vertical-rl 且无 .hltr 配对");
    }
    aozora_score = qMin(aozora_score, 100);

    // ---------------- 判定 ----------------
    if (dpfj_score >= 50 && dpfj_score >= aozora_score) {
        detection.profileName = QStringLiteral("DPFJ/EBPAJ");
        detection.confidence = qMin(1.0, dpfj_score / 100.0);
    } else if (aozora_score >= 50) {
        detection.profileName = QStringLiteral("AozoraEpub3");
        detection.confidence = qMin(1.0, aozora_score / 100.0);
    } else if (features.hasVerticalWriting) {
        detection.profileName = QStringLiteral("Generic");
        detection.confidence = 0.5;
        detection.reasons << QStringLiteral("Generic：存在纵向排版但未命中已知模板");
    } else {
        detection.profileName = QString();
        detection.confidence = 0.0;
    }

    detection.pairedHltr = features.hasPairedVrtlHltr;
    detection.canSwitchHltr = features.hasPairedVrtlHltr
        && detection.profileName == QStringLiteral("DPFJ/EBPAJ")
        && detection.confidence >= 0.5;
    return detection;
}

bool VerticalProfileDetector::hasVerticalSignals(const QStringList& cssTexts)
{
    for (const QString& css : cssTexts) {
        if (!css.isEmpty()) {
            const VerticalLayoutAnalyzer::CssAnalysis analysis = VerticalLayoutAnalyzer::analyzeCss(css);
            if (analysis.hasVerticalWritingMode) {
                return true;
            }
        }
    }
    return false;
}

} // namespace BuiltinPlugins
