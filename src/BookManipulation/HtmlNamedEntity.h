#pragma once
#ifndef HTML_NAMED_ENTITY_H
#define HTML_NAMED_ENTITY_H

#include <QList>
#include <QString>

// QChar is UTF-16 only and asserts in Qt 6 debug builds when cp > 0xFFFF.
inline QString HtmlNamedEntityFromCodepoint(uint cp)
{
    const char32_t u = static_cast<char32_t>(cp);
    return QString::fromUcs4(&u, 1);
}

inline QString HtmlNamedEntityToNumericReferences(const QString &chars)
{
    QString refs;
    const auto cps = chars.toUcs4();
    for (uint cp : cps) {
        refs += QLatin1String("&#x") + QString::number(cp, 16) + QLatin1Char(';');
    }
    return refs;
}

#endif
