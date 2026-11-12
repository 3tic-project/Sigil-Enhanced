#pragma once

#include <QString>

namespace CodeViewTextDrag
{

inline QString MimeType()
{
    return QStringLiteral("application/x-sigil-codeview-text");
}

struct Result
{
    QString text;
    int selectionStart = 0;
    int selectionEnd = 0;
    bool changed = false;
};

// Moves the half-open range [start, end) to the caret position `drop`.
// A drop inside the selection, or any out-of-range index, leaves the text alone.
inline Result Apply(const QString &text, int start, int end, int drop)
{
    Result result;
    result.text = text;
    result.selectionStart = start;
    result.selectionEnd = end;
    if (start < 0 || end > text.size() || start >= end || drop < 0 || drop > text.size()) {
        return result;
    }
    if (drop >= start && drop <= end) {
        return result;
    }

    const int length = end - start;
    const QString moved = text.mid(start, length);
    if (drop > end) {
        result.text.remove(start, length);
        const int insert_at = drop - length;
        result.text.insert(insert_at, moved);
        result.selectionStart = insert_at;
        result.selectionEnd = insert_at + length;
    } else {
        result.text.insert(drop, moved);
        result.text.remove(start + length, length);
        result.selectionStart = drop;
        result.selectionEnd = drop + length;
    }
    result.changed = result.text != text;
    return result;
}

}
