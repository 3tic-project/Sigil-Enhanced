/************************************************************************
**
**  Copyright (C) 2026  Sigil-Enhanced contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/SearchBatchRunner.h"

namespace SearchBatch
{

Result Runner::Run(const QList<Rule>& rules,
                   const QHash<QString, QString>& originalTexts,
                   const ApplyFunction& apply,
                   const CancelFunction& isCancelled)
{
    Result result;
    if (!apply) {
        result.error = QStringLiteral("Search batch has no replacement engine.");
        return result;
    }

    QHash<QString, QString> workingTexts = originalTexts;

    for (const Rule& rule : rules) {
        RuleResult ruleResult;
        ruleResult.id = rule.id;
        ruleResult.name = rule.name;

        for (const QString& resourcePath : rule.resourcePaths) {
            if (isCancelled && isCancelled()) {
                result.cancelled = true;
                result.error = QStringLiteral("Search batch was cancelled.");
                result.rules.append(ruleResult);
                return result;
            }

            const auto working = workingTexts.constFind(resourcePath);
            if (working == workingTexts.constEnd()) {
                result.error = QStringLiteral("Search batch target is missing: %1").arg(resourcePath);
                result.rules.append(ruleResult);
                return result;
            }

            const QString currentText = working.value();
            const ApplyResult applyResult = apply(rule, resourcePath, currentText);
            if (!applyResult.ok) {
                result.error = applyResult.error.isEmpty() ?
                    QStringLiteral("Search rule failed for %1: %2").arg(rule.name, resourcePath) :
                    applyResult.error;
                result.rules.append(ruleResult);
                return result;
            }

            ruleResult.replacementCount += applyResult.replacementCount;
            result.replacementCount += applyResult.replacementCount;
            if (applyResult.replacementCount > 0) {
                ++ruleResult.matchedResourceCount;
            }
            if (applyResult.text != currentText) {
                workingTexts.insert(resourcePath, applyResult.text);
                ++ruleResult.changedResourceCount;
            }
        }
        result.rules.append(ruleResult);
    }

    for (auto it = originalTexts.constBegin(); it != originalTexts.constEnd(); ++it) {
        const auto working = workingTexts.constFind(it.key());
        if (working != workingTexts.constEnd() && working.value() != it.value()) {
            result.changedTexts.insert(it.key(), working.value());
        }
    }

    result.success = true;
    return result;
}

}
