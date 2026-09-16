/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/PatchRange.h"

#include <QJsonArray>

namespace SigilAgent
{

namespace
{

constexpr int MAX_OCCURRENCE_PREVIEW = 20;
constexpr int MAX_MISMATCH_TEXT_PREVIEW = 512;

QString sliceAt(const QString &source, int start, int end)
{
    if (start < 0 || end < start || end > source.size()) return QString();
    return source.mid(start, end - start);
}

struct HitProbe {
    int first = -1;
    int count = 0;
};

HitProbe probeHits(const QString &source, const QString &expected,
                   int start = 0, int end = -1)
{
    HitProbe probe;
    if (expected.isEmpty()) return probe;
    const int bounded_start = qBound(0, start, source.size());
    const int bounded_end = end < 0
        ? source.size() : qBound(bounded_start, end, source.size());
    int from = bounded_start;
    while (probe.count < 2) {
        const int at = source.indexOf(expected, from);
        if (at < 0 || at >= bounded_end) break;
        if (probe.first < 0) probe.first = at;
        ++probe.count;
        from = at + expected.size();
    }
    return probe;
}

bool sourceRangeForLine(const QString &source, int wanted_line,
                        int *start, int *end)
{
    if (wanted_line < 1 || !start || !end) return false;
    int line = 1;
    int line_start = 0;
    while (line < wanted_line) {
        const int newline = source.indexOf(QLatin1Char('\n'), line_start);
        if (newline < 0) return false;
        line_start = newline + 1;
        ++line;
    }
    const int newline = source.indexOf(QLatin1Char('\n'), line_start);
    *start = line_start;
    *end = newline < 0 ? source.size() : newline + 1;
    return true;
}

QJsonArray occurrenceList(const QString &source, const QString &expected,
                          bool *truncated)
{
    QJsonArray occurrences;
    if (truncated) *truncated = false;
    if (expected.isEmpty()) return occurrences;
    int from = 0;
    int line = 1;
    int next_newline = source.indexOf(QLatin1Char('\n'));
    while (occurrences.size() <= MAX_OCCURRENCE_PREVIEW) {
        const int at = source.indexOf(expected, from);
        if (at < 0) break;
        if (occurrences.size() == MAX_OCCURRENCE_PREVIEW) {
            if (truncated) *truncated = true;
            break;
        }
        while (next_newline >= 0 && next_newline < at) {
            ++line;
            next_newline = source.indexOf(QLatin1Char('\n'), next_newline + 1);
        }
        occurrences.append(QJsonObject {
            { QStringLiteral("start"), at },
            { QStringLiteral("end"), at + expected.size() },
            { QStringLiteral("line"), line }
        });
        from = at + expected.size();
    }
    return occurrences;
}

void addTextPreview(QJsonObject *object, const QString &key,
                    const QString &text)
{
    if (!object) return;
    object->insert(key, text.left(MAX_MISMATCH_TEXT_PREVIEW));
    object->insert(key + QStringLiteral("_length"), text.size());
    object->insert(key + QStringLiteral("_truncated"),
                   text.size() > MAX_MISMATCH_TEXT_PREVIEW);
}

QJsonObject mismatchData(const QString &source, int start, int end, const QString &expected)
{
    const bool valid_range = start >= 0 && end >= start && end <= source.size();
    const int actual_length = valid_range ? end - start : 0;
    const QString actual = valid_range
        ? source.mid(start, qMin(actual_length, MAX_MISMATCH_TEXT_PREVIEW))
        : QString();
    const int context = 40;
    const int bounded_start = qBound(0, start, source.size());
    const int bounded_end = valid_range ? end : bounded_start;
    const int from = qMax(0, bounded_start - context);
    const int context_length = qMin(
        source.size() - from, bounded_end - bounded_start + context * 2);
    QJsonObject data {
        { QStringLiteral("start"), start },
        { QStringLiteral("end"), end },
        { QStringLiteral("total"), source.size() }
    };
    addTextPreview(&data, QStringLiteral("expected_text"), expected);
    data.insert(QStringLiteral("actual_text"), actual);
    data.insert(QStringLiteral("actual_text_length"), actual_length);
    data.insert(QStringLiteral("actual_text_truncated"),
                actual_length > MAX_MISMATCH_TEXT_PREVIEW);
    data.insert(QStringLiteral("context"), source.mid(
        from, qMin(context_length, MAX_MISMATCH_TEXT_PREVIEW)));
    data.insert(QStringLiteral("context_start"), from);
    data.insert(QStringLiteral("context_length"), context_length);
    data.insert(QStringLiteral("context_truncated"),
                context_length > MAX_MISMATCH_TEXT_PREVIEW);
    bool occurrences_truncated = false;
    const QJsonArray occurrences = occurrenceList(
        source, expected, &occurrences_truncated);
    data.insert(QStringLiteral("occurrences"), occurrences);
    data.insert(QStringLiteral("occurrences_returned"), occurrences.size());
    data.insert(QStringLiteral("occurrences_truncated"), occurrences_truncated);
    if (occurrences_truncated) {
        data.insert(QStringLiteral("occurrence_count_lower_bound"),
                    MAX_OCCURRENCE_PREVIEW + 1);
    } else {
        data.insert(QStringLiteral("occurrence_count"), occurrences.size());
    }
    return data;
}

} // namespace

int lineNumberAt(const QString &source, int offset)
{
    int line = 1;
    const int n = qBound(0, offset, source.size());
    for (int i = 0; i < n; ++i) {
        if (source.at(i) == QLatin1Char('\n')) ++line;
    }
    return line;
}

QJsonArray fragmentLines(const QString &source, int offset, int length)
{
    QJsonArray lines;
    const int start = qBound(0, offset, source.size());
    const int end = qBound(start, start + qMax(0, length), source.size());
    if (start >= end) return lines;
    int line = lineNumberAt(source, start);
    int cursor = start;
    while (cursor < end) {
        int newline = source.indexOf(QLatin1Char('\n'), cursor);
        if (newline < 0 || newline >= end) newline = end;
        lines.append(QJsonObject {
            { QStringLiteral("line"), line },
            { QStringLiteral("text"), source.mid(cursor, newline - cursor) }
        });
        cursor = newline + 1;
        ++line;
        if (newline >= end) break;
    }
    return lines;
}

void addFragmentLineMetadata(QJsonObject *object, const QString &source, int offset, int length)
{
    if (!object) return;
    const int start = qBound(0, offset, source.size());
    const int end = qBound(start, start + qMax(0, length), source.size());
    object->insert(QStringLiteral("start_line"), lineNumberAt(source, start));
    object->insert(QStringLiteral("end_line"), lineNumberAt(source, qMax(start, end - 1)));
    object->insert(QStringLiteral("lines"), fragmentLines(source, start, end - start));
}

bool rangeSplitsMarkup(const QString &source, int start, int end)
{
    if (start < 0 || end < start || end > source.size()) return true;
    int cursor = 0;
    while (cursor < source.size()) {
        const int open = source.indexOf(QLatin1Char('<'), cursor);
        if (open < 0) break;
        int close = source.indexOf(QLatin1Char('>'), open);
        if (close < 0) close = source.size() - 1;
        const bool starts_inside = start > open && start <= close;
        const bool ends_inside = end > open && end <= close;
        if (starts_inside || ends_inside) return true;
        cursor = close + 1;
    }
    return false;
}

PatchRangeResolution resolvePatchRange(const QString &source,
                                       int start,
                                       int end,
                                       const QString &expected_text,
                                       int start_line)
{
    PatchRangeResolution result;
    if (expected_text.isEmpty()) {
        result.code = QStringLiteral("PATCH_EXPECTED_TEXT_REQUIRED");
        result.message = QStringLiteral(
            "resource.patch_fragment requires expected_text copied from resource.read_fragment text. "
            "Do not invent character offsets. If the substring appears more than once, pass start_line.");
        result.data = mismatchData(source, start, end, expected_text);
        return result;
    }
    if (expected_text.size() > MAX_PATCH_FRAGMENT_LENGTH) {
        result.code = QStringLiteral("PATCH_EXPECTED_TEXT_TOO_LARGE");
        result.message = QStringLiteral(
            "expected_text is capped at 8192 UTF-16 units. Read and patch a smaller fragment.");
        result.data = QJsonObject {
            { QStringLiteral("expected_text_length"), expected_text.size() },
            { QStringLiteral("max_expected_text_length"),
              MAX_PATCH_FRAGMENT_LENGTH }
        };
        return result;
    }

    int chosen = -1;
    const bool range_matches = start >= 0 && end >= start && end <= source.size()
        && sliceAt(source, start, end) == expected_text;

    if (range_matches && start_line < 1) {
        chosen = start;
    } else {
        const HitProbe hits = probeHits(source, expected_text);
        if (hits.count == 1) {
            chosen = hits.first;
            result.rangeCorrected = !(range_matches && start == chosen);
        } else if (hits.count == 0) {
            result.code = QStringLiteral("PATCH_TEXT_NOT_FOUND");
            result.message = QStringLiteral(
                "expected_text was not found. Re-read the fragment and copy text exactly; do not guess offsets.");
            result.data = mismatchData(source, start, end, expected_text);
            return result;
        } else {
            if (start_line >= 1) {
                int line_start = 0;
                int line_end = 0;
                const HitProbe on_line = sourceRangeForLine(
                        source, start_line, &line_start, &line_end)
                    ? probeHits(source, expected_text, line_start, line_end)
                    : HitProbe();
                if (on_line.count == 1) {
                    chosen = on_line.first;
                    result.rangeCorrected = !range_matches
                        || start != chosen;
                } else if (on_line.count == 0) {
                    result.code = QStringLiteral("PATCH_TEXT_NOT_FOUND");
                    result.message = QStringLiteral(
                        "expected_text was not found on start_line. Use the line number from read_fragment.lines.");
                    result.data = mismatchData(source, start, end, expected_text);
                    result.data.insert(QStringLiteral("start_line"), start_line);
                    return result;
                } else {
                    result.code = QStringLiteral("PATCH_TEXT_AMBIGUOUS");
                    result.message = QStringLiteral(
                        "expected_text occurs more than once on that line. Copy a longer unique substring.");
                    result.data = mismatchData(source, start, end, expected_text);
                    result.data.insert(QStringLiteral("start_line"), start_line);
                    return result;
                }
            } else {
                result.code = QStringLiteral("PATCH_TEXT_AMBIGUOUS");
                result.message = QStringLiteral(
                    "expected_text occurs more than once. Pass start_line from read_fragment.lines to pick one occurrence.");
                result.data = mismatchData(source, start, end, expected_text);
                return result;
            }
        }
    }

    result.start = chosen;
    result.end = chosen + expected_text.size();

    if (rangeSplitsMarkup(source, result.start, result.end)) {
        result.code = QStringLiteral("PATCH_SPLITS_MARKUP");
        result.message = QStringLiteral(
            "The match cuts through a markup tag. Replace a complete tag, text node, or whole line, not a partial tag.");
        result.data = mismatchData(source, result.start, result.end, expected_text);
        result.data.insert(QStringLiteral("split_start"), result.start);
        result.data.insert(QStringLiteral("split_end"), result.end);
        result.data.insert(QStringLiteral("line"), lineNumberAt(source, result.start));
        return result;
    }

    result.ok = true;
    result.data = QJsonObject {
        { QStringLiteral("start"), result.start },
        { QStringLiteral("end"), result.end },
        { QStringLiteral("line"), lineNumberAt(source, result.start) },
        { QStringLiteral("range_corrected"), result.rangeCorrected },
        { QStringLiteral("replaced_length"), result.end - result.start }
    };
    return result;
}

} // namespace SigilAgent
