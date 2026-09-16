#include "ViewEditors/CodeViewSelectionPolicy.h"

#include <algorithm>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringView>
#include <QTextBoundaryFinder>

#include "Parsers/TagLister.h"

namespace {

constexpr int kSentenceProjectionRadius = 65536;

QString LocalName(const QString &name)
{
    const qsizetype separator = name.lastIndexOf(QLatin1Char(':'));
    return (separator >= 0 ? name.mid(separator + 1) : name).toLower();
}

bool IsUnsafeContainer(const QString &name)
{
    static const QSet<QString> unsafe {
        QStringLiteral("code"), QStringLiteral("math"), QStringLiteral("pre"),
        QStringLiteral("script"), QStringLiteral("style"), QStringLiteral("svg")
    };
    return unsafe.contains(LocalName(name));
}

bool IsTextUnit(const QString &name)
{
    const QString local_name = LocalName(name);
    static const QSet<QString> units {
        QStringLiteral("blockquote"), QStringLiteral("dd"), QStringLiteral("div"),
        QStringLiteral("dt"), QStringLiteral("h1"), QStringLiteral("h2"),
        QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"),
        QStringLiteral("h6"), QStringLiteral("li"), QStringLiteral("p")
    };
    return units.contains(local_name);
}

bool IsStructuralBlock(const QString &name)
{
    static const QSet<QString> blocks {
        QStringLiteral("address"), QStringLiteral("article"), QStringLiteral("aside"),
        QStringLiteral("blockquote"), QStringLiteral("details"), QStringLiteral("dialog"),
        QStringLiteral("div"), QStringLiteral("dl"), QStringLiteral("fieldset"),
        QStringLiteral("figure"), QStringLiteral("footer"), QStringLiteral("form"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("header"), QStringLiteral("hgroup"), QStringLiteral("hr"),
        QStringLiteral("li"), QStringLiteral("main"), QStringLiteral("nav"),
        QStringLiteral("ol"), QStringLiteral("p"), QStringLiteral("pre"),
        QStringLiteral("section"), QStringLiteral("table"), QStringLiteral("ul")
    };
    return blocks.contains(LocalName(name));
}

bool HasStructuralDescendant(TagLister &tags, int open_index, int close_index)
{
    for (int index = open_index + 1; index < close_index; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if ((tag.ttype == QLatin1String("begin") || tag.ttype == QLatin1String("single"))
            && IsStructuralBlock(tag.tname)) {
            return true;
        }
    }
    return false;
}

bool HasVisibleSourceText(const QString &source, TagLister &tags,
                          int open_index, int close_index, int start, int end)
{
    auto range_has_text = [&](int range_start, int range_end) {
        for (int position = range_start; position < range_end; ++position) {
            if (!source.at(position).isSpace()) return true;
        }
        return false;
    };

    int text_start = start;
    for (int index = open_index + 1; index < close_index; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.pos < text_start || tag.pos >= end || tag.len <= 0) continue;
        if (range_has_text(text_start, tag.pos)) return true;
        text_start = std::min(end, tag.pos + tag.len);
    }
    return range_has_text(text_start, end);
}

struct TextProjection {
    QString text;
    QList<int> sourceStarts;
    QList<int> sourceEnds;
    int sourceRangeStart = 0;
    int sourceRangeEnd = 0;
};

QString DecodeEntity(const QStringView entity)
{
    if (entity.size() < 3 || entity.front() != QLatin1Char('&')
        || entity.back() != QLatin1Char(';')) {
        return QString();
    }
    const QString body = entity.sliced(1, entity.size() - 2).toString();
    if (body.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        const bool hexadecimal = body.size() > 2
            && (body.at(1) == QLatin1Char('x')
                || body.at(1) == QLatin1Char('X'));
        const uint value = body.mid(hexadecimal ? 2 : 1).toUInt(
            &ok, hexadecimal ? 16 : 10);
        if (ok && value > 0 && value <= 0x10ffff
            && !(value >= 0xd800 && value <= 0xdfff)) {
            const char32_t scalar = static_cast<char32_t>(value);
            return QString::fromUcs4(&scalar, 1);
        }
    }
    static const QHash<QString, QString> named {
        { QStringLiteral("amp"), QStringLiteral("&") },
        { QStringLiteral("apos"), QStringLiteral("'") },
        { QStringLiteral("gt"), QStringLiteral(">") },
        { QStringLiteral("hellip"), QString(QChar(0x2026)) },
        { QStringLiteral("ldquo"), QString(QChar(0x201c)) },
        { QStringLiteral("lsquo"), QString(QChar(0x2018)) },
        { QStringLiteral("lt"), QStringLiteral("<") },
        { QStringLiteral("mdash"), QString(QChar(0x2014)) },
        { QStringLiteral("nbsp"), QString(QChar(0x00a0)) },
        { QStringLiteral("ndash"), QString(QChar(0x2013)) },
        { QStringLiteral("quot"), QStringLiteral("\"") },
        { QStringLiteral("rdquo"), QString(QChar(0x201d)) },
        { QStringLiteral("rsquo"), QString(QChar(0x2019)) }
    };
    const auto found = named.constFind(body);
    return found == named.cend() ? QString() : found.value();
}

void AppendProjectionText(TextProjection *projection, const QString &text,
                          int source_start, int source_end)
{
    if (!projection) return;
    projection->text += text;
    for (int index = 0; index < text.size(); ++index) {
        projection->sourceStarts.append(source_start);
        projection->sourceEnds.append(source_end);
    }
}

void AppendVisibleRange(TextProjection *projection, const QString &source,
                        int start, int end)
{
    int position = start;
    while (position < end) {
        if (source.at(position) == QLatin1Char('&')) {
            const int semicolon = source.indexOf(QLatin1Char(';'), position + 1);
            if (semicolon > position && semicolon < end
                && semicolon - position <= 64) {
                const QString decoded = DecodeEntity(
                    QStringView(source).sliced(position,
                                               semicolon - position + 1));
                AppendProjectionText(
                    projection,
                    decoded.isEmpty() ? QString(QChar(0xfffc)) : decoded,
                    position, semicolon + 1);
                position = semicolon + 1;
                continue;
            }
        }
        AppendProjectionText(projection, source.mid(position, 1),
                             position, position + 1);
        ++position;
    }
}

TextProjection BuildProjection(const QString &source, TagLister &tags,
                               int start, int end)
{
    TextProjection projection;
    projection.sourceRangeStart = start;
    projection.sourceRangeEnd = end;
    int source_position = start;
    const int tag_count = std::max(0, static_cast<int>(tags.size()) - 1);
    for (int index = 0; index < tag_count && source_position < end; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.pos + tag.len <= source_position || tag.pos >= end) continue;
        if (tag.pos > source_position) {
            AppendVisibleRange(&projection, source, source_position,
                               std::min(tag.pos, end));
        }
        source_position = std::max(
            source_position, std::min(end, tag.pos + tag.len));
    }
    if (source_position < end) {
        AppendVisibleRange(&projection, source, source_position, end);
    }
    return projection;
}

int ProjectionPositionForSource(const TextProjection &projection,
                                int source_position)
{
    for (int index = 0; index < projection.text.size(); ++index) {
        if (source_position >= projection.sourceStarts.at(index)
            && source_position < projection.sourceEnds.at(index)) {
            return index;
        }
    }
    return -1;
}

bool FindSentenceBounds(const QString &text, int position,
                        int *sentence_start, int *sentence_end)
{
    if (position < 0 || position >= text.size()) return false;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);
    QList<int> boundaries { 0 };
    finder.toStart();
    int boundary = -1;
    while ((boundary = finder.toNextBoundary()) >= 0) {
        if (boundaries.constLast() != boundary) boundaries.append(boundary);
    }
    if (boundaries.constLast() != text.size()) boundaries.append(text.size());

    int start = -1;
    int end = -1;
    for (int index = 0; index + 1 < boundaries.size(); ++index) {
        if (position >= boundaries.at(index)
            && position < boundaries.at(index + 1)) {
            start = boundaries.at(index);
            end = boundaries.at(index + 1);
            break;
        }
    }
    if (start < 0 || end <= start) return false;
    while (start < end && text.at(start).isSpace()) ++start;
    while (end > start && text.at(end - 1).isSpace()) --end;
    if (position < start || position >= end) return false;
    *sentence_start = start;
    *sentence_end = end;
    return true;
}

bool HasUnsafeDescendant(TagLister &tags, int start, int end)
{
    const int tag_count = std::max(0, static_cast<int>(tags.size()) - 1);
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.pos < start || tag.pos >= end) continue;
        if ((tag.ttype == QLatin1String("begin")
             || tag.ttype == QLatin1String("single"))
            && IsUnsafeContainer(tag.tname)) {
            return true;
        }
    }
    return false;
}

bool HasUnbalancedMarkup(TagLister &tags, int unit_start, int unit_end,
                         int selection_start, int selection_end)
{
    const int tag_count = std::max(0, static_cast<int>(tags.size()) - 1);
    QHash<int, int> close_by_open_position;
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.ttype == QLatin1String("end") && tag.open_pos >= 0) {
            close_by_open_position.insert(tag.open_pos, index);
        }
    }
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &open_tag = tags.at(index);
        if (open_tag.ttype != QLatin1String("begin")
            || open_tag.pos < unit_start || open_tag.pos >= unit_end) {
            continue;
        }
        const auto close = close_by_open_position.constFind(open_tag.pos);
        if (close == close_by_open_position.cend()) continue;
        const TagLister::TagInfo &close_tag = tags.at(close.value());
        const bool includes_open = selection_start <= open_tag.pos
            && selection_end >= open_tag.pos + open_tag.len;
        const bool includes_close = selection_start <= close_tag.pos
            && selection_end >= close_tag.pos + close_tag.len;
        if (includes_open != includes_close) return true;
    }
    return false;
}

void ExpandFullySelectedMarkup(TagLister &tags,
                               const TextProjection &projection,
                               int unit_start, int unit_end,
                               int projected_start, int projected_end,
                               int *selection_start, int *selection_end)
{
    const int tag_count = std::max(0, static_cast<int>(tags.size()) - 1);
    QList<int> next_nonspace(projection.text.size() + 1, -1);
    int next = -1;
    for (int projected = projection.text.size() - 1; projected >= 0; --projected) {
        if (!projection.text.at(projected).isSpace()) next = projected;
        next_nonspace[projected] = next;
    }
    QList<int> previous_nonspace(projection.text.size(), -1);
    int previous = -1;
    for (int projected = 0; projected < projection.text.size(); ++projected) {
        if (!projection.text.at(projected).isSpace()) previous = projected;
        previous_nonspace[projected] = previous;
    }
    QHash<int, int> close_by_open_position;
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.ttype == QLatin1String("end") && tag.open_pos >= 0) {
            close_by_open_position.insert(tag.open_pos, index);
        }
    }
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &open_tag = tags.at(index);
        if (open_tag.ttype != QLatin1String("begin")
            || open_tag.pos < unit_start || open_tag.pos >= unit_end) {
            continue;
        }
        const auto close = close_by_open_position.constFind(open_tag.pos);
        if (close == close_by_open_position.cend()) continue;
        const TagLister::TagInfo &close_tag = tags.at(close.value());
        const int content_start = open_tag.pos + open_tag.len;
        const int content_end = close_tag.pos;
        if (content_start < projection.sourceRangeStart
            || content_end > projection.sourceRangeEnd) {
            continue;
        }
        const int projected_begin = static_cast<int>(std::distance(
            projection.sourceEnds.cbegin(),
            std::upper_bound(projection.sourceEnds.cbegin(),
                             projection.sourceEnds.cend(), content_start)));
        const int element_projected_end = static_cast<int>(std::distance(
            projection.sourceStarts.cbegin(),
            std::lower_bound(projection.sourceStarts.cbegin(),
                             projection.sourceStarts.cend(), content_end)));
        if (projected_begin >= element_projected_end) continue;
        const int first_visible = next_nonspace.at(projected_begin);
        const int last_visible = previous_nonspace.at(element_projected_end - 1);
        if (first_visible < projected_begin
            || first_visible >= element_projected_end
            || last_visible < projected_begin
            || projected_start > first_visible
            || projected_end <= last_visible) {
            continue;
        }
        *selection_start = std::min(*selection_start, open_tag.pos);
        *selection_end = std::max(
            *selection_end, close_tag.pos + close_tag.len);
    }
}

}

CodeViewSelectionPolicy::Result CodeViewSelectionPolicy::FindTextUnit(
    const QString &source, TagLister &tags, int position)
{
    Result result;
    if (position < 0 || position >= source.size()) {
        result.reason = Reason::OutsideDocument;
        return result;
    }

    const int tag_count = std::max(0, static_cast<int>(tags.size()) - 1);
    QHash<int, int> close_by_open_position;
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.pos >= 0 && tag.len > 0
            && position >= tag.pos && position < tag.pos + tag.len) {
            result.reason = Reason::InMarkup;
            return result;
        }
        if (tag.ttype == QLatin1String("end") && tag.open_pos >= 0) {
            close_by_open_position.insert(tag.open_pos, index);
        }
    }

    struct Candidate {
        int openIndex;
        int closeIndex;
    };
    QList<Candidate> containing;
    for (int index = 0; index < tag_count; ++index) {
        const TagLister::TagInfo &tag = tags.at(index);
        if (tag.ttype != QLatin1String("begin")) continue;
        const auto close = close_by_open_position.constFind(tag.pos);
        if (close == close_by_open_position.cend()) continue;
        const TagLister::TagInfo &close_tag = tags.at(close.value());
        if (position >= tag.pos + tag.len && position < close_tag.pos) {
            if (IsUnsafeContainer(tag.tname)) {
                result.reason = Reason::UnsafeContainer;
                return result;
            }
            containing.append(Candidate { index, close.value() });
        }
    }

    for (auto candidate = containing.crbegin(); candidate != containing.crend(); ++candidate) {
        const TagLister::TagInfo &open_tag = tags.at(candidate->openIndex);
        if (!IsTextUnit(open_tag.tname)) continue;
        if (HasStructuralDescendant(tags, candidate->openIndex, candidate->closeIndex)) continue;

        const TagLister::TagInfo &close_tag = tags.at(candidate->closeIndex);
        const int start = open_tag.pos + open_tag.len;
        const int end = close_tag.pos;
        if (!HasVisibleSourceText(source, tags, candidate->openIndex,
                                  candidate->closeIndex, start, end)) {
            result.reason = Reason::EmptyContent;
            return result;
        }
        result.start = start;
        result.end = end;
        result.outerStart = open_tag.pos;
        result.outerEnd = close_tag.pos + close_tag.len;
        result.reason = Reason::ElementContent;
        return result;
    }
    return result;
}

CodeViewSelectionPolicy::Result CodeViewSelectionPolicy::FindSentence(
    const QString &source, TagLister &tags, int position)
{
    Result unit = FindTextUnit(source, tags, position);
    if (!unit.hasSelection()) return unit;
    if (HasUnsafeDescendant(tags, unit.start, unit.end)) {
        Result result;
        result.reason = Reason::UnsafeContainer;
        return result;
    }

    const int projection_start = std::max(
        unit.start, position - kSentenceProjectionRadius);
    const int projection_end = std::min(
        unit.end, position + kSentenceProjectionRadius);
    const TextProjection projection = BuildProjection(
        source, tags, projection_start, projection_end);
    const int projected_position = ProjectionPositionForSource(
        projection, position);
    int sentence_start = -1;
    int sentence_end = -1;
    if (projected_position < 0
        || !FindSentenceBounds(projection.text, projected_position,
                               &sentence_start, &sentence_end)) {
        Result result;
        result.reason = Reason::NoSentence;
        return result;
    }
    if ((sentence_start == 0 && projection_start > unit.start)
        || (sentence_end == projection.text.size()
            && projection_end < unit.end)) {
        Result result;
        result.reason = Reason::NoSentence;
        return result;
    }

    int source_start = projection.sourceStarts.at(sentence_start);
    int source_end = projection.sourceEnds.at(sentence_end - 1);
    ExpandFullySelectedMarkup(tags, projection, unit.start, unit.end,
                              sentence_start, sentence_end,
                              &source_start, &source_end);
    if (HasUnbalancedMarkup(tags, unit.start, unit.end,
                            source_start, source_end)) {
        unit.reason = Reason::SentenceFallbackElement;
        return unit;
    }
    unit.start = source_start;
    unit.end = source_end;
    unit.reason = Reason::Sentence;
    return unit;
}
