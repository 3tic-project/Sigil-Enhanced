/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
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

#include <QByteArray>
#include <QString>

class EpubFileSnapshot
{
public:
    EpubFileSnapshot();

    static EpubFileSnapshot capture(const QString& path, QString* error = nullptr);

    bool isValid() const;
    QString path() const;
    QByteArray sha256() const;
    bool matchesSource(QString* error = nullptr) const;
    bool copyTo(const QString& destination, QString* error = nullptr) const;
    void clear();

private:
    QString m_Path;
    qint64 m_Size;
    qint64 m_ModifiedMs;
    QByteArray m_Sha256;
    bool m_Valid;
};
