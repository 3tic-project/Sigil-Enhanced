/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/RegexWorkbench/RegexRecipeStore.h"

#include <cmath>
#include <limits>
#include <utility>

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include "Misc/Utility.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

const QString RecipeFormat = QStringLiteral("sigil.regexWorkbench.recipe");

void SetError(QString* error, const QString& message)
{
    if (error != nullptr) {
        *error = message;
    }
}

bool ValidLimits(const RegexRecipeLimits& limits)
{
    return limits.maxFileBytes > 0 && limits.maxRules > 0 &&
           limits.maxIdCodeUnits > 0 && limits.maxNameCodeUnits > 0 &&
           limits.maxPatternCodeUnits > 0 && limits.maxCaptureNamesPerRule > 0 &&
           limits.maxIterations > 0;
}

bool IsWholeFunctionReplacement(const QString& replacement)
{
    const QString trimmed = replacement.trimmed();
    return trimmed.startsWith(QStringLiteral("\\F<")) &&
           trimmed.endsWith(QLatin1Char('>'));
}

QString SecondaryModeName(SecondaryMode mode)
{
    switch (mode) {
        case SecondaryMode::None:
            return QStringLiteral("None");
        case SecondaryMode::PreSearch:
            return QStringLiteral("PreSearch");
        case SecondaryMode::FilterAccept:
            return QStringLiteral("FilterAccept");
        case SecondaryMode::FilterReject:
            return QStringLiteral("FilterReject");
    }
    return QString();
}

bool ParseSecondaryMode(const QString& value, SecondaryMode& mode)
{
    if (value == QStringLiteral("None")) {
        mode = SecondaryMode::None;
    } else if (value == QStringLiteral("PreSearch")) {
        mode = SecondaryMode::PreSearch;
    } else if (value == QStringLiteral("FilterAccept")) {
        mode = SecondaryMode::FilterAccept;
    } else if (value == QStringLiteral("FilterReject")) {
        mode = SecondaryMode::FilterReject;
    } else {
        return false;
    }
    return true;
}

QString VariableScopeName(VariableScope scope)
{
    switch (scope) {
        case VariableScope::Resource:
            return QStringLiteral("Resource");
        case VariableScope::Batch:
            return QStringLiteral("Batch");
        case VariableScope::Session:
            return QStringLiteral("Session");
    }
    return QString();
}

bool ParseVariableScope(const QString& value, VariableScope& scope)
{
    if (value == QStringLiteral("Resource")) {
        scope = VariableScope::Resource;
    } else if (value == QStringLiteral("Batch")) {
        scope = VariableScope::Batch;
    } else if (value == QStringLiteral("Session")) {
        scope = VariableScope::Session;
    } else {
        return false;
    }
    return true;
}

QString WritePolicyName(WritePolicy policy)
{
    switch (policy) {
        case WritePolicy::LastWins:
            return QStringLiteral("LastWins");
        case WritePolicy::FirstOnly:
            return QStringLiteral("FirstOnly");
        case WritePolicy::Append:
            return QStringLiteral("Append");
    }
    return QString();
}

bool ParseWritePolicy(const QString& value, WritePolicy& policy)
{
    if (value == QStringLiteral("LastWins")) {
        policy = WritePolicy::LastWins;
    } else if (value == QStringLiteral("FirstOnly")) {
        policy = WritePolicy::FirstOnly;
    } else if (value == QStringLiteral("Append")) {
        policy = WritePolicy::Append;
    } else {
        return false;
    }
    return true;
}

bool HasOnlyKeys(const QJsonObject& object,
                 const QSet<QString>& allowed,
                 const QString& context,
                 QString* error)
{
    for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
        if (!allowed.contains(field.key())) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore", "Unknown %1 field: %2")
                                .arg(context, field.key()));
            return false;
        }
    }
    return true;
}

bool RequiredString(const QJsonObject& object,
                    const QString& key,
                    QString& value,
                    QString* error)
{
    const QJsonValue field = object.value(key);
    if (!field.isString()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Recipe field %1 must be a string")
                            .arg(key));
        return false;
    }
    value = field.toString();
    return true;
}

bool OptionalString(const QJsonObject& object,
                    const QString& key,
                    const QString& defaultValue,
                    QString& value,
                    QString* error)
{
    if (!object.contains(key)) {
        value = defaultValue;
        return true;
    }
    return RequiredString(object, key, value, error);
}

bool OptionalBool(const QJsonObject& object,
                  const QString& key,
                  bool defaultValue,
                  bool& value,
                  QString* error)
{
    if (!object.contains(key)) {
        value = defaultValue;
        return true;
    }
    const QJsonValue field = object.value(key);
    if (!field.isBool()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Recipe field %1 must be a boolean")
                            .arg(key));
        return false;
    }
    value = field.toBool();
    return true;
}

bool OptionalInt(const QJsonObject& object,
                 const QString& key,
                 int defaultValue,
                 int minimum,
                 int maximum,
                 int& value,
                 QString* error)
{
    if (!object.contains(key)) {
        value = defaultValue;
        return true;
    }
    const QJsonValue field = object.value(key);
    const double number = field.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!field.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
        number < minimum || number > maximum) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe field %1 must be an integer in [%2, %3]")
                            .arg(key)
                            .arg(minimum)
                            .arg(maximum));
        return false;
    }
    value = static_cast<int>(number);
    return true;
}

bool ParseCaptureNames(const QJsonObject& object,
                       QStringList& names,
                       int maximum,
                       QString* error)
{
    names.clear();
    if (!object.contains(QStringLiteral("captureToVar"))) {
        return true;
    }
    const QJsonValue field = object.value(QStringLiteral("captureToVar"));
    if (!field.isArray()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe field captureToVar must be an array"));
        return false;
    }
    const QJsonArray array = field.toArray();
    if (array.size() > maximum) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe captureToVar exceeds its item limit"));
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue& item : array) {
        if (!item.isString() || !SearchVariableStore::IsValidName(item.toString()) ||
            seen.contains(item.toString())) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe captureToVar contains an invalid or duplicate name"));
            return false;
        }
        seen.insert(item.toString());
        names.append(item.toString());
    }
    return true;
}

bool ParseRule(const QJsonObject& object,
               RegexWorkbenchRule& rule,
               const RegexRecipeLimits& limits,
               QString* error)
{
    static const QSet<QString> allowed = {
        QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("find"),
        QStringLiteral("replace"), QStringLiteral("secondaryMode"),
        QStringLiteral("secondaryPattern"), QStringLiteral("recursive"),
        QStringLiteral("maxIterations"), QStringLiteral("allowEmpty"),
        QStringLiteral("variableExpansionEnabled"),
        QStringLiteral("autoIngestNamedCaptures"),
        QStringLiteral("captureToVar"), QStringLiteral("enabled")
    };
    if (!HasOnlyKeys(object, allowed, QStringLiteral("rule"), error) ||
        !RequiredString(object, QStringLiteral("id"), rule.id, error) ||
        !RequiredString(object, QStringLiteral("name"), rule.name, error) ||
        !RequiredString(object, QStringLiteral("find"), rule.find, error) ||
        !RequiredString(object, QStringLiteral("replace"), rule.replace, error) ||
        !OptionalString(object, QStringLiteral("secondaryPattern"), QString(),
                        rule.secondaryPattern, error)) {
        return false;
    }

    QString secondaryName;
    if (!OptionalString(object, QStringLiteral("secondaryMode"),
                        QStringLiteral("None"), secondaryName, error) ||
        !ParseSecondaryMode(secondaryName, rule.secondaryMode)) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Unknown recipe secondaryMode: %1")
                            .arg(secondaryName));
        return false;
    }
    return OptionalBool(object, QStringLiteral("recursive"), false,
                        rule.recursive, error) &&
           OptionalInt(object, QStringLiteral("maxIterations"), 32, 1,
                       limits.maxIterations, rule.maxIterations, error) &&
           OptionalBool(object, QStringLiteral("allowEmpty"), false,
                        rule.allowEmpty, error) &&
           OptionalBool(object, QStringLiteral("variableExpansionEnabled"), false,
                        rule.variableExpansionEnabled, error) &&
           OptionalBool(object, QStringLiteral("autoIngestNamedCaptures"), false,
                        rule.autoIngestNamedCaptures, error) &&
           ParseCaptureNames(object, rule.captureToVar,
                             limits.maxCaptureNamesPerRule, error) &&
           OptionalBool(object, QStringLiteral("enabled"), true,
                        rule.enabled, error);
}

QJsonObject SerializeRule(const RegexWorkbenchRule& rule)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), rule.id);
    object.insert(QStringLiteral("name"), rule.name);
    object.insert(QStringLiteral("secondaryMode"), SecondaryModeName(rule.secondaryMode));
    object.insert(QStringLiteral("secondaryPattern"), rule.secondaryPattern);
    object.insert(QStringLiteral("find"), rule.find);
    object.insert(QStringLiteral("replace"), rule.replace);
    object.insert(QStringLiteral("recursive"), rule.recursive);
    object.insert(QStringLiteral("maxIterations"), rule.maxIterations);
    object.insert(QStringLiteral("allowEmpty"), rule.allowEmpty);
    object.insert(QStringLiteral("variableExpansionEnabled"),
                  rule.variableExpansionEnabled);
    object.insert(QStringLiteral("autoIngestNamedCaptures"),
                  rule.autoIngestNamedCaptures);
    QJsonArray captures;
    for (const QString& name : rule.captureToVar) {
        captures.append(name);
    }
    object.insert(QStringLiteral("captureToVar"), captures);
    object.insert(QStringLiteral("enabled"), rule.enabled);
    return object;
}

}

bool RegexRecipeStore::Validate(const RegexRecipe& recipe,
                                QString* error,
                                RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    if (!ValidLimits(limits)) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Invalid recipe limits"));
        return false;
    }
    if (recipe.name.isEmpty() || recipe.name.size() > limits.maxNameCodeUnits) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe name is empty or exceeds its limit"));
        return false;
    }
    if (VariableScopeName(recipe.variableScope).isEmpty() ||
        WritePolicyName(recipe.writePolicy).isEmpty()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe has an unknown variable scope or write policy"));
        return false;
    }
    if (recipe.rules.size() > limits.maxRules) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe exceeds its rule-count limit"));
        return false;
    }

    QSet<QString> ids;
    for (const RegexWorkbenchRule& rule : recipe.rules) {
        if (rule.id.isEmpty() || rule.id.size() > limits.maxIdCodeUnits ||
            rule.name.isEmpty() || rule.name.size() > limits.maxNameCodeUnits ||
            rule.find.isEmpty() || rule.find.size() > limits.maxPatternCodeUnits ||
            rule.replace.size() > limits.maxPatternCodeUnits ||
            rule.secondaryPattern.size() > limits.maxPatternCodeUnits) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe rule has an empty or oversized required field"));
            return false;
        }
        if (ids.contains(rule.id)) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe contains duplicate rule id: %1")
                                .arg(rule.id));
            return false;
        }
        ids.insert(rule.id);
        if (SecondaryModeName(rule.secondaryMode).isEmpty() ||
            (rule.secondaryMode == SecondaryMode::None && !rule.secondaryPattern.isEmpty()) ||
            (rule.secondaryMode != SecondaryMode::None && rule.secondaryPattern.isEmpty())) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe rule %1 has inconsistent secondary configuration")
                                .arg(rule.id));
            return false;
        }
        if (rule.maxIterations <= 0 || rule.maxIterations > limits.maxIterations ||
            (rule.allowEmpty && !rule.recursive)) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe rule %1 has invalid recursive limits")
                                .arg(rule.id));
            return false;
        }
        if (rule.captureToVar.size() > limits.maxCaptureNamesPerRule) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe rule %1 exceeds its capture-name limit")
                                .arg(rule.id));
            return false;
        }
        QSet<QString> captureNames;
        for (const QString& name : rule.captureToVar) {
            if (!SearchVariableStore::IsValidName(name) || captureNames.contains(name)) {
                SetError(error, QCoreApplication::translate(
                                    "RegexWorkbenchCore",
                                    "Recipe rule %1 has an invalid or duplicate capture name")
                                    .arg(rule.id));
                return false;
            }
            captureNames.insert(name);
        }
        if (IsWholeFunctionReplacement(rule.replace)) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Recipe rule %1 uses an unsupported Python function replacement")
                                .arg(rule.id));
            return false;
        }
    }
    return true;
}

QByteArray RegexRecipeStore::Serialize(const RegexRecipe& recipe,
                                       QString* error,
                                       RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    if (!Validate(recipe, error, limits)) {
        return QByteArray();
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), RecipeFormat);
    root.insert(QStringLiteral("version"), CurrentVersion);
    root.insert(QStringLiteral("name"), recipe.name);
    root.insert(QStringLiteral("variableScope"),
                VariableScopeName(recipe.variableScope));
    root.insert(QStringLiteral("writePolicy"), WritePolicyName(recipe.writePolicy));
    QJsonArray rules;
    for (const RegexWorkbenchRule& rule : recipe.rules) {
        rules.append(SerializeRule(rule));
    }
    root.insert(QStringLiteral("rules"), rules);
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (data.size() > limits.maxFileBytes) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Serialized recipe exceeds its file-size limit"));
        return QByteArray();
    }
    return data;
}

bool RegexRecipeStore::Deserialize(const QByteArray& data,
                                   RegexRecipe& recipe,
                                   QString* error,
                                   RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    if (!ValidLimits(limits)) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Invalid recipe limits"));
        return false;
    }
    if (data.size() > limits.maxFileBytes) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe exceeds its file-size limit"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Invalid recipe JSON: %1")
                            .arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root = document.object();
    static const QSet<QString> allowed = {
        QStringLiteral("format"), QStringLiteral("version"),
        QStringLiteral("name"), QStringLiteral("variableScope"),
        QStringLiteral("writePolicy"), QStringLiteral("rules")
    };
    if (!HasOnlyKeys(root, allowed, QStringLiteral("root"), error)) {
        return false;
    }
    if (root.value(QStringLiteral("format")).toString() != RecipeFormat) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Unknown recipe format"));
        return false;
    }
    int version = 0;
    if (!root.contains(QStringLiteral("version")) ||
        !OptionalInt(root, QStringLiteral("version"), 0, CurrentVersion,
                     CurrentVersion, version, error)) {
        if (error != nullptr && error->isEmpty()) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore", "Recipe version is required"));
        }
        return false;
    }

    RegexRecipe parsed;
    QString scopeName;
    QString policyName;
    if (!RequiredString(root, QStringLiteral("name"), parsed.name, error) ||
        !RequiredString(root, QStringLiteral("variableScope"), scopeName, error) ||
        !RequiredString(root, QStringLiteral("writePolicy"), policyName, error)) {
        return false;
    }
    if (!ParseVariableScope(scopeName, parsed.variableScope) ||
        !ParseWritePolicy(policyName, parsed.writePolicy)) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Unknown recipe variable scope or write policy"));
        return false;
    }
    const QJsonValue rulesValue = root.value(QStringLiteral("rules"));
    if (!rulesValue.isArray() || rulesValue.toArray().size() > limits.maxRules) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe rules must be an array within the rule-count limit"));
        return false;
    }
    for (const QJsonValue& item : rulesValue.toArray()) {
        if (!item.isObject()) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore",
                                "Each recipe rule must be an object"));
            return false;
        }
        RegexWorkbenchRule rule;
        if (!ParseRule(item.toObject(), rule, limits, error)) {
            return false;
        }
        parsed.rules.append(rule);
    }
    if (!Validate(parsed, error, limits)) {
        return false;
    }
    recipe = parsed;
    return true;
}

bool RegexRecipeStore::SaveFile(const QString& path,
                                const RegexRecipe& recipe,
                                QString* error,
                                RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    if (path.isEmpty()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Recipe path is empty"));
        return false;
    }
    const QByteArray data = Serialize(recipe, error, limits);
    if (data.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    QDir directory = info.dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Could not create recipe directory: %1")
                            .arg(directory.absolutePath()));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Could not save recipe %1: %2")
                            .arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool RegexRecipeStore::LoadFile(const QString& path,
                                RegexRecipe& recipe,
                                QString* error,
                                RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Could not open recipe %1: %2")
                            .arg(path, file.errorString()));
        return false;
    }
    if (file.size() > limits.maxFileBytes) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe exceeds its file-size limit"));
        return false;
    }
    const qint64 readLimit = limits.maxFileBytes == std::numeric_limits<qint64>::max()
                                 ? limits.maxFileBytes
                                 : limits.maxFileBytes + 1;
    const QByteArray data = file.read(readLimit);
    if (data.size() > limits.maxFileBytes) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Recipe exceeds its file-size limit"));
        return false;
    }
    return Deserialize(data, recipe, error, limits);
}

bool RegexRecipeStore::LoadNamed(const QString& identifier,
                                 RegexRecipe& recipe,
                                 QString* resolvedPath,
                                 QString* error,
                                 RegexRecipeLimits limits)
{
    if (error != nullptr) {
        error->clear();
    }
    if (resolvedPath != nullptr) {
        resolvedPath->clear();
    }
    const QString trimmed = identifier.trimmed();
    if (trimmed.isEmpty()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Recipe name or path is empty"));
        return false;
    }

    const QFileInfo explicitInfo(trimmed);
    if (explicitInfo.isAbsolute()) {
        if (!explicitInfo.isFile()) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore", "Recipe file does not exist: %1")
                                .arg(trimmed));
            return false;
        }
        if (!LoadFile(explicitInfo.absoluteFilePath(), recipe, error, limits)) {
            return false;
        }
        if (resolvedPath != nullptr) {
            *resolvedPath = explicitInfo.absoluteFilePath();
        }
        return true;
    }

    if (QFileInfo(trimmed).fileName() != trimmed) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore",
                            "Relative recipe identifiers must not contain directories"));
        return false;
    }

    const QDir directory(DefaultDirectory());
    QStringList candidateNames{trimmed};
    if (!trimmed.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        candidateNames.append(trimmed + QStringLiteral(".json"));
    }
    for (const QString& candidateName : std::as_const(candidateNames)) {
        const QFileInfo candidate(directory.filePath(candidateName));
        if (!candidate.isFile()) {
            continue;
        }
        if (!LoadFile(candidate.absoluteFilePath(), recipe, error, limits)) {
            return false;
        }
        if (resolvedPath != nullptr) {
            *resolvedPath = candidate.absoluteFilePath();
        }
        return true;
    }

    QString matchedPath;
    RegexRecipe matchedRecipe;
    const QFileInfoList entries = directory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& entry : entries) {
        RegexRecipe candidate;
        if (!LoadFile(entry.absoluteFilePath(), candidate, nullptr, limits) ||
            candidate.name != trimmed) {
            continue;
        }
        if (!matchedPath.isEmpty()) {
            SetError(error, QCoreApplication::translate(
                                "RegexWorkbenchCore", "Recipe name is ambiguous: %1")
                                .arg(trimmed));
            return false;
        }
        matchedPath = entry.absoluteFilePath();
        matchedRecipe = candidate;
    }
    if (matchedPath.isEmpty()) {
        SetError(error, QCoreApplication::translate(
                            "RegexWorkbenchCore", "Could not find recipe: %1")
                            .arg(trimmed));
        return false;
    }
    recipe = matchedRecipe;
    if (resolvedPath != nullptr) {
        *resolvedPath = matchedPath;
    }
    return true;
}

QString RegexRecipeStore::DefaultDirectory()
{
    return QDir(Utility::DefinePrefsDir()).filePath(QStringLiteral("regex_workbench"));
}

}
}
