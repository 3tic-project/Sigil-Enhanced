/************************************************************************
**
**  Copyright (C) 2026 Kevin B. Hendricks, Stratford, Ontario, Canada
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include "Misc/CodepointNames.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstring>

namespace {

constexpr const char* kControlNames[] = {
    "NULL", "START OF HEADING", "START OF TEXT", "END OF TEXT",
    "END OF TRANSMISSION", "ENQUIRY", "ACKNOWLEDGE", "BELL",
    "BACKSPACE", "TAB", "NEW LINE", "VERTICAL TAB", "FORM FEED",
    "CARRIAGE RETURN", "SHIFT OUT", "SHIFT IN", "DATA LINK ESCAPE",
    "DEVICE CONTROL ONE", "DEVICE CONTROL TWO", "DEVICE CONTROL THREE",
    "DEVICE CONTROL FOUR", "NEGATIVE ACKNOWLEDGE", "SYNCHRONOUS IDLE",
    "END OF TRANSMISSION BLOCK", "CANCEL", "END OF MEDIUM",
    "SUBSTITUTE", "ESCAPE", "FILE SEPARATOR (FS)",
    "GROUP SEPARATOR (GS)", "RECORD SEPARATOR (RS)", "UNIT SEPARATOR (US)",
};

quint32 ReadLe32(const char* address)
{
    const auto* bytes = reinterpret_cast<const uchar*>(address);
    return quint32(bytes[0]) | (quint32(bytes[1]) << 8)
        | (quint32(bytes[2]) << 16) | (quint32(bytes[3]) << 24);
}

struct NameArchive {
    QByteArray bytes;
    quint32 count = 0;
    qsizetype names_offset = 0;
    quint32 names_size = 0;
    bool valid = false;
};

const NameArchive& Archive()
{
    static const NameArchive archive = [] {
        NameArchive result;
        QFile file(QStringLiteral(":/codepoint_names/unicode14.bin"));
        if (!file.open(QFile::ReadOnly)) {
            return result;
        }
        result.bytes = qUncompress(file.readAll());
        if (result.bytes.size() < 12
            || std::memcmp(result.bytes.constData(), "CPN1", 4) != 0) {
            return result;
        }
        result.count = ReadLe32(result.bytes.constData() + 4);
        result.names_size = ReadLe32(result.bytes.constData() + 8);
        result.names_offset = 12 + qsizetype(result.count) * 8;
        result.valid = result.names_offset <= result.bytes.size()
            && result.names_size == result.bytes.size() - result.names_offset;
        return result;
    }();
    return archive;
}

QString LookupName(int cp)
{
    if (cp < 32) {
        return QString::fromLatin1(kControlNames[cp]);
    }
    const NameArchive& archive = Archive();
    if (!archive.valid) {
        return QString();
    }

    quint32 first = 0;
    quint32 last = archive.count;
    const char* data = archive.bytes.constData();
    while (first < last) {
        const quint32 middle = first + (last - first) / 2;
        const quint32 candidate = ReadLe32(data + 12 + qsizetype(middle) * 8);
        if (candidate < quint32(cp)) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    if (first == archive.count
        || ReadLe32(data + 12 + qsizetype(first) * 8) != quint32(cp)) {
        return QStringLiteral("Unknown");
    }
    const quint32 offset = ReadLe32(data + 12 + qsizetype(first) * 8 + 4);
    if (offset >= archive.names_size) {
        return QString();
    }
    const char* name = data + archive.names_offset + offset;
    const char* end = static_cast<const char*>(std::memchr(name, 0, archive.names_size - offset));
    return end ? QString::fromLatin1(name, end - name) : QString();
}

}

CodepointNames::CodepointNames() = default;

QString CodepointNames::GetName(int cp)
{
    if (cp == -1) {
        return QStringLiteral("EOF");
    }
    if (cp < 0 || cp > 0x10ffff) {
        return QString();
    }
    const auto cached = m_NameCache.constFind(cp);
    if (cached != m_NameCache.cend()) {
        return *cached;
    }
    const QString name = LookupName(cp);
    m_NameCache.insert(cp, name);
    return name;
}
