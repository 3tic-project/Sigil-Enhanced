/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/UI/AgentLocations.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

#include "Agent/UI/AgentMarkdown.h"

namespace SigilAgent
{

namespace
{

struct Span {
    int start = 0;
    int end = 0;
};

enum class MentionKind {
    PathText,
    PathCode,
    LineRef
};

struct Mention {
    int start = 0;
    int end = 0;
    MentionKind kind = MentionKind::PathText;
    int resource = -1;
    int line = -1;
};

struct ScannedLine {
    bool tableRow = false;
    QList<Mention> mentions;
};

struct ResourceState {
    bool loaded = false;
    bool available = false;
    int lineCount = 0;
    QByteArray sha256;
};

bool isAsciiWord(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
        || (u >= '0' && u <= '9') || u == '_';
}

bool isPathContinuation(QChar c)
{
    return isAsciiWord(c) || c == QLatin1Char('/') || c == QLatin1Char('-')
        || c == QLatin1Char('%');
}

bool pathBoundaryOk(const QString &line, int start, int end)
{
    if (start > 0) {
        const QChar before = line.at(start - 1);
        if (isPathContinuation(before) || before == QLatin1Char('.')) return false;
    }
    if (end < line.size()) {
        const QChar after = line.at(end);
        if (isPathContinuation(after)) return false;
        if (after == QLatin1Char('.') && end + 1 < line.size()
            && isAsciiWord(line.at(end + 1))) {
            return false;
        }
    }
    return true;
}

bool overlaps(const Span &span, int start, int end)
{
    return start < span.end && span.start < end;
}

bool insideAny(const QList<Span> &spans, int start, int end)
{
    for (const Span &span : spans) {
        if (start >= span.start && end <= span.end) return true;
    }
    return false;
}

bool overlapsAny(const QList<Span> &spans, int start, int end)
{
    for (const Span &span : spans) {
        if (overlaps(span, start, end)) return true;
    }
    return false;
}

bool isTableDivider(const QString &line)
{
    QStringList cells = line.trimmed().split(QLatin1Char('|'));
    if (!cells.isEmpty() && cells.first().trimmed().isEmpty()) cells.removeFirst();
    if (!cells.isEmpty() && cells.last().trimmed().isEmpty()) cells.removeLast();
    if (cells.size() < 2) return false;
    static const QRegularExpression cell(QStringLiteral("^:?-{3,}:?$"));
    for (const QString &value : cells) {
        if (!cell.match(value.trimmed()).hasMatch()) return false;
    }
    return true;
}

QList<Span> codeSpans(const QString &line)
{
    QList<Span> spans;
    int i = 0;
    while (i < line.size()) {
        if (line.at(i) == QLatin1Char('\\') && i + 1 < line.size()) {
            i += 2;
            continue;
        }
        if (line.at(i) != QLatin1Char('`')) {
            ++i;
            continue;
        }
        int run = 0;
        while (i + run < line.size() && line.at(i + run) == QLatin1Char('`')) ++run;
        int search = i + run;
        int close = -1;
        while (search < line.size()) {
            const int at = line.indexOf(QLatin1Char('`'), search);
            if (at < 0) break;
            int other = 0;
            while (at + other < line.size() && line.at(at + other) == QLatin1Char('`')) ++other;
            if (other == run) {
                close = at;
                break;
            }
            search = at + other;
        }
        if (close < 0) {
            i += run;
            continue;
        }
        spans.append(Span { i, close + run });
        i = close + run;
    }
    return spans;
}

QList<Span> linkLikeSpans(const QString &line)
{
    static const QList<QRegularExpression> patterns {
        QRegularExpression(QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^)\\n]*\\)")),
        QRegularExpression(QStringLiteral("<[A-Za-z][A-Za-z0-9+.\\-]{1,31}:[^<>\\s]*>")),
        QRegularExpression(QStringLiteral(
            "(?:[A-Za-z][A-Za-z0-9+.\\-]{1,31}://|www\\.)[^\\s<>]*"))
    };
    QList<Span> spans;
    for (const QRegularExpression &pattern : patterns) {
        auto it = pattern.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            spans.append(Span { static_cast<int>(match.capturedStart()),
                                static_cast<int>(match.capturedEnd()) });
        }
    }
    return spans;
}

QString escapeLinkText(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('[') || c == QLatin1Char(']') || c == QLatin1Char('\\')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(c);
    }
    return escaped;
}

QByteArray sha256Of(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

int sourceLineCount(const QString &text)
{
    return static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
}

} // namespace

QString agentLocationStatusName(AgentLocationStatus status)
{
    switch (status) {
        case AgentLocationStatus::Exact: return QStringLiteral("exact");
        case AgentLocationStatus::ContentChanged: return QStringLiteral("content_changed");
        case AgentLocationStatus::ResourceMissing: return QStringLiteral("resource_missing");
        case AgentLocationStatus::OtherBook: return QStringLiteral("other_book");
        case AgentLocationStatus::Unavailable: return QStringLiteral("unavailable");
        case AgentLocationStatus::Unknown: break;
    }
    return QStringLiteral("unknown");
}

AgentLinkifyResult AgentLocationTable::linkify(const QString &markdown,
                                               const AgentLocationSource &source)
{
    AgentLinkifyResult result;
    result.markdown = markdown;
    const QString book_session_id = source.bookSessionId();
    if (book_session_id.isEmpty() || markdown.isEmpty()) return result;

    QList<AgentLocationResource> resources = source.textResources();
    resources.erase(std::remove_if(resources.begin(), resources.end(),
                                   [](const AgentLocationResource &resource) {
                                       return resource.resourceId.isEmpty()
                                           || resource.bookPath.isEmpty();
                                   }),
                    resources.end());
    if (resources.isEmpty()) return result;
    QHash<QString, int> by_path;
    for (int i = 0; i < resources.size(); ++i) by_path.insert(resources.at(i).bookPath, i);
    QList<int> by_length;
    for (int i = 0; i < resources.size(); ++i) by_length.append(i);
    std::sort(by_length.begin(), by_length.end(), [&resources](int a, int b) {
        return resources.at(a).bookPath.size() > resources.at(b).bookPath.size();
    });

    static const QRegularExpression fence_open(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression line_ref(
        QStringLiteral("(?<![A-Za-z0-9_])L([1-9][0-9]{0,6})(?![A-Za-z0-9_])"));

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QList<ScannedLine> scanned;
    scanned.reserve(lines.size());
    QSet<int> message_resources;
    QChar fence_char;
    int fence_length = 0;
    bool table_rows = false;
    for (const QString &line : lines) {
        ScannedLine scan;
        if (fence_length > 0) {
            static const QRegularExpression fence_close(QStringLiteral("^ {0,3}(`{3,}|~{3,})\\s*$"));
            const QRegularExpressionMatch close = fence_close.match(line);
            if (close.hasMatch() && close.captured(1).at(0) == fence_char
                && close.captured(1).size() >= fence_length) {
                fence_length = 0;
            }
            scanned.append(scan);
            table_rows = false;
            continue;
        }
        const QRegularExpressionMatch open = fence_open.match(line);
        if (open.hasMatch()) {
            fence_char = open.captured(1).at(0);
            fence_length = static_cast<int>(open.captured(1).size());
            scanned.append(scan);
            table_rows = false;
            continue;
        }
        // Indented Markdown code must not supply the sole file context for a
        // line reference elsewhere in the answer.
        if (line.startsWith(QLatin1Char('\t'))
            || line.startsWith(QStringLiteral("    "))) {
            scanned.append(scan);
            table_rows = false;
            continue;
        }
        const bool divider = isTableDivider(line);
        if (divider && !scanned.isEmpty() && lines.at(scanned.size() - 1).contains(QLatin1Char('|'))) {
            scanned.last().tableRow = true;
            table_rows = true;
        } else if (table_rows && !line.contains(QLatin1Char('|'))) {
            table_rows = false;
        }
        scan.tableRow = line.trimmed().startsWith(QLatin1Char('|'))
            || (table_rows && line.contains(QLatin1Char('|')));

        const QList<Span> links = linkLikeSpans(line);
        const QList<Span> codes = codeSpans(line);
        QList<Span> protected_spans = links;
        for (const Span &code : codes) {
            protected_spans.append(code);
            if (overlapsAny(links, code.start, code.end)) continue;
            const QString content = line.mid(code.start, code.end - code.start)
                                        .remove(QLatin1Char('`')).trimmed();
            const auto hit = by_path.constFind(content);
            if (hit == by_path.constEnd()) continue;
            scan.mentions.append(Mention { code.start, code.end, MentionKind::PathCode,
                                           hit.value(), -1 });
        }

        QList<Span> taken = protected_spans;
        for (int index : by_length) {
            const QString &path = resources.at(index).bookPath;
            int from = 0;
            while (true) {
                const int at = line.indexOf(path, from);
                if (at < 0) break;
                const int end = at + static_cast<int>(path.size());
                from = at + 1;
                if (overlapsAny(taken, at, end) || !pathBoundaryOk(line, at, end)) continue;
                scan.mentions.append(Mention { at, end, MentionKind::PathText, index, -1 });
                taken.append(Span { at, end });
            }
        }

        auto it = line_ref.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int start = static_cast<int>(match.capturedStart());
            const int end = static_cast<int>(match.capturedEnd());
            if (overlapsAny(taken, start, end)) continue;
            scan.mentions.append(Mention { start, end, MentionKind::LineRef, -1,
                                           match.captured(1).toInt() });
        }
        for (const Mention &mention : scan.mentions) {
            if (mention.resource >= 0) message_resources.insert(mention.resource);
        }
        scanned.append(scan);
    }

    QHash<int, ResourceState> states;
    auto state_for = [&](int index) -> const ResourceState & {
        ResourceState &state = states[index];
        if (state.loaded) return state;
        state.loaded = true;
        QString path;
        QString text;
        if (source.resourceText(resources.at(index).resourceId, &path, &text)) {
            state.available = true;
            state.lineCount = sourceLineCount(text);
            state.sha256 = sha256Of(text);
        }
        return state;
    };

    const int message_resource = message_resources.size() == 1
        ? *message_resources.constBegin() : -1;
    QStringList output;
    output.reserve(lines.size());
    for (int row = 0; row < lines.size(); ++row) {
        const QString &line = lines.at(row);
        ScannedLine &scan = scanned[row];
        if (scan.mentions.isEmpty()) {
            output.append(line);
            continue;
        }
        std::sort(scan.mentions.begin(), scan.mentions.end(),
                  [](const Mention &a, const Mention &b) { return a.start < b.start; });
        int line_resource = message_resource;
        if (scan.tableRow) {
            QSet<int> row_resources;
            for (const Mention &mention : scan.mentions) {
                if (mention.resource >= 0) row_resources.insert(mention.resource);
            }
            if (row_resources.size() == 1) line_resource = *row_resources.constBegin();
            else if (row_resources.size() > 1) line_resource = -1;
        }
        QString rewritten;
        int cursor = 0;
        for (const Mention &mention : scan.mentions) {
            rewritten += line.mid(cursor, mention.start - cursor);
            const QString original = line.mid(mention.start, mention.end - mention.start);
            cursor = mention.end;
            if (mention.kind == MentionKind::LineRef) {
                if (line_resource < 0) {
                    ++result.unboundLineRefs;
                    rewritten += original;
                    continue;
                }
                const ResourceState &state = state_for(line_resource);
                if (!state.available || mention.line > state.lineCount) {
                    ++result.outOfRangeLineRefs;
                    rewritten += original;
                    continue;
                }
                const QString id = issue(book_session_id, resources.at(line_resource),
                                         AgentLocationKind::SourceLine, mention.line,
                                         state.sha256);
                rewritten += QStringLiteral("[%1](%2)").arg(original, agentLocationHref(id));
                ++result.lineLinks;
                continue;
            }
            const ResourceState &state = state_for(mention.resource);
            if (!state.available) {
                rewritten += original;
                continue;
            }
            const QString id = issue(book_session_id, resources.at(mention.resource),
                                     AgentLocationKind::File, -1, state.sha256);
            const QString text = mention.kind == MentionKind::PathCode
                ? original : escapeLinkText(original);
            rewritten += QStringLiteral("[%1](%2)").arg(text, agentLocationHref(id));
            ++result.fileLinks;
        }
        rewritten += line.mid(cursor);
        output.append(rewritten);
    }
    result.markdown = output.join(QLatin1Char('\n'));
    return result;
}

QString AgentLocationTable::issue(const QString &book_session_id,
                                  const AgentLocationResource &resource,
                                  AgentLocationKind kind,
                                  int line,
                                  const QByteArray &content_sha256)
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(book_session_id, resource.resourceId)
        .arg(static_cast<int>(kind))
        .arg(line)
        .arg(QString::fromLatin1(content_sha256.toHex()));
    const auto existing = m_idsByKey.constFind(key);
    if (existing != m_idsByKey.constEnd()) return existing.value();
    AgentLocation location;
    location.id = QUuid::createUuid().toString(QUuid::Id128);
    location.bookSessionId = book_session_id;
    location.resourceId = resource.resourceId;
    location.bookPath = resource.bookPath;
    location.kind = kind;
    location.line = line;
    location.contentSha256 = content_sha256;
    m_locations.insert(location.id, location);
    m_idsByKey.insert(key, location.id);
    return location.id;
}

const AgentLocation *AgentLocationTable::find(const QString &id) const
{
    const auto it = m_locations.constFind(id);
    return it == m_locations.constEnd() ? nullptr : &it.value();
}

AgentLocationCheck AgentLocationTable::check(const QString &id,
                                             const AgentLocationSource *source) const
{
    AgentLocationCheck check;
    const AgentLocation *location = find(id);
    if (!location) {
        check.status = m_revoked.contains(id)
            ? AgentLocationStatus::OtherBook : AgentLocationStatus::Unknown;
        return check;
    }
    check.location = *location;
    if (!source) {
        check.status = AgentLocationStatus::Unavailable;
        return check;
    }
    if (source->bookSessionId() != location->bookSessionId) {
        check.status = AgentLocationStatus::OtherBook;
        return check;
    }
    QString text;
    if (!source->resourceText(location->resourceId, &check.currentBookPath, &text)) {
        check.status = AgentLocationStatus::ResourceMissing;
        return check;
    }
    if (location->kind == AgentLocationKind::File) {
        check.status = AgentLocationStatus::Exact;
        return check;
    }
    check.status = sha256Of(text) == location->contentSha256
            && location->line <= sourceLineCount(text)
        ? AgentLocationStatus::Exact : AgentLocationStatus::ContentChanged;
    return check;
}

void AgentLocationTable::revokeOtherBooks(const QString &book_session_id)
{
    for (auto it = m_locations.begin(); it != m_locations.end();) {
        if (it.value().bookSessionId == book_session_id) {
            ++it;
            continue;
        }
        m_revoked.insert(it.key());
        it = m_locations.erase(it);
    }
    for (auto it = m_idsByKey.begin(); it != m_idsByKey.end();) {
        if (m_locations.contains(it.value())) ++it;
        else it = m_idsByKey.erase(it);
    }
}

void AgentLocationTable::clear()
{
    m_locations.clear();
    m_idsByKey.clear();
    m_revoked.clear();
}

} // namespace SigilAgent
