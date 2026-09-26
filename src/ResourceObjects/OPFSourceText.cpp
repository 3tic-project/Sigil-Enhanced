#include "ResourceObjects/OPFSourceText.h"

#include <QHash>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <utility>

namespace {

bool IsLineEnd(QChar character)
{
    return character == QChar('\r') || character == QChar('\n') ||
           character == QChar(0x2028) || character == QChar(0x2029);
}

QStringList SplitLines(const QString &text, bool editor)
{
    QStringList lines;
    int start = 0;
    for (int index = 0; index < text.size(); ++index) {
        if (editor ? text.at(index) != QChar('\n') : !IsLineEnd(text.at(index))) continue;
        if (!editor && text.at(index) == QChar('\r') &&
            index + 1 < text.size() && text.at(index + 1) == QChar('\n')) ++index;
        lines.append(text.mid(start, index - start + 1));
        start = index + 1;
    }
    if (start < text.size()) lines.append(text.mid(start));
    return lines;
}

QString EditorText(QString source)
{
    return source.replace("\r\n", "\n").replace('\r', '\n')
                 .replace(QChar(0x2028), QChar('\n')).replace(QChar(0x2029), QChar('\n'));
}

QString Ending(const QString &line)
{
    if (line.endsWith("\r\n")) return QStringLiteral("\r\n");
    if (!line.isEmpty() && IsLineEnd(line.back())) return line.right(1);
    return {};
}

struct Match {
    int old = 0;
    int edited = 0;
    int length = 0;
};

struct Range {
    int oldBegin;
    int oldEnd;
    int editedBegin;
    int editedEnd;
};

// The same longest-match and autojunk rules as difflib.SequenceMatcher,
// needed to preserve the original suffix of long runs of repeated lines.
QVector<Match> MatchingBlocks(const QStringList &old, const QStringList &edited)
{
    QHash<QString, QVector<int>> positions;
    for (int index = 0; index < edited.size(); ++index) positions[edited.at(index)].append(index);
    if (edited.size() >= 200) {
        const int popular = edited.size() / 100 + 1;
        for (auto it = positions.begin(); it != positions.end();) {
            if (it.value().size() > popular) it = positions.erase(it);
            else ++it;
        }
    }

    QVector<Match> blocks;
    QVector<Range> pending {{0, int(old.size()), 0, int(edited.size())}};
    while (!pending.isEmpty()) {
        const Range range = pending.takeLast();
        Match best;
        best.old = range.oldBegin;
        best.edited = range.editedBegin;
        QHash<int, int> previous;
        for (int oldIndex = range.oldBegin; oldIndex < range.oldEnd; ++oldIndex) {
            QHash<int, int> current;
            const auto found = positions.constFind(old.at(oldIndex));
            if (found != positions.cend()) {
                for (const int editedIndex : found.value()) {
                    if (editedIndex < range.editedBegin) continue;
                    if (editedIndex >= range.editedEnd) break;
                    const int length = previous.value(editedIndex - 1) + 1;
                    current.insert(editedIndex, length);
                    if (length > best.length) {
                        best = {oldIndex - length + 1, editedIndex - length + 1, length};
                    }
                }
            }
            previous = std::move(current);
        }
        while (best.old > range.oldBegin && best.edited > range.editedBegin &&
               old.at(best.old - 1) == edited.at(best.edited - 1)) {
            --best.old;
            --best.edited;
            ++best.length;
        }
        while (best.old + best.length < range.oldEnd &&
               best.edited + best.length < range.editedEnd &&
               old.at(best.old + best.length) == edited.at(best.edited + best.length)) {
            ++best.length;
        }
        if (best.length == 0) continue;
        blocks.append(best);
        if (range.oldBegin < best.old && range.editedBegin < best.edited)
            pending.append({range.oldBegin, best.old, range.editedBegin, best.edited});
        if (best.old + best.length < range.oldEnd && best.edited + best.length < range.editedEnd)
            pending.append({best.old + best.length, range.oldEnd,
                            best.edited + best.length, range.editedEnd});
    }
    std::sort(blocks.begin(), blocks.end(), [](const Match &left, const Match &right) {
        return left.old < right.old;
    });
    QVector<Match> merged;
    for (const Match &block : blocks) {
        if (!merged.isEmpty() && merged.last().old + merged.last().length == block.old &&
            merged.last().edited + merged.last().length == block.edited) {
            merged.last().length += block.length;
        } else {
            merged.append(block);
        }
    }
    merged.append({int(old.size()), int(edited.size()), 0});
    return merged;
}

}

QString OPFSourceText::Restore(const QString &original, const QString &edited)
{
    if (EditorText(original) == edited) return original;
    const QStringList old = SplitLines(original, false);
    const QStringList updated = SplitLines(edited, true);
    QStringList oldEditor;
    oldEditor.reserve(old.size());
    for (const QString &line : old) oldEditor.append(EditorText(line));

    int prefix = 0;
    while (prefix < old.size() && prefix < updated.size() && oldEditor.at(prefix) == updated.at(prefix))
        ++prefix;
    int oldEnd = old.size();
    int updatedEnd = updated.size();
    while (oldEnd > prefix && updatedEnd > prefix &&
           oldEditor.at(oldEnd - 1) == updated.at(updatedEnd - 1)) {
        --oldEnd;
        --updatedEnd;
    }

    QStringList endings, nextStyle, previousStyle;
    endings.reserve(old.size());
    nextStyle.resize(old.size() + 1);
    previousStyle.resize(old.size() + 1);
    for (const QString &line : old) endings.append(Ending(line));
    for (int index = old.size() - 1; index >= 0; --index)
        nextStyle[index] = endings.at(index).isEmpty() ? nextStyle.at(index + 1) : endings.at(index);
    for (int index = 0; index < old.size(); ++index)
        previousStyle[index + 1] = endings.at(index).isEmpty() ? previousStyle.at(index) : endings.at(index);

    QStringList result = old.mid(0, prefix);
    const QVector<Match> blocks = MatchingBlocks(oldEditor.mid(prefix, oldEnd - prefix),
                                                  updated.mid(prefix, updatedEnd - prefix));
    int oldIndex = prefix;
    int updatedIndex = prefix;
    for (const Match &block : blocks) {
        const int left = oldIndex;
        const int right = prefix + block.old;
        const int start = updatedIndex;
        const int end = prefix + block.edited;
        QString nearby = nextStyle.at(left);
        if (nearby.isEmpty()) nearby = previousStyle.at(left);
        if (nearby.isEmpty()) nearby = QStringLiteral("\n");
        for (int index = start; index < end; ++index) {
            QString style = nearby;
            if (right - left == end - start && !endings.at(left + index - start).isEmpty())
                style = endings.at(left + index - start);
            const QString &line = updated.at(index);
            result.append(line.endsWith('\n') ? line.left(line.size() - 1) + style : line);
        }
        for (int index = 0; index < block.length; ++index) result.append(old.at(right + index));
        oldIndex = right + block.length;
        updatedIndex = end + block.length;
    }
    for (int index = oldEnd; index < old.size(); ++index) result.append(old.at(index));
    return result.join(QString());
}
