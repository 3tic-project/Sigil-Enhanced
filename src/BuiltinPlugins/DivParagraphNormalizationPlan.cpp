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

#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"

#include <QCryptographicHash>
#include <QHash>

namespace BuiltinPlugins
{

QString DivParagraphNormalizationPlan::hashText(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString DivParagraphNormalizationPlan::hashStylesheets(
    const QVector<DivParagraphCssAnalyzer::Source>& stylesheets)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const DivParagraphCssAnalyzer::Source& stylesheet : stylesheets) {
        const QByteArray id = stylesheet.id.toUtf8();
        const QByteArray text = stylesheet.text.toUtf8();
        hash.addData(QByteArray::number(id.size()));
        hash.addData(QByteArrayLiteral(":"));
        hash.addData(id);
        hash.addData(QByteArray::number(text.size()));
        hash.addData(QByteArrayLiteral(":"));
        hash.addData(text);
    }
    return QString::fromLatin1(hash.result().toHex());
}

DivParagraphNormalizationPlan::Result
DivParagraphNormalizationPlan::build(
    const QVector<Input>& inputs,
    const BookLiveParagraphNormalizer::Options& options)
{
    Result plan;
    plan.presetId = options.presetId();
    QCryptographicHash plan_hash(QCryptographicHash::Sha256);
    plan_hash.addData(plan.presetId.toUtf8());

    for (const Input& input : inputs) {
        Entry entry;
        entry.resourceId = input.resourceId;
        entry.baseRevision = input.baseRevision;
        entry.beforeHash = hashText(input.text);
        entry.cssHash = hashStylesheets(input.stylesheets);
        entry.analysis = BookLiveParagraphNormalizer::analyzeXhtmlText(
            input.text, options, input.stylesheets);
        if (plan.ruleVersion.isEmpty()) {
            plan.ruleVersion = entry.analysis.ruleVersion;
            plan_hash.addData(plan.ruleVersion.toUtf8());
        }

        if (!entry.analysis.ok) {
            entry.status = Status::Error;
            entry.messages << entry.analysis.message;
            plan.errorFiles++;
        } else if (entry.analysis.safeToNormalize) {
            const BookLiveParagraphNormalizer::NormalizeResult normalized =
                BookLiveParagraphNormalizer::normalizeXhtmlText(
                    input.text, options, input.stylesheets);
            entry.messages = normalized.messages;
            if (!normalized.ok) {
                entry.status = Status::Error;
                plan.errorFiles++;
                plan.errors << QStringLiteral("%1: %2")
                    .arg(input.resourceId, normalized.messages.join(QStringLiteral("; ")));
            } else if (!normalized.changed) {
                entry.status = Status::Skip;
                plan.skippedFiles++;
            } else {
                entry.status = Status::Apply;
                entry.output = normalized.text;
                entry.afterHash = normalized.afterHash;
                plan.applyFiles++;
                plan.conversionCount += entry.analysis.convertibleLeaves;
                plan.protectedCount += entry.analysis.protectedRanges.count();
            }
        } else if (entry.analysis.candidate) {
            entry.status = Status::Review;
            entry.messages << entry.analysis.message;
            plan.reviewFiles++;
        } else {
            entry.status = Status::Skip;
            entry.messages << entry.analysis.message;
            plan.skippedFiles++;
        }

        plan_hash.addData(entry.resourceId.toUtf8());
        plan_hash.addData(entry.baseRevision.toUtf8());
        plan_hash.addData(entry.beforeHash.toUtf8());
        plan_hash.addData(entry.cssHash.toUtf8());
        plan_hash.addData(entry.afterHash.toUtf8());
        plan.entries << entry;
    }

    plan.ok = plan.errors.isEmpty();
    plan.planId = QString::fromLatin1(plan_hash.result().toHex());
    return plan;
}

QStringList DivParagraphNormalizationPlan::revisionConflicts(
    const Result& plan,
    const QVector<Input>& current_inputs)
{
    QHash<QString, Input> current_by_id;
    for (const Input& input : current_inputs) {
        current_by_id.insert(input.resourceId, input);
    }

    QStringList conflicts;
    for (const Entry& entry : plan.entries) {
        if (entry.status != Status::Apply) {
            continue;
        }
        if (!current_by_id.contains(entry.resourceId)) {
            conflicts << QStringLiteral("%1: resource is no longer available")
                             .arg(entry.resourceId);
            continue;
        }
        const Input current = current_by_id.value(entry.resourceId);
        if (current.baseRevision != entry.baseRevision) {
            conflicts << QStringLiteral("%1: resource revision changed")
                             .arg(entry.resourceId);
        } else if (hashText(current.text) != entry.beforeHash) {
            conflicts << QStringLiteral("%1: XHTML content changed")
                             .arg(entry.resourceId);
        } else if (hashStylesheets(current.stylesheets) != entry.cssHash) {
            conflicts << QStringLiteral("%1: stylesheet content changed")
                             .arg(entry.resourceId);
        }
    }
    return conflicts;
}

}
