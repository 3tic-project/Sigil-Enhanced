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

#pragma once
#ifndef SEARCH_VARIABLE_STORE_H
#define SEARCH_VARIABLE_STORE_H

#include <utility>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class SPCRE;

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

enum class VariableScope {
    Resource,
    Batch,
    Session
};

enum class WritePolicy {
    LastWins,
    FirstOnly,
    Append
};

struct VariableStoreLimits
{
    qint64 maxValueCodeUnits = 64 * 1024;
    qint64 maxTotalCodeUnits = 4 * 1024 * 1024;
    int maxVariables = 4096;
};

class SearchVariableStore final
{
public:
    using Frame = QHash<QString, QStringList>;

    struct Snapshot {
        VariableScope scope = VariableScope::Resource;
        WritePolicy writePolicy = WritePolicy::LastWins;
        QString activeResource;
        QHash<QString, Frame> resourceFrames;
        Frame batchFrame;
        Frame sessionFrame;
    };

    explicit SearchVariableStore(VariableStoreLimits limits = VariableStoreLimits());

    void setScope(VariableScope scope);
    VariableScope scope() const;
    void setWritePolicy(WritePolicy policy);
    WritePolicy writePolicy() const;
    void setActiveResource(const QString& bookpath);
    QString activeResource() const;

    void clear();
    void clearRunLocals();

    bool has(const QString& name) const;
    QString get(const QString& name, bool* found = nullptr) const;
    QStringList getList(const QString& name) const;
    bool set(const QString& name, const QString& value, QString* error = nullptr);

    bool ingestNamedCaptures(
        const QHash<QString, int>& captureNumbers,
        const QString& matchText,
        const QList<std::pair<int, int>>& captures,
        const QStringList& onlyNames = QStringList(),
        QString* error = nullptr);
    bool ingestNamedCaptures(
        SPCRE& regex,
        const QString& matchText,
        const QList<std::pair<int, int>>& captures,
        const QStringList& onlyNames = QStringList(),
        QString* error = nullptr);

    Snapshot snapshot() const;
    bool restore(const Snapshot& snapshot, QString* error = nullptr);
    QByteArray stateData() const;
    qint64 totalCodeUnits() const;

    static bool IsValidName(const QString& name);

private:
    Frame* activeFrame(QString* error = nullptr);
    const Frame* activeFrame() const;
    static qint64 FrameCodeUnits(const Frame& frame);
    static qint64 SnapshotCodeUnits(const Snapshot& snapshot);
    static int SnapshotVariableCount(const Snapshot& snapshot);
    bool validateSnapshot(const Snapshot& snapshot,
                          qint64& codeUnits,
                          int& variableCount,
                          QString* error) const;

    VariableStoreLimits m_limits;
    VariableScope m_scope = VariableScope::Resource;
    WritePolicy m_writePolicy = WritePolicy::LastWins;
    QString m_activeResource;
    QHash<QString, Frame> m_resourceFrames;
    Frame m_batchFrame;
    Frame m_sessionFrame;
    qint64 m_totalCodeUnits = 0;
    int m_variableCount = 0;
};

}
}

#endif // SEARCH_VARIABLE_STORE_H
