/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginTextEdit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>

namespace
{

bool ReadPosition(const QJsonObject &object, const QString &name, int *position)
{
    const QJsonValue value = object.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > std::numeric_limits<int>::max()) {
        return false;
    }
    *position = static_cast<int>(number);
    return true;
}

bool IsUtf16Boundary(const QString &text, int position)
{
    return position <= 0 || position >= text.size()
        || !(text.at(position - 1).isHighSurrogate() && text.at(position).isLowSurrogate());
}

}

namespace PluginApi
{

bool ParseTextEdits(const QJsonArray &values,
                    const QString &current_text,
                    QList<TextEdit> *edits,
                    QString *error)
{
    if (!edits) return false;
    edits->clear();
    if (values.isEmpty() || values.size() > 1000) {
        if (error) *error = QStringLiteral("An edit request must contain between 1 and 1000 edits");
        return false;
    }

    for (const QJsonValue &value : values) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Every edit must be an object");
            return false;
        }
        const QJsonObject object = value.toObject();
        TextEdit edit;
        if (!ReadPosition(object, QStringLiteral("start"), &edit.start)
            || !ReadPosition(object, QStringLiteral("end"), &edit.end)
            || !object.value(QStringLiteral("text")).isString()) {
            if (error) *error = QStringLiteral("Edit start/end must be integers and text must be a string");
            return false;
        }
        if (edit.start > edit.end || edit.end > current_text.size()) {
            if (error) *error = QStringLiteral("Edit range is outside the current document");
            return false;
        }
        if (!IsUtf16Boundary(current_text, edit.start) || !IsUtf16Boundary(current_text, edit.end)) {
            if (error) *error = QStringLiteral("Edit range splits a UTF-16 surrogate pair");
            return false;
        }
        edit.text = object.value(QStringLiteral("text")).toString();
        edits->append(edit);
    }

    std::sort(edits->begin(), edits->end(), [](const TextEdit &left, const TextEdit &right) {
        if (left.start != right.start) return left.start < right.start;
        return left.end < right.end;
    });
    for (int index = 1; index < edits->size(); ++index) {
        const TextEdit &previous = edits->at(index - 1);
        const TextEdit &current = edits->at(index);
        if (current.start < previous.end
            || (current.start == previous.start && current.end == previous.end)) {
            if (error) *error = QStringLiteral("Edit ranges overlap");
            edits->clear();
            return false;
        }
    }
    std::reverse(edits->begin(), edits->end());
    return true;
}

void ApplyTextEdits(QTextDocument *document, const QList<TextEdit> &edits)
{
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (const TextEdit &edit : edits) {
        cursor.setPosition(edit.start, QTextCursor::MoveAnchor);
        cursor.setPosition(edit.end, QTextCursor::KeepAnchor);
        cursor.insertText(edit.text);
    }
    cursor.endEditBlock();
}

} // namespace PluginApi
