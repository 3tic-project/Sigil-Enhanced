/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Misc/StagedTextValidator.h"

#include <QCoreApplication>
#include <QXmlStreamReader>

namespace SearchBatch
{

namespace
{

QString NormalizedMediaType(const QString& mediaType)
{
    return mediaType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

void AppendIssue(StagedValidationResult& result,
                 const StagedValidationIssue& issue,
                 int maximum)
{
    ++result.issueCount;
    if (result.issues.size() < maximum) {
        result.issues.append(issue);
    } else {
        result.issuesTruncated = true;
    }
}

}

bool StagedTextValidator::RequiresXmlValidation(const QString& mediaType)
{
    const QString normalized = NormalizedMediaType(mediaType);
    return normalized == QStringLiteral("application/xml") ||
           normalized == QStringLiteral("text/xml") ||
           normalized.endsWith(QStringLiteral("+xml"));
}

StagedValidationResult StagedTextValidator::Validate(
    const QHash<QString, QString>& changedTexts,
    const QHash<QString, QString>& mediaTypes,
    StagedValidationOptions options)
{
    StagedValidationResult result;
    if (options.maxIssues <= 0 || options.entityExpansionLimit <= 0) {
        result.error = QCoreApplication::translate(
            "RegexWorkbenchCore", "Invalid staged text validation limits");
        return result;
    }

    QStringList paths = changedTexts.keys();
    paths.sort();
    for (const QString& path : paths) {
        if (options.isCancelled && options.isCancelled()) {
            result.cancelled = true;
            result.error = QCoreApplication::translate(
                "RegexWorkbenchCore", "Staged text validation was cancelled");
            return result;
        }
        if (!mediaTypes.contains(path)) {
            StagedValidationIssue issue;
            issue.resourcePath = path;
            issue.message = QCoreApplication::translate(
                "RegexWorkbenchCore", "Missing media type for staged resource");
            AppendIssue(result, issue, options.maxIssues);
            continue;
        }
        if (!RequiresXmlValidation(mediaTypes.value(path))) {
            continue;
        }

        ++result.validatedResourceCount;
        QXmlStreamReader reader(changedTexts.value(path));
        reader.setEntityExpansionLimit(options.entityExpansionLimit);
        while (!reader.atEnd()) {
            reader.readNext();
        }
        if (reader.hasError()) {
            StagedValidationIssue issue;
            issue.resourcePath = path;
            issue.line = reader.lineNumber();
            issue.column = reader.columnNumber();
            issue.message = reader.errorString();
            AppendIssue(result, issue, options.maxIssues);
        }
    }

    if (result.issueCount > 0) {
        const StagedValidationIssue& first = result.issues.first();
        result.error = QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Staged XML is not well formed: %1 at %2:%3: %4")
                           .arg(first.resourcePath)
                           .arg(first.line)
                           .arg(first.column)
                           .arg(first.message);
        return result;
    }
    result.success = true;
    return result;
}

}
