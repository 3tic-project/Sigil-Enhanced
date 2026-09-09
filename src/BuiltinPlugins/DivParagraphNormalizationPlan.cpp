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

namespace
{

void addFramed(QCryptographicHash& hash, const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    hash.addData(QByteArray::number(bytes.size()));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(bytes);
}

}

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
        hash.addData(stylesheet.available ? QByteArrayLiteral(":1")
                                          : QByteArrayLiteral(":0"));
    }
    return QString::fromLatin1(hash.result().toHex());
}

DivParagraphNormalizationPlan::Result
DivParagraphNormalizationPlan::build(
    const QVector<Input>& inputs,
    const BookLiveParagraphNormalizer::Options& options,
    const ProgressFunction& progress)
{
    Result plan;
    plan.presetId = options.presetId();

    for (int input_index = 0; input_index < inputs.count(); ++input_index) {
        if (progress && !progress(input_index, inputs.count())) {
            plan.cancelled = true;
            plan.errors << QStringLiteral("normalization plan generation was cancelled");
            plan.ok = false;
            refreshIdentity(plan);
            return plan;
        }
        const Input& input = inputs.at(input_index);
        Entry entry;
        entry.resourceId = input.resourceId;
        entry.baseRevision = input.baseRevision;
        entry.source = input.text;
        entry.beforeHash = hashText(input.text);
        entry.cssHash = hashStylesheets(input.stylesheets);
        const BookLiveParagraphNormalizer::NormalizeResult normalized =
            BookLiveParagraphNormalizer::normalizeXhtmlText(
                input.text, options, input.stylesheets);
        entry.analysis = normalized.before;
        if (plan.ruleVersion.isEmpty()) {
            plan.ruleVersion = entry.analysis.ruleVersion;
        }

        if (!entry.analysis.ok) {
            entry.status = Status::Error;
            entry.messages << entry.analysis.message;
            plan.errorFiles++;
        } else if (entry.analysis.safeToNormalize) {
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

        plan.entries << entry;
    }

    if (progress && !progress(inputs.count(), inputs.count())) {
        plan.cancelled = true;
        plan.errors << QStringLiteral("normalization plan generation was cancelled");
        plan.ok = false;
        refreshIdentity(plan);
        return plan;
    }
    plan.ok = plan.errors.isEmpty();
    refreshIdentity(plan);
    return plan;
}

void DivParagraphNormalizationPlan::refreshIdentity(Result& plan)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addFramed(hash, plan.ruleVersion);
    addFramed(hash, plan.presetId);
    for (const Entry& entry : plan.entries) {
        addFramed(hash, entry.resourceId);
        addFramed(hash, entry.baseRevision);
        addFramed(hash, entry.beforeHash);
        addFramed(hash, entry.cssHash);
        addFramed(hash, entry.afterHash);
        addFramed(hash, QString::number(static_cast<int>(entry.status)));
    }
    plan.planId = QString::fromLatin1(hash.result().toHex());
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
