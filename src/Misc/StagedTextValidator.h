/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef STAGED_TEXT_VALIDATOR_H
#define STAGED_TEXT_VALIDATOR_H

#include <functional>

#include <QHash>
#include <QList>
#include <QString>

namespace SearchBatch
{

struct StagedValidationIssue
{
    QString resourcePath;
    qint64 line = -1;
    qint64 column = -1;
    QString message;
};

struct StagedValidationResult
{
    bool success = false;
    bool cancelled = false;
    int validatedResourceCount = 0;
    qint64 issueCount = 0;
    QList<StagedValidationIssue> issues;
    bool issuesTruncated = false;
    QString error;
};

struct StagedValidationOptions
{
    int maxIssues = 100;
    int entityExpansionLimit = 1024;
    std::function<bool()> isCancelled;
};

class StagedTextValidator final
{
public:
    static StagedValidationResult Validate(
        const QHash<QString, QString>& changedTexts,
        const QHash<QString, QString>& mediaTypes,
        StagedValidationOptions options = StagedValidationOptions());

    static bool RequiresXmlValidation(const QString& mediaType);
};

}

#endif // STAGED_TEXT_VALIDATOR_H
