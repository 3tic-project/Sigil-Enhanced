#include "ViewEditors/CodeViewSelectionPolicy.h"

#include <algorithm>

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "Parsers/TagLister.h"

namespace {

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
