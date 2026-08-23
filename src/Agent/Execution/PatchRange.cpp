/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/PatchRange.h"

#include <QJsonArray>
#include <QList>

namespace SigilAgent
{

namespace
{

QString sliceAt(const QString &source, int start, int end)
{
    if (start < 0 || end < start || end > source.size()) return QString();
    return source.mid(start, end - start);
}

QJsonObject mismatchData(const QString &source, int start, int end, const QString &expected)
{
    const QString actual = sliceAt(source, start, end);
    const int context = 40;
    const int from = qMax(0, start - context);
    QJsonObject data {
        { QStringLiteral("start"), start },
        { QStringLiteral("end"), end },
        { QStringLiteral("total"), source.size() },
        { QStringLiteral("expected_text"), expected },
        { QStringLiteral("actual_text"), actual },
        { QStringLiteral("context"), source.mid(from, qMin(source.size() - from, (end - start) + context * 2)) }
    };
    QJsonArray occurrences;
    int from_index = 0;
    while (!expected.isEmpty()) {
        const int at = source.indexOf(expected, from_index);
        if (at < 0) break;
        occurrences.append(QJsonObject {
            { QStringLiteral("start"), at },
            { QStringLiteral("end"), at + expected.size() }
        });
        from_index = at + qMax(1, expected.size());
        if (occurrences.size() >= 20) break;
    }
    data.insert(QStringLiteral("occurrences"), occurrences);
    return data;
}

} // namespace

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
                                       const QString &expected_text)
{
    PatchRangeResolution result;
    const bool insertion = expected_text.isEmpty() && start == end
        && start >= 0 && start <= source.size();
    if (expected_text.isEmpty() && !insertion) {
        result.code = QStringLiteral("PATCH_EXPECTED_TEXT_REQUIRED");
        result.message = QStringLiteral(
            "resource.patch_fragment requires expected_text equal to the exact current substring "
            "being replaced. Copy it from resource.read_fragment; do not guess offsets.");
        result.data = mismatchData(source, start, end, expected_text);
        return result;
    }

    const bool range_in_bounds = start >= 0 && end >= start && end <= source.size();
    const bool range_matches = range_in_bounds && sliceAt(source, start, end) == expected_text;
    if (range_matches || insertion) {
        result.start = start;
        result.end = end;
    } else {
        QList<int> hits;
        int from = 0;
        while (!expected_text.isEmpty()) {
            const int at = source.indexOf(expected_text, from);
            if (at < 0) break;
            hits.append(at);
            from = at + expected_text.size();
        }
        if (hits.size() == 1) {
            result.start = hits.first();
            result.end = result.start + expected_text.size();
            result.rangeCorrected = true;
        } else if (hits.isEmpty()) {
            result.code = QStringLiteral("PATCH_TEXT_NOT_FOUND");
            result.message = QStringLiteral(
                "expected_text was not found in the resource. Re-read the fragment and copy the substring exactly.");
            result.data = mismatchData(source, start, end, expected_text);
            return result;
        } else {
            result.code = QStringLiteral("PATCH_TEXT_AMBIGUOUS");
            result.message = QStringLiteral(
                "expected_text occurs more than once. Pass start/end that exactly cover one occurrence.");
            result.data = mismatchData(source, start, end, expected_text);
            return result;
        }
    }

    if (rangeSplitsMarkup(source, result.start, result.end)) {
        result.code = QStringLiteral("PATCH_SPLITS_MARKUP");
        result.message = QStringLiteral(
            "The patch range cuts through a markup tag. Replace a complete tag or a text node, not a partial tag.");
        result.data = mismatchData(source, result.start, result.end, expected_text);
        result.data.insert(QStringLiteral("split_start"), result.start);
        result.data.insert(QStringLiteral("split_end"), result.end);
        return result;
    }

    result.ok = true;
    result.data = QJsonObject {
        { QStringLiteral("start"), result.start },
        { QStringLiteral("end"), result.end },
        { QStringLiteral("range_corrected"), result.rangeCorrected },
        { QStringLiteral("replaced_length"), result.end - result.start }
    };
    return result;
}

} // namespace SigilAgent
