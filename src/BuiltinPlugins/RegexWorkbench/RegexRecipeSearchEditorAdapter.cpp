/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "BuiltinPlugins/RegexWorkbench/RegexRecipeSearchEditorAdapter.h"

#include <QCoreApplication>
#include <QSet>
#include <QUuid>

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

bool IsWholeFunctionReplacement(const QString& replacement)
{
    const QString trimmed = replacement.trimmed();
    return trimmed.startsWith(QStringLiteral("\\F<")) &&
           trimmed.endsWith(QLatin1Char('>'));
}

}

RegexRecipeImportResult RegexRecipeSearchEditorAdapter::Import(
    const RegexSearchTemplateEntry& entry)
{
    RegexRecipeImportResult result;
    if (entry.isGroup) {
        result.errorMessage = QCoreApplication::translate(
            "RegexWorkbenchCore", "Search template groups cannot be imported as rules");
        return result;
    }
    if (entry.name.isEmpty() || entry.find.isEmpty()) {
        result.errorMessage = QCoreApplication::translate(
            "RegexWorkbenchCore", "Search template name and find pattern must not be empty");
        return result;
    }
    if (IsWholeFunctionReplacement(entry.replace)) {
        result.errorMessage = QCoreApplication::translate(
            "RegexWorkbenchCore",
            "Python function search templates are not supported by Regex Workbench");
        return result;
    }

    const QStringList tokens = entry.controls.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> tokenSet(tokens.cbegin(), tokens.cend());
    if (tokenSet.contains(QStringLiteral("NL"))) {
        result.errorMessage = QCoreApplication::translate(
            "RegexWorkbenchCore", "Only regular-expression search templates can be imported");
        return result;
    }

    result.rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.rule.name = entry.name;
    result.rule.find = entry.find;
    result.rule.replace = entry.replace;
    if (tokenSet.contains(QStringLiteral("PS"))) {
        if (entry.prefind.isEmpty()) {
            result.warnings.append(QCoreApplication::translate(
                "RegexWorkbenchCore",
                "PreSearch control was ignored because the prefind pattern is empty"));
        } else {
            result.rule.secondaryMode = SecondaryMode::PreSearch;
            result.rule.secondaryPattern = entry.prefind;
        }
    } else if (!entry.prefind.isEmpty()) {
        result.warnings.append(QCoreApplication::translate(
            "RegexWorkbenchCore",
            "Stored prefind pattern was ignored because controls do not contain PS"));
    }

    static const QSet<QString> ignoredScope = {
        QStringLiteral("CF"), QStringLiteral("AH"), QStringLiteral("AC"),
        QStringLiteral("SF"), QStringLiteral("OP"), QStringLiteral("NX")
    };
    static const QSet<QString> ignoredDirection = {
        QStringLiteral("UP"), QStringLiteral("DN")
    };
    QStringList dropped;
    for (const QString& token : tokens) {
        if (ignoredScope.contains(token) || ignoredDirection.contains(token)) {
            dropped.append(token);
        }
    }
    dropped.removeDuplicates();
    if (!dropped.isEmpty()) {
        result.warnings.append(
            QCoreApplication::translate(
                "RegexWorkbenchCore",
                "Per-entry scope or direction controls were not imported: %1")
                .arg(dropped.join(QLatin1Char(' '))));
    }

    // All advanced behavior remains opt-in for imported legacy templates.
    result.rule.recursive = false;
    result.rule.allowEmpty = false;
    result.rule.captureOnly = false;
    result.rule.variableExpansionEnabled = false;
    result.rule.autoIngestNamedCaptures = false;
    result.rule.captureToVar.clear();
    result.rule.enabled = true;
    result.success = true;
    return result;
}

RegexRecipeImportResult RegexRecipeSearchEditorAdapter::Import(
    const SearchEditorModelPlus::searchEntry& entry)
{
    RegexSearchTemplateEntry value;
    value.isGroup = entry.is_group;
    value.fullName = entry.fullname;
    value.name = entry.name;
    value.prefind = entry.prefind;
    value.find = entry.find;
    value.replace = entry.replace;
    value.controls = entry.controls;
    return Import(value);
}

}
}
