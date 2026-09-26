#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace OPFSourcePatch {

// Projects the package fields the legacy parser understands. The result is a
// model, not a file that may be saved in place of the original source.
QString ModelXml(const QString &source);

// Applies only the semantic difference between two models to source.
// A failed edit throws ErrorParsingXml and produces no partial result.
QString ApplyModelUpdate(const QString &source, const QString &beforeModel, const QString &afterModel);

// Adds one navigation manifest item without changing spine or reading order.
QString AddNavigationManifest(const QString &source, const QString &href, const QString &identifier);

// Replaces manifest ids and the references listed by the legacy rebasing
// routine. Every other byte of source is left unchanged.
QString MapIdentifiers(const QString &source, const QHash<QString, QString> &changedIds);

// Applies a plugin "metadata", "manifest" or "spine" plan through ApplyModelUpdate.
// Mirrors the legacy opf_package_update.apply_update, including its validation.
QString ApplyPackageUpdate(const QString &source, const QString &operation, const QJsonObject &payload);

}
