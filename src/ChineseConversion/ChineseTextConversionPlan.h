/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

#include "ChineseConversion/ChineseConversionTypes.h"

class OpenCCConverter;

enum class ChineseDocumentKind {
    Xhtml,
    Svg
};

enum class ChineseTextSourceKind {
    TextNode,
    Attribute
};

struct ChineseTextChange {
    qsizetype byteStart = -1;
    qsizetype byteLength = 0;
    QString before;
    QString after;
    QString nodePath;
    QString attributeName;
    ChineseTextSourceKind sourceKind = ChineseTextSourceKind::TextNode;
};

class ChineseTextConversionPlan final
{
public:
    static ChineseTextConversionPlan Build(const QString& source,
                                           ChineseDocumentKind documentKind,
                                           const ChineseConversionOptions& options,
                                           const OpenCCConverter& converter);

    bool IsValid() const;
    bool HasChanges() const;
    QString ErrorString() const;
    QStringList Warnings() const;
    QList<ChineseTextChange> Changes() const;
    int SkippedJapaneseSegments() const;
    int SkippedProtectedSegments() const;
    QString Apply(QString *error = nullptr) const;
    QString Apply(const QSet<int>& enabledChanges, QString *error = nullptr) const;

private:
    QString m_Source;
    QByteArray m_Utf8Source;
    QList<ChineseTextChange> m_Changes;
    QStringList m_Warnings;
    QString m_Error;
    int m_SkippedJapaneseSegments = 0;
    int m_SkippedProtectedSegments = 0;
};
