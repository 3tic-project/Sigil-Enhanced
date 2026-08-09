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

#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

#include <algorithm>

#include <QDataStream>
#include <QIODevice>
#include <QSet>

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

void SetError(QString* error, const QString& message)
{
    if (error != nullptr) {
        *error = message;
    }
}

void WriteFrame(QDataStream& stream, const SearchVariableStore::Frame& frame)
{
    QStringList names = frame.keys();
    names.sort();
    stream << names.size();
    for (const QString& name : names) {
        stream << name << frame.value(name);
    }
}

}

SearchVariableStore::SearchVariableStore(VariableStoreLimits limits) :
    m_limits(limits)
{
}

void SearchVariableStore::setScope(VariableScope scope)
{
    m_scope = scope;
}

VariableScope SearchVariableStore::scope() const
{
    return m_scope;
}

void SearchVariableStore::setWritePolicy(WritePolicy policy)
{
    m_writePolicy = policy;
}

WritePolicy SearchVariableStore::writePolicy() const
{
    return m_writePolicy;
}

void SearchVariableStore::setActiveResource(const QString& bookpath)
{
    m_activeResource = bookpath;
}

QString SearchVariableStore::activeResource() const
{
    return m_activeResource;
}

void SearchVariableStore::clear()
{
    m_resourceFrames.clear();
    m_batchFrame.clear();
    m_sessionFrame.clear();
    m_totalCodeUnits = 0;
    m_variableCount = 0;
}

void SearchVariableStore::clearRunLocals()
{
    m_resourceFrames.clear();
    m_batchFrame.clear();
    m_totalCodeUnits = FrameCodeUnits(m_sessionFrame);
    m_variableCount = m_sessionFrame.size();
}

bool SearchVariableStore::has(const QString& name) const
{
    const Frame* frame = activeFrame();
    return frame != nullptr && frame->contains(name);
}

QString SearchVariableStore::get(const QString& name, bool* found) const
{
    const QStringList values = getList(name);
    const bool hasValue = !values.isEmpty();
    if (found != nullptr) {
        *found = hasValue;
    }
    return hasValue ? values.last() : QString();
}

QStringList SearchVariableStore::getList(const QString& name) const
{
    const Frame* frame = activeFrame();
    return frame == nullptr ? QStringList() : frame->value(name);
}

bool SearchVariableStore::set(const QString& name, const QString& value, QString* error)
{
    if (!IsValidName(name)) {
        SetError(error, QStringLiteral("Invalid variable name: %1").arg(name));
        return false;
    }
    if (m_limits.maxValueCodeUnits <= 0 || m_limits.maxTotalCodeUnits <= 0 ||
        m_limits.maxVariables <= 0) {
        SetError(error, QStringLiteral("Invalid variable store limits"));
        return false;
    }
    if (value.size() > m_limits.maxValueCodeUnits) {
        SetError(error, QStringLiteral("Variable %1 exceeds the per-value UTF-16 limit").arg(name));
        return false;
    }

    if (m_scope == VariableScope::Resource && m_activeResource.isEmpty()) {
        SetError(error, QStringLiteral("No active resource for resource-scoped variables"));
        return false;
    }
    const Frame* existingFrame = static_cast<const SearchVariableStore*>(this)->activeFrame();
    const bool isNewVariable = existingFrame == nullptr || !existingFrame->contains(name);
    const QStringList previous = existingFrame == nullptr
                                     ? QStringList()
                                     : existingFrame->value(name);
    if (m_writePolicy == WritePolicy::FirstOnly && !previous.isEmpty()) {
        return true;
    }
    if (isNewVariable && m_variableCount >= m_limits.maxVariables) {
        SetError(error, QStringLiteral("Variable store exceeds its variable-count limit"));
        return false;
    }

    qint64 previousUnits = 0;
    for (const QString& item : previous) {
        previousUnits += item.size();
    }
    const qint64 proposedTotal = m_writePolicy == WritePolicy::Append
                                     ? m_totalCodeUnits + value.size()
                                     : m_totalCodeUnits - previousUnits + value.size();
    if (proposedTotal > m_limits.maxTotalCodeUnits) {
        SetError(error, QStringLiteral("Variable store exceeds its total UTF-16 limit"));
        return false;
    }

    Frame* frame = activeFrame(error);
    if (frame == nullptr) {
        return false;
    }
    if (m_writePolicy == WritePolicy::Append) {
        (*frame)[name].append(value);
    } else {
        (*frame)[name] = QStringList{value};
    }
    m_totalCodeUnits = proposedTotal;
    if (isNewVariable) {
        ++m_variableCount;
    }
    return true;
}

bool SearchVariableStore::ingestNamedCaptures(
    const QHash<QString, int>& captureNumbers,
    const QString& matchText,
    const QList<std::pair<int, int>>& captures,
    const QStringList& onlyNames,
    QString* error)
{
    QStringList names = onlyNames.isEmpty() ? captureNumbers.keys() : onlyNames;
    if (onlyNames.isEmpty()) {
        names.sort();
    }
    QSet<QString> seen;
    const Snapshot before = snapshot();
    for (const QString& name : names) {
        if (seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        if (!captureNumbers.contains(name)) {
            restore(before);
            SetError(error, QStringLiteral("Named capture does not exist: %1").arg(name));
            return false;
        }
        const int number = captureNumbers.value(name);
        if (number <= 0 || number >= captures.size()) {
            restore(before);
            SetError(error, QStringLiteral("Invalid capture number for %1").arg(name));
            return false;
        }
        const std::pair<int, int> offsets = captures.at(number);
        if (offsets.first < 0 && offsets.second < 0) {
            continue;
        }
        if (offsets.first < 0 || offsets.second < 0 ||
            offsets.second < offsets.first || offsets.second > matchText.size()) {
            restore(before);
            SetError(error, QStringLiteral("Invalid capture offsets for %1").arg(name));
            return false;
        }
        if (!set(name, matchText.mid(offsets.first, offsets.second - offsets.first), error)) {
            restore(before);
            return false;
        }
    }
    return true;
}

SearchVariableStore::Snapshot SearchVariableStore::snapshot() const
{
    Snapshot value;
    value.scope = m_scope;
    value.writePolicy = m_writePolicy;
    value.activeResource = m_activeResource;
    value.resourceFrames = m_resourceFrames;
    value.batchFrame = m_batchFrame;
    value.sessionFrame = m_sessionFrame;
    return value;
}

bool SearchVariableStore::restore(const Snapshot& value, QString* error)
{
    qint64 codeUnits = 0;
    int variableCount = 0;
    if (!validateSnapshot(value, codeUnits, variableCount, error)) {
        return false;
    }
    m_scope = value.scope;
    m_writePolicy = value.writePolicy;
    m_activeResource = value.activeResource;
    m_resourceFrames = value.resourceFrames;
    m_batchFrame = value.batchFrame;
    m_sessionFrame = value.sessionFrame;
    m_totalCodeUnits = codeUnits;
    m_variableCount = variableCount;
    return true;
}

QByteArray SearchVariableStore::stateData() const
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << static_cast<qint32>(m_scope)
           << static_cast<qint32>(m_writePolicy)
           << m_activeResource;
    QStringList resources = m_resourceFrames.keys();
    resources.sort();
    stream << resources.size();
    for (const QString& resource : resources) {
        stream << resource;
        WriteFrame(stream, m_resourceFrames.value(resource));
    }
    WriteFrame(stream, m_batchFrame);
    WriteFrame(stream, m_sessionFrame);
    return data;
}

qint64 SearchVariableStore::totalCodeUnits() const
{
    return m_totalCodeUnits;
}

bool SearchVariableStore::IsValidName(const QString& name)
{
    if (name.isEmpty() || name.size() > 64) {
        return false;
    }
    const auto isAsciiLetter = [](QChar character) {
        const ushort value = character.unicode();
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    };
    if (!isAsciiLetter(name.at(0)) && name.at(0) != QLatin1Char('_')) {
        return false;
    }
    for (int index = 1; index < name.size(); ++index) {
        const QChar character = name.at(index);
        const ushort value = character.unicode();
        const bool isAsciiDigit = value >= '0' && value <= '9';
        if (!isAsciiLetter(character) && !isAsciiDigit && character != QLatin1Char('_')) {
            return false;
        }
    }
    return true;
}

SearchVariableStore::Frame* SearchVariableStore::activeFrame(QString* error)
{
    switch (m_scope) {
        case VariableScope::Resource:
            if (m_activeResource.isEmpty()) {
                SetError(error, QStringLiteral("No active resource for resource-scoped variables"));
                return nullptr;
            }
            return &m_resourceFrames[m_activeResource];
        case VariableScope::Batch:
            return &m_batchFrame;
        case VariableScope::Session:
            return &m_sessionFrame;
    }
    SetError(error, QStringLiteral("Unknown variable scope"));
    return nullptr;
}

const SearchVariableStore::Frame* SearchVariableStore::activeFrame() const
{
    switch (m_scope) {
        case VariableScope::Resource: {
            const auto found = m_resourceFrames.constFind(m_activeResource);
            return found == m_resourceFrames.constEnd() ? nullptr : &found.value();
        }
        case VariableScope::Batch:
            return &m_batchFrame;
        case VariableScope::Session:
            return &m_sessionFrame;
    }
    return nullptr;
}

qint64 SearchVariableStore::FrameCodeUnits(const Frame& frame)
{
    qint64 total = 0;
    for (auto variable = frame.constBegin(); variable != frame.constEnd(); ++variable) {
        for (const QString& value : variable.value()) {
            total += value.size();
        }
    }
    return total;
}

qint64 SearchVariableStore::SnapshotCodeUnits(const Snapshot& value)
{
    qint64 total = FrameCodeUnits(value.batchFrame) + FrameCodeUnits(value.sessionFrame);
    for (const Frame& frame : value.resourceFrames) {
        total += FrameCodeUnits(frame);
    }
    return total;
}

int SearchVariableStore::SnapshotVariableCount(const Snapshot& value)
{
    int total = value.batchFrame.size() + value.sessionFrame.size();
    for (const Frame& frame : value.resourceFrames) {
        total += frame.size();
    }
    return total;
}

bool SearchVariableStore::validateSnapshot(const Snapshot& value,
                                           qint64& codeUnits,
                                           int& variableCount,
                                           QString* error) const
{
    const auto validateFrame = [&](const Frame& frame) {
        for (auto variable = frame.constBegin(); variable != frame.constEnd(); ++variable) {
            if (!IsValidName(variable.key()) || variable.value().isEmpty()) {
                return false;
            }
            for (const QString& item : variable.value()) {
                if (item.size() > m_limits.maxValueCodeUnits) {
                    return false;
                }
            }
        }
        return true;
    };

    const bool validScope = value.scope == VariableScope::Resource ||
                            value.scope == VariableScope::Batch ||
                            value.scope == VariableScope::Session;
    const bool validPolicy = value.writePolicy == WritePolicy::LastWins ||
                             value.writePolicy == WritePolicy::FirstOnly ||
                             value.writePolicy == WritePolicy::Append;
    if (!validScope || !validPolicy ||
        !validateFrame(value.batchFrame) || !validateFrame(value.sessionFrame)) {
        SetError(error, QStringLiteral("Invalid variable store snapshot"));
        return false;
    }
    for (auto resource = value.resourceFrames.constBegin();
         resource != value.resourceFrames.constEnd(); ++resource) {
        if (resource.key().isEmpty() || !validateFrame(resource.value())) {
            SetError(error, QStringLiteral("Invalid resource frame in variable store snapshot"));
            return false;
        }
    }
    codeUnits = SnapshotCodeUnits(value);
    variableCount = SnapshotVariableCount(value);
    if (codeUnits > m_limits.maxTotalCodeUnits || variableCount > m_limits.maxVariables) {
        SetError(error, QStringLiteral("Variable store snapshot exceeds total limit"));
        return false;
    }
    return true;
}

}
}
