/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINTEXTEDIT_H
#define PLUGINTEXTEDIT_H

#include <QJsonArray>
#include <QList>
#include <QString>

class QTextDocument;

namespace PluginApi
{

struct TextEdit {
    int start = 0;
    int end = 0;
    QString text;
};

bool ParseTextEdits(const QJsonArray &values,
                    const QString &current_text,
                    QList<TextEdit> *edits,
                    QString *error);
void ApplyTextEdits(QTextDocument *document, const QList<TextEdit> &edits);

} // namespace PluginApi

#endif // PLUGINTEXTEDIT_H
