/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/ProofAudit.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QVector>

#include "Agent/Execution/IBookWorkspace.h"

namespace SigilAgent
{
namespace
{

constexpr int kRuleVersion = 2;
constexpr int kMaxIssues = 20000;
constexpr int kMaxPage = 50;
constexpr qint64 kMaxSourceUnits = 5'000'000;

struct Unit {
    QChar ch;
    int start = -1;
    int end = -1;
    bool cdata = false;
};

struct Frame {
    QString name;
    bool hidden = false;
};

struct Source {
    QString id;
    QString path;
    QString text;
};

QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString bookKey(IBookWorkspace *workspace)
{
    const QJsonObject metadata = workspace->metadata();
    QString identifier = metadata.value(QStringLiteral("identifier")).toString();
    for (const QJsonValue &value : metadata.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("name")).toString().endsWith(
                QLatin1String("identifier"), Qt::CaseInsensitive)) {
            identifier = entry.value(QStringLiteral("content")).toString();
            break;
        }
    }
    if (identifier.isEmpty()) identifier = QStringLiteral("session:") + workspace->bookSessionId();
    return digest(identifier.toUtf8());
}

QString recordsPath(const QString &key)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Sigil-Enhanced/NativeAgent/proof-decisions-")
        + key + QStringLiteral(".json");
}

QString configPath(const QString &key)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Sigil-Enhanced/NativeAgent/proof-config-")
        + key + QStringLiteral(".json");
}

QJsonObject loadConfig(const QString &key)
{
    QFile file(configPath(key));
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

bool saveConfig(const QString &key, const QJsonObject &config)
{
    const QString path = configPath(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

QHash<QString, QJsonObject> loadRecords(const QString &key)
{
    QHash<QString, QJsonObject> records;
    QFile file(recordsPath(key));
    if (!file.open(QIODevice::ReadOnly)) return records;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return records;
    const QJsonObject object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isObject()) records.insert(it.key(), it.value().toObject());
    }
    return records;
}

bool saveRecords(const QString &key, const QHash<QString, QJsonObject> &records)
{
    const QString path = recordsPath(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QJsonObject object;
    for (auto it = records.cbegin(); it != records.cend(); ++it) object.insert(it.key(), it.value());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

QString escapeXmlText(const QString &plain)
{
    QString escaped = plain;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return escaped;
}

bool validXmlText(const QString &plain)
{
    for (int i = 0; i < plain.size(); ++i) {
        const QChar ch = plain.at(i);
        const ushort u = ch.unicode();
        if (ch.isHighSurrogate()) {
            if (i + 1 >= plain.size() || !plain.at(++i).isLowSurrogate()) return false;
        } else if (ch.isLowSurrogate()
                   || (u < 0x20 && u != '\t' && u != '\n' && u != '\r')
                   || u == 0xfffe || u == 0xffff) {
            return false;
        }
    }
    return true;
}

QJsonObject effectiveConfig(const QJsonObject &config, const QString &path)
{
    const QJsonObject book = config.value(QStringLiteral("book")).toObject();
    const QJsonObject file = config.value(QStringLiteral("files")).toObject()
        .value(path).toObject();
    QSet<QString> allowedRepeats;
    QSet<QString> allowedTerms;
    QMap<QString, QJsonObject> variants;
    for (const QJsonObject &level : { book, file }) {
        for (const QJsonValue &value : level.value(QStringLiteral("allow_repeats")).toArray()) {
            if (value.isString()) allowedRepeats.insert(value.toString());
        }
        for (const QJsonValue &value : level.value(QStringLiteral("allowed_terms")).toArray()) {
            if (value.isString()) allowedTerms.insert(value.toString());
        }
        for (const QJsonValue &value : level.value(QStringLiteral("variant_pairs")).toArray()) {
            const QJsonObject pair = value.toObject();
            const QString observed = pair.value(QStringLiteral("observed")).toString();
            if (!observed.isEmpty()) variants.insert(observed, pair);
        }
    }
    QStringList repeats = allowedRepeats.values();
    QStringList terms = allowedTerms.values();
    std::sort(repeats.begin(), repeats.end());
    std::sort(terms.begin(), terms.end());
    QJsonArray pairs;
    for (auto it = variants.cbegin(); it != variants.cend(); ++it) pairs.append(it.value());
    return QJsonObject {
        { QStringLiteral("allow_repeats"), QJsonArray::fromStringList(repeats) },
        { QStringLiteral("allowed_terms"), QJsonArray::fromStringList(terms) },
        { QStringLiteral("variant_pairs"), pairs }
    };
}

bool isBlock(const QString &name)
{
    static const QSet<QString> blocks {
        QStringLiteral("p"), QStringLiteral("div"), QStringLiteral("section"),
        QStringLiteral("article"), QStringLiteral("aside"), QStringLiteral("blockquote"),
        QStringLiteral("li"), QStringLiteral("ul"), QStringLiteral("ol"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
        QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("tr"),
        QStringLiteral("td"), QStringLiteral("th")
    };
    return blocks.contains(name);
}

bool isSkipped(const QString &name)
{
    static const QSet<QString> skipped {
        QStringLiteral("head"), QStringLiteral("script"), QStringLiteral("style"),
        QStringLiteral("rt"), QStringLiteral("rp"), QStringLiteral("noscript"),
        QStringLiteral("svg"), QStringLiteral("math"), QStringLiteral("nav"),
        QStringLiteral("template")
    };
    return skipped.contains(name);
}

bool hiddenAttribute(const QString &tag)
{
    static const QRegularExpression hidden(
        QStringLiteral("\\s(?:hidden(?:\\s|=|/|>)|aria-hidden\\s*=\\s*['\"]?true(?:['\"\\s/>]|$))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression style(
        QStringLiteral("\\sstyle\\s*=\\s*(['\"])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    if (hidden.match(tag).hasMatch()) return true;
    const QRegularExpressionMatch match = style.match(tag);
    if (!match.hasMatch()) return false;
    const QString css = match.captured(2).toLower();
    static const QRegularExpression invisible(
        QStringLiteral("(?:^|;)\\s*(?:display\\s*:\\s*none|visibility\\s*:\\s*hidden)(?:\\s*!important)?\\s*(?:;|$)"));
    return invisible.match(css).hasMatch();
}

bool decodeEntity(const QString &entity, QString *decoded)
{
    static const QHash<QString, QString> named {
        { QStringLiteral("amp"), QStringLiteral("&") },
        { QStringLiteral("lt"), QStringLiteral("<") },
        { QStringLiteral("gt"), QStringLiteral(">") },
        { QStringLiteral("quot"), QStringLiteral("\"") },
        { QStringLiteral("apos"), QStringLiteral("'") },
        { QStringLiteral("nbsp"), QString(QChar(0x00a0)) },
        { QStringLiteral("hellip"), QString(QChar(0x2026)) },
        { QStringLiteral("mdash"), QString(QChar(0x2014)) },
        { QStringLiteral("ndash"), QString(QChar(0x2013)) }
    };
    if (named.contains(entity)) {
        *decoded = named.value(entity);
        return true;
    }
    if (!entity.startsWith(QLatin1Char('#'))) {
        const QString encoded = QStringLiteral("&") + entity + QLatin1Char(';');
        const QString html = QTextDocumentFragment::fromHtml(encoded).toPlainText();
        if (!html.isEmpty() && html != encoded) {
            *decoded = html;
            return true;
        }
        return false;
    }
    const bool hex = entity.size() > 2 && entity.at(1).toLower() == QLatin1Char('x');
    bool ok = false;
    const uint cp = entity.mid(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
    if (!ok || cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    const char32_t character = static_cast<char32_t>(cp);
    *decoded = QString::fromUcs4(&character, 1);
    return true;
}

void separator(QVector<Unit> *units)
{
    if (!units->isEmpty() && units->last().start >= 0) units->append(Unit {});
}

bool extractVisible(const QString &source, QVector<Unit> *units, QString *error)
{
    QVector<Frame> stack;
    bool body = false;
    for (int i = 0; i < source.size();) {
        if (source.at(i) == QLatin1Char('<')) {
            if (source.mid(i, 4) == QLatin1String("<!--")) {
                const int end = source.indexOf(QStringLiteral("-->"), i + 4);
                if (end < 0) { *error = QStringLiteral("Unclosed XHTML comment"); return false; }
                i = end + 3;
                continue;
            }
            if (source.mid(i, 9) == QLatin1String("<![CDATA[")) {
                const int end = source.indexOf(QStringLiteral("]]>") , i + 9);
                if (end < 0) { *error = QStringLiteral("Unclosed CDATA"); return false; }
                if (body && !stack.isEmpty() && !stack.last().hidden) {
                    for (int j = i + 9; j < end; ++j) units->append({ source.at(j), j, j + 1, true });
                }
                i = end + 3;
                continue;
            }
            int end = i + 1;
            QChar quote;
            while (end < source.size()) {
                const QChar ch = source.at(end);
                if (!quote.isNull()) {
                    if (ch == quote) quote = QChar();
                } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
                    quote = ch;
                } else if (ch == QLatin1Char('>')) break;
                ++end;
            }
            if (end >= source.size()) { *error = QStringLiteral("Unclosed XHTML tag"); return false; }
            const QString tag = source.mid(i, end - i + 1);
            i = end + 1;
            if (tag.startsWith(QLatin1String("<?")) || tag.startsWith(QLatin1String("<!"))) continue;
            const bool closing = tag.startsWith(QLatin1String("</"));
            const int nameStart = closing ? 2 : 1;
            int nameEnd = nameStart;
            while (nameEnd < tag.size() && !tag.at(nameEnd).isSpace()
                   && tag.at(nameEnd) != QLatin1Char('>')
                   && tag.at(nameEnd) != QLatin1Char('/')) ++nameEnd;
            QString name = tag.mid(nameStart, nameEnd - nameStart).toLower();
            name = name.section(QLatin1Char(':'), -1);
            if (name.isEmpty()) { *error = QStringLiteral("Malformed XHTML tag"); return false; }
            if (closing) {
                if (stack.isEmpty() || stack.last().name != name) {
                    *error = QStringLiteral("Mismatched XHTML close tag: %1").arg(name);
                    return false;
                }
                if (body && isBlock(name) && !stack.last().hidden) separator(units);
                stack.removeLast();
                if (name == QLatin1String("body")) body = false;
                continue;
            }
            const bool selfClosing = tag.left(tag.size() - 1).trimmed().endsWith(QLatin1Char('/'));
            const bool parentHidden = !stack.isEmpty() && stack.last().hidden;
            const bool hidden = parentHidden || isSkipped(name) || hiddenAttribute(tag);
            if (name == QLatin1String("body")) body = true;
            if (body && isBlock(name) && !hidden) separator(units);
            if (!selfClosing) stack.append({ name, hidden });
            if (body && selfClosing && isBlock(name) && !hidden) separator(units);
            continue;
        }
        const bool visible = body && !stack.isEmpty() && !stack.last().hidden;
        if (visible && source.at(i) == QLatin1Char('&')) {
            const int semicolon = source.indexOf(QLatin1Char(';'), i + 1);
            if (semicolon < 0 || semicolon - i > 32) {
                *error = QStringLiteral("Malformed XHTML entity at UTF-16 offset %1").arg(i);
                return false;
            }
            QString decoded;
            if (!decodeEntity(source.mid(i + 1, semicolon - i - 1), &decoded)) {
                *error = QStringLiteral("Unsupported XHTML entity at UTF-16 offset %1").arg(i);
                return false;
            }
            if (visible) {
                for (const QChar ch : decoded) units->append({ ch, i, semicolon + 1 });
            }
            i = semicolon + 1;
            continue;
        }
        if (visible) units->append({ source.at(i), i, i + 1 });
        ++i;
    }
    if (!stack.isEmpty()) {
        *error = QStringLiteral("Unclosed XHTML element: %1").arg(stack.last().name);
        return false;
    }
    return true;
}

QString context(const QVector<Unit> &units, int from, int to)
{
    QString value;
    for (int i = from; i < to && i < units.size(); ++i) {
        if (i < 0 || units.at(i).start < 0) break;
        value.append(units.at(i).ch);
    }
    return value;
}

QJsonObject issue(const QVector<Unit> &units, int first, int last,
                  const Source &source, const QString &snapshot,
                  const QString &resourceDigest, const QString &configDigest,
                  const QString &rule, const QString &severity,
                  const QString &replacement, const QString &reason)
{
    const int start = units.at(first).start;
    const int end = units.at(last).end;
    const QString raw = source.text.mid(start, end - start);
    const QString visible = context(units, first, last + 1);
    bool fromCdata = false;
    for (int i = first; i <= last; ++i) fromCdata |= units.at(i).cdata;
    QString before;
    for (int i = first - 1; i >= 0 && first - i <= 40 && units.at(i).start >= 0; --i) {
        before.prepend(units.at(i).ch);
    }
    const QString after = context(units, last + 1, last + 41);
    const bool direct = !fromCdata && raw == visible;
    const QString sourceKind = fromCdata ? QStringLiteral("cdata")
        : direct ? QStringLiteral("direct")
        : raw.contains(QLatin1Char('<')) ? QStringLiteral("markup")
                                          : QStringLiteral("entity");
    const QByteArray identity = snapshot.toUtf8() + source.id.toUtf8()
        + QByteArray::number(start) + ':' + QByteArray::number(end) + rule.toUtf8();
    const QString fingerprint = digest((rule + QLatin1Char('|') + source.path
        + QLatin1Char('|') + resourceDigest
        + QLatin1Char('|') + configDigest
        + QLatin1Char('|') + digest(raw.toUtf8()) + QLatin1Char('|')
        + before + QLatin1Char('|') + visible + QLatin1Char('|') + after).toUtf8());
    return QJsonObject {
        { QStringLiteral("issue_id"), digest(identity) },
        { QStringLiteral("rule"), rule },
        { QStringLiteral("severity"), severity },
        { QStringLiteral("resource_id"), source.id },
        { QStringLiteral("book_path"), source.path },
        { QStringLiteral("start"), start },
        { QStringLiteral("end"), end },
        { QStringLiteral("length"), end - start },
        { QStringLiteral("offset_units"), QStringLiteral("UTF-16 source") },
        { QStringLiteral("text"), visible.left(64) },
        { QStringLiteral("text_length"), visible.size() },
        { QStringLiteral("text_truncated"), visible.size() > 64 },
        { QStringLiteral("source_sha256"), digest(raw.toUtf8()) },
        { QStringLiteral("resource_sha256"), resourceDigest },
        { QStringLiteral("fingerprint"), fingerprint },
        { QStringLiteral("source_kind"), sourceKind },
        { QStringLiteral("before"), before },
        { QStringLiteral("after"), after },
        { QStringLiteral("suggestion"), replacement },
        { QStringLiteral("auto_fixable"), direct },
        { QStringLiteral("review_reason"), direct ? reason
                                                    : reason + QStringLiteral(" Source contains markup, CDATA, or an entity; inspect it before editing.") }
    };
}

bool isSuspicious(QChar ch)
{
    const ushort u = ch.unicode();
    return u == 0xfffd || u == 0x200b || u == 0xfeff || u == 0x2060
        || (u < 0x20 && u != '\n' && u != '\r' && u != '\t');
}

void scan(const QVector<Unit> &units, const Source &source,
          const QString &snapshot, const QString &configDigest,
          const QJsonObject &config, QJsonArray *issues)
{
    const QString resourceDigest = digest(source.text.toUtf8());
    QSet<QString> allowedRepeats;
    for (const QJsonValue &value : config.value(QStringLiteral("allow_repeats")).toArray()) {
        allowedRepeats.insert(value.toString());
    }
    for (int i = 0; i < units.size(); ++i) {
        if (units.at(i).start < 0) continue;
        const QChar ch = units.at(i).ch;
        if (isSuspicious(ch)) {
            const bool replacement = ch == QChar(0xfffd);
            issues->append(issue(units, i, i, source, snapshot, resourceDigest, configDigest,
                                 replacement ? QStringLiteral("REPLACEMENT_CHARACTER")
                                             : QStringLiteral("INVISIBLE_OR_CONTROL"),
                                 QStringLiteral("high"), QString(),
                                 QStringLiteral("Suspicious encoded or invisible character; choose its intended text.")));
        }
        if (ch != QChar(0xff0c) && ch != QChar(0x3002)) continue;
        if (i > 0 && units.at(i - 1).start >= 0 && units.at(i - 1).ch == ch) continue;
        int last = i;
        while (last + 1 < units.size() && units.at(last + 1).start >= 0
               && units.at(last + 1).ch == ch) ++last;
        if (last > i) {
            const QString repeated = QString(ch) + ch;
            if (!allowedRepeats.contains(repeated)) {
                issues->append(issue(units, i, last, source, snapshot, resourceDigest, configDigest,
                                     QStringLiteral("REPEATED_PUNCTUATION"),
                                     QStringLiteral("low"), QString(ch),
                                     QStringLiteral("Repeated punctuation may be intentional; review the book's style.")));
            }
            i = last;
        }
    }
    const QJsonArray pairs = config.value(QStringLiteral("variant_pairs")).toArray();
    const QJsonArray allowedTerms = config.value(QStringLiteral("allowed_terms")).toArray();
    if (pairs.isEmpty()) return;
    int segmentStart = 0;
    while (segmentStart < units.size()) {
        while (segmentStart < units.size() && units.at(segmentStart).start < 0) ++segmentStart;
        if (segmentStart >= units.size()) break;
        int segmentEnd = segmentStart;
        while (segmentEnd < units.size() && units.at(segmentEnd).start >= 0) ++segmentEnd;
        const QString visible = context(units, segmentStart, segmentEnd);
        QVector<bool> allowed(visible.size(), false);
        for (const QJsonValue &value : allowedTerms) {
            const QString term = value.toString();
            if (term.isEmpty()) continue;
            int from = 0;
            while ((from = visible.indexOf(term, from, Qt::CaseSensitive)) >= 0) {
                for (int j = from; j < from + term.size(); ++j) allowed[j] = true;
                from += qMax(1, term.size());
            }
        }
        for (const QJsonValue &value : pairs) {
            const QJsonObject pair = value.toObject();
            const QString observed = pair.value(QStringLiteral("observed")).toString();
            if (observed.isEmpty()) continue;
            int from = 0;
            while ((from = visible.indexOf(observed, from, Qt::CaseSensitive)) >= 0) {
                bool suppressed = false;
                for (int j = from; j < from + observed.size(); ++j) suppressed |= allowed.at(j);
                if (!suppressed) {
                    issues->append(issue(units, segmentStart + from,
                                         segmentStart + from + observed.size() - 1,
                                         source, snapshot, resourceDigest, configDigest,
                                         QStringLiteral("TERM_VARIANT"), QStringLiteral("low"),
                                         pair.value(QStringLiteral("preferred")).toString(),
                                         QStringLiteral("Configured term variant; review names and context before changing.")));
                }
                from += qMax(1, observed.size());
            }
        }
        segmentStart = segmentEnd + 1;
    }
}

int countStyle(const QVector<Unit> &units, QJsonObject *counts)
{
    int visibleCharacters = 0;
    const auto increment = [counts](const QString &key) {
        counts->insert(key, counts->value(key).toInt() + 1);
    };
    for (int i = 0; i < units.size(); ++i) {
        if (units.at(i).start < 0) continue;
        ++visibleCharacters;
        const QChar ch = units.at(i).ch;
        if (ch == QChar(0x3000)) increment(QStringLiteral("fullwidth_space"));
        if (ch == QChar(0xff0c)) increment(QStringLiteral("chinese_comma"));
        if (ch == QChar(0x3002)) increment(QStringLiteral("chinese_full_stop"));
        if (i + 1 >= units.size() || units.at(i + 1).start < 0) continue;
        const QChar next = units.at(i + 1).ch;
        if (ch == QChar(0xff01) && next == ch) increment(QStringLiteral("double_exclamation"));
        if ((ch == QChar(0xff5e) || ch == QChar(0x301c)) && next == ch) {
            increment(QStringLiteral("double_tilde"));
        }
        if (ch == QChar(0x2026) && next == ch) increment(QStringLiteral("double_ellipsis"));
        if (ch == QChar(0x2014) && next == ch) increment(QStringLiteral("double_em_dash"));
    }
    return visibleCharacters;
}

} // namespace

void ProofAudit::loadBookState(IBookWorkspace *workspace)
{
    const QString currentKey = bookKey(workspace);
    if (m_bookKey == currentKey && m_sessionId == workspace->bookSessionId()) {
        const QJsonObject latestConfig = loadConfig(currentKey);
        if (latestConfig != m_config) {
            m_config = latestConfig;
            m_snapshotId.clear();
            m_decisions.clear();
            m_planId.clear();
        }
        return;
    }
    m_bookKey = currentKey;
    m_sessionId = workspace->bookSessionId();
    m_records = loadRecords(currentKey);
    m_config = loadConfig(currentKey);
    m_snapshotId.clear();
    m_decisions.clear();
    m_planId.clear();
}

ToolResult ProofAudit::settings(IBookWorkspace *workspace)
{
    if (!workspace) return ToolResult::failure(QStringLiteral("NO_BOOK"), QStringLiteral("No open book"));
    loadBookState(workspace);
    return ToolResult::success(QJsonObject {
        { QStringLiteral("book"), m_config.value(QStringLiteral("book")).toObject() },
        { QStringLiteral("files"), m_config.value(QStringLiteral("files")).toObject() },
        { QStringLiteral("configuration_digest"), digest(QJsonDocument(m_config).toJson(QJsonDocument::Compact)) },
        { QStringLiteral("rule_version"), kRuleVersion },
        { QStringLiteral("stored_locally"), true }
    });
}

ToolResult ProofAudit::configure(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    if (!workspace) return ToolResult::failure(QStringLiteral("NO_BOOK"), QStringLiteral("No open book"));
    loadBookState(workspace);
    const QJsonObject scope = arguments.value(QStringLiteral("scope")).toObject();
    const QString kind = scope.value(QStringLiteral("kind")).toString();
    QString path;
    if (kind == QLatin1String("file")) {
        const QString wanted = scope.value(QStringLiteral("resource_id")).toString();
        for (const QJsonValue &value : workspace->resources()) {
            const QJsonObject resource = value.toObject();
            if (resource.value(QStringLiteral("resource_id")).toString() != wanted
                && resource.value(QStringLiteral("book_path")).toString() != wanted) continue;
            if (resource.value(QStringLiteral("kind")).toString() != QLatin1String("xhtml")
                && resource.value(QStringLiteral("media_type")).toString() != QLatin1String("application/xhtml+xml")) break;
            path = resource.value(QStringLiteral("book_path")).toString();
            break;
        }
        if (path.isEmpty()) return ToolResult::failure(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Configure a known XHTML file"));
    } else if (kind != QLatin1String("book")) {
        return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("Configuration scope must be book or file"));
    }
    QJsonObject next = m_config;
    QJsonObject files = next.value(QStringLiteral("files")).toObject();
    QJsonObject level = kind == QLatin1String("book")
        ? next.value(QStringLiteral("book")).toObject() : files.value(path).toObject();
    const auto validateStrings = [](const QJsonValue &value, int maximum,
                                    const QString &field, QJsonArray *normalized) -> QString {
        if (!value.isArray() || value.toArray().size() > maximum) {
            return QStringLiteral("%1 must be an array with at most %2 entries").arg(field).arg(maximum);
        }
        QSet<QString> unique;
        for (const QJsonValue &item : value.toArray()) {
            if (!item.isString() || item.toString().isEmpty()
                || item.toString().size() > 64 || !validXmlText(item.toString())) {
                return QStringLiteral("%1 contains an invalid term (maximum 64 UTF-16 units)").arg(field);
            }
            unique.insert(item.toString());
        }
        QStringList sorted = unique.values();
        std::sort(sorted.begin(), sorted.end());
        *normalized = QJsonArray::fromStringList(sorted);
        return QString();
    };
    if (arguments.contains(QStringLiteral("allow_repeats"))) {
        QJsonArray values;
        const QString error = validateStrings(arguments.value(QStringLiteral("allow_repeats")),
                                              2, QStringLiteral("allow_repeats"), &values);
        if (!error.isEmpty()) return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"), error);
        for (const QJsonValue &value : values) {
            if (value.toString() != QStringLiteral("，，") && value.toString() != QStringLiteral("。。")) {
                return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"),
                                           QStringLiteral("allow_repeats supports only ，， and 。。"));
            }
        }
        level.insert(QStringLiteral("allow_repeats"), values);
    }
    if (arguments.contains(QStringLiteral("allowed_terms"))) {
        QJsonArray values;
        const QString error = validateStrings(arguments.value(QStringLiteral("allowed_terms")),
                                              100, QStringLiteral("allowed_terms"), &values);
        if (!error.isEmpty()) return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"), error);
        level.insert(QStringLiteral("allowed_terms"), values);
    }
    if (arguments.contains(QStringLiteral("variant_pairs"))) {
        const QJsonValue pairs = arguments.value(QStringLiteral("variant_pairs"));
        if (!pairs.isArray() || pairs.toArray().size() > 32) {
            return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"),
                                       QStringLiteral("variant_pairs must have at most 32 entries"));
        }
        QMap<QString, QString> variants;
        for (const QJsonValue &value : pairs.toArray()) {
            const QJsonObject pair = value.toObject();
            const QString observed = pair.value(QStringLiteral("observed")).toString();
            const QString preferred = pair.value(QStringLiteral("preferred")).toString();
            if (observed.isEmpty() || preferred.isEmpty() || observed == preferred
                || observed.size() > 64 || preferred.size() > 64
                || !validXmlText(observed) || !validXmlText(preferred)
                || variants.contains(observed)) {
                return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"),
                                           QStringLiteral("Each variant pair needs unique observed and distinct preferred XHTML text (1–64 UTF-16 units)"));
            }
            variants.insert(observed, preferred);
        }
        QJsonArray normalized;
        for (auto it = variants.cbegin(); it != variants.cend(); ++it) {
            normalized.append(QJsonObject {
                { QStringLiteral("observed"), it.key() },
                { QStringLiteral("preferred"), it.value() }
            });
        }
        level.insert(QStringLiteral("variant_pairs"), normalized);
    }
    if (!arguments.contains(QStringLiteral("allow_repeats"))
        && !arguments.contains(QStringLiteral("allowed_terms"))
        && !arguments.contains(QStringLiteral("variant_pairs"))) {
        return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"),
                                   QStringLiteral("Provide at least one configuration field; use an empty array to clear it"));
    }
    if (kind == QLatin1String("book")) next.insert(QStringLiteral("book"), level);
    else {
        files.insert(path, level);
        next.insert(QStringLiteral("files"), files);
    }
    if (QJsonDocument(next).toJson(QJsonDocument::Compact).size() > 32768) {
        return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_INVALID"),
                                   QStringLiteral("Local proofreading configuration exceeds 32 KiB"));
    }
    if (!saveConfig(m_bookKey, next)) {
        return ToolResult::failure(QStringLiteral("AUDIT_CONFIG_WRITE_FAILED"),
                                   QStringLiteral("Could not save local proofreading configuration"));
    }
    m_config = next;
    m_snapshotId.clear();
    m_decisions.clear();
    m_planId.clear();
    return ToolResult::success(QJsonObject {
        { QStringLiteral("scope"), kind },
        { QStringLiteral("book_path"), path },
        { QStringLiteral("configuration"), level },
        { QStringLiteral("configuration_digest"), digest(QJsonDocument(m_config).toJson(QJsonDocument::Compact)) },
        { QStringLiteral("stored_locally"), true },
        { QStringLiteral("audit_snapshot_invalidated"), true },
        { QStringLiteral("book_changed"), false }
    });
}

ToolResult ProofAudit::audit(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    if (!workspace) return ToolResult::failure(QStringLiteral("NO_BOOK"), QStringLiteral("No open book"));
    loadBookState(workspace);
    const QJsonValue scopeValue = arguments.value(QStringLiteral("scope"));
    if (!scopeValue.isUndefined() && !scopeValue.isObject()) {
        return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("scope must be an object"));
    }
    const QJsonObject scope = scopeValue.toObject();
    const QString kind = scope.value(QStringLiteral("kind")).toString(QStringLiteral("whole_book"));
    const QString ruleset = arguments.value(QStringLiteral("ruleset")).toString(QStringLiteral("default"));
    if (ruleset != QLatin1String("default")) {
        return ToolResult::failure(QStringLiteral("AUDIT_RULESET_INVALID"), QStringLiteral("Only the default ruleset is available"));
    }
    const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(20), kMaxPage);
    QList<Source> all;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("kind")).toString() != QLatin1String("xhtml")
            && item.value(QStringLiteral("media_type")).toString() != QLatin1String("application/xhtml+xml")) continue;
        Source source;
        source.id = item.value(QStringLiteral("resource_id")).toString();
        source.path = item.value(QStringLiteral("book_path")).toString();
        source.text = workspace->workingText(source.id);
        all.append(source);
    }
    std::sort(all.begin(), all.end(), [](const Source &a, const Source &b) {
        return a.path == b.path ? a.id < b.id : a.path < b.path;
    });
    QSet<QString> ids;
    int selectionStart = -1;
    int selectionEnd = -1;
    if (kind == QLatin1String("file") || kind == QLatin1String("selection")) {
        const QString id = scope.value(QStringLiteral("resource_id")).toString();
        if (id.isEmpty()) return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("resource_id is required"));
        ids.insert(id);
        if (kind == QLatin1String("selection")) {
            selectionStart = scope.value(QStringLiteral("start")).toInt(-1);
            selectionEnd = scope.value(QStringLiteral("end")).toInt(-1);
            if (selectionStart < 0 || selectionEnd <= selectionStart) {
                return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("selection requires start < end in UTF-16 source offsets"));
            }
        }
    } else if (kind == QLatin1String("resources")) {
        const QJsonValue requested = scope.value(QStringLiteral("resource_ids"));
        if (!requested.isArray()) return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("resource_ids must be an array"));
        for (const QJsonValue &value : requested.toArray()) {
            if (!value.isString()) return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("resource_ids must contain strings"));
            ids.insert(value.toString());
        }
    } else if (kind == QLatin1String("spine")) {
        const QJsonArray spine = workspace->spine();
        const int start = scope.value(QStringLiteral("start_index")).toInt(0);
        const int end = scope.value(QStringLiteral("end_index")).toInt(spine.size());
        if (start < 0 || end <= start || end > spine.size()) {
            return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("spine uses a valid zero-based [start_index,end_index) range"));
        }
        for (int i = start; i < end; ++i) ids.insert(spine.at(i).toObject().value(QStringLiteral("resource_id")).toString());
    } else if (kind != QLatin1String("whole_book")) {
        return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("Unknown audit scope kind"));
    }
    QList<Source> selected;
    for (const Source &source : all) {
        if (kind == QLatin1String("whole_book") || ids.contains(source.id)
            || ids.contains(source.path)) selected.append(source);
    }
    if (kind != QLatin1String("whole_book") && selected.isEmpty()) {
        return ToolResult::failure(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("No XHTML resource in the requested scope"));
    }
    if (kind == QLatin1String("resources") && ids.size() != selected.size()) {
        return ToolResult::failure(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("An XHTML resource in resource_ids was not found"));
    }
    if (selectionStart >= 0 && selectionEnd > selected.first().text.size()) {
        return ToolResult::failure(QStringLiteral("AUDIT_SCOPE_INVALID"), QStringLiteral("selection exceeds XHTML source length"));
    }
    qint64 sourceUnits = 0;
    for (const Source &source : selected) sourceUnits += source.text.size();
    if (selected.size() > 1000 || sourceUnits > kMaxSourceUnits) {
        return ToolResult::failure(QStringLiteral("AUDIT_BUDGET_EXCEEDED"),
                                   QStringLiteral("Audit scope exceeds 1000 XHTML resources or 5 million UTF-16 source units; choose a narrower scope"),
                                   QJsonObject {
                                       { QStringLiteral("resources"), selected.size() },
                                       { QStringLiteral("source_units"), sourceUnits }
                                   });
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(workspace->bookSessionId().toUtf8());
    hash.addData(QByteArray::number(workspace->revision()));
    hash.addData(QByteArray::number(kRuleVersion));
    hash.addData(QJsonDocument(scope).toJson(QJsonDocument::Compact));
    hash.addData(QJsonDocument(m_config).toJson(QJsonDocument::Compact));
    for (const Source &source : selected) {
        hash.addData(source.id.toUtf8());
        hash.addData(source.path.toUtf8());
        hash.addData(source.text.toUtf8());
    }
    const QString snapshot = QString::fromLatin1(hash.result().toHex());
    int offset = 0;
    const QString cursor = arguments.value(QStringLiteral("cursor")).toString();
    if (!cursor.isEmpty()) {
        const QList<QByteArray> parts = QByteArray::fromBase64(
            cursor.toLatin1(), QByteArray::Base64UrlEncoding).split(':');
        bool ok = false;
        const int parsed = parts.size() == 2 ? parts.first().toInt(&ok) : -1;
        if (!ok || parsed < 0 || parts.size() != 2 || parts.last().size() != snapshot.size()) {
            return ToolResult::failure(QStringLiteral("AUDIT_CURSOR_INVALID"), QStringLiteral("Invalid audit cursor"));
        }
        if (parts.last() != snapshot.toLatin1()) {
            return ToolResult::failure(QStringLiteral("AUDIT_SNAPSHOT_STALE"), QStringLiteral("Book or audit scope changed; restart the audit"));
        }
        offset = parsed;
    }

    if (m_snapshotId != snapshot) {
        QJsonArray issues;
        int visibleCharacters = 0;
        QJsonObject styleCounts;
        const QString configDigest = digest(QJsonDocument(m_config).toJson(QJsonDocument::Compact));
        QHash<QString, QString> sourceDigests;
        for (const Source &source : selected) {
            sourceDigests.insert(source.id, digest(source.text.toUtf8()));
            QVector<Unit> units;
            QString error;
            if (!extractVisible(source.text, &units, &error)) {
                return ToolResult::failure(QStringLiteral("AUDIT_XHTML_INVALID"), error,
                    QJsonObject {{ QStringLiteral("resource_id"), source.id }});
            }
            visibleCharacters += countStyle(units, &styleCounts);
            scan(units, source, snapshot, configDigest,
                 effectiveConfig(m_config, source.path), &issues);
            if (issues.size() > kMaxIssues) {
                return ToolResult::failure(QStringLiteral("AUDIT_BUDGET_EXCEEDED"),
                    QStringLiteral("More than 20000 candidates; choose a narrower scope"));
            }
        }
        QList<QJsonObject> ordered;
        for (const QJsonValue &value : issues) ordered.append(value.toObject());
        std::sort(ordered.begin(), ordered.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const QString ap = a.value(QStringLiteral("book_path")).toString();
            const QString bp = b.value(QStringLiteral("book_path")).toString();
            if (ap != bp) return ap < bp;
            const int as = a.value(QStringLiteral("start")).toInt();
            const int bs = b.value(QStringLiteral("start")).toInt();
            if (as != bs) return as < bs;
            return a.value(QStringLiteral("rule")).toString() < b.value(QStringLiteral("rule")).toString();
        });
        issues = QJsonArray();
        for (const QJsonObject &value : ordered) issues.append(value);
        if (selectionStart >= 0) {
            QJsonArray filtered;
            for (const QJsonValue &value : issues) {
                const QJsonObject candidate = value.toObject();
                if (candidate.value(QStringLiteral("start")).toInt() >= selectionStart
                    && candidate.value(QStringLiteral("end")).toInt() <= selectionEnd) filtered.append(candidate);
            }
            issues = filtered;
        }
        QHash<QString, int> fingerprintCounts;
        for (const QJsonValue &value : issues) {
            const QString fingerprint = value.toObject().value(QStringLiteral("fingerprint")).toString();
            fingerprintCounts[fingerprint] += 1;
        }
        QJsonArray reviewedIssues;
        m_decisions.clear();
        for (const QJsonValue &value : issues) {
            QJsonObject candidate = value.toObject();
            const QString fingerprint = candidate.value(QStringLiteral("fingerprint")).toString();
            if (fingerprintCounts.value(fingerprint) == 1 && m_records.contains(fingerprint)
                && m_records.value(fingerprint).value(QStringLiteral("rule_version")).toInt() == kRuleVersion) {
                const QJsonObject record = m_records.value(fingerprint);
                const QString decision = record.value(QStringLiteral("decision")).toString();
                candidate.insert(QStringLiteral("decision"), decision);
                if (decision == QLatin1String("accept")) {
                    candidate.insert(QStringLiteral("replacement"), record.value(QStringLiteral("replacement")));
                }
                m_decisions.insert(candidate.value(QStringLiteral("issue_id")).toString(), record);
            } else if (fingerprintCounts.value(fingerprint) > 1) {
                candidate.insert(QStringLiteral("decision_status"), QStringLiteral("ambiguous_fingerprint"));
            }
            reviewedIssues.append(candidate);
        }
        m_snapshotId = snapshot;
        m_issues = reviewedIssues;
        m_resourceCount = selected.size();
        m_visibleCharacters = visibleCharacters;
        m_styleCounts = styleCounts;
        m_sourceDigests = sourceDigests;
        m_sessionId = workspace->bookSessionId();
        m_auditRevision = workspace->revision();
        m_planId.clear();
    }
    offset = qMin(offset, m_issues.size());
    QJsonArray page;
    for (int i = offset; i < qMin(offset + limit, m_issues.size()); ++i) page.append(m_issues.at(i));
    const bool hasMore = offset + page.size() < m_issues.size();
    QJsonObject data {
        { QStringLiteral("issues"), page },
        { QStringLiteral("rule_version"), kRuleVersion },
        { QStringLiteral("snapshot_id"), snapshot },
        { QStringLiteral("configuration_digest"), digest(QJsonDocument(m_config).toJson(QJsonDocument::Compact)) },
        { QStringLiteral("book_revision"), static_cast<qint64>(workspace->revision()) },
        { QStringLiteral("total_count"), m_issues.size() },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("has_more"), hasMore },
        { QStringLiteral("scanned_resources"), m_resourceCount },
        { QStringLiteral("visible_characters"), m_visibleCharacters },
        { QStringLiteral("style_counts"), m_styleCounts },
        { QStringLiteral("style_counts_are_errors"), false },
        { QStringLiteral("review_required"), true }
    };
    if (hasMore) {
        const QByteArray cursorBytes = QByteArray::number(offset + page.size())
            + ':' + snapshot.toLatin1();
        data.insert(QStringLiteral("next_cursor"), QString::fromLatin1(
            cursorBytes.toBase64(QByteArray::Base64UrlEncoding
                                 | QByteArray::OmitTrailingEquals)));
    }
    return ToolResult::success(data);
}

bool ProofAudit::sourceSnapshotCurrent(IBookWorkspace *workspace) const
{
    if (!workspace || m_snapshotId.isEmpty()
        || m_sessionId != workspace->bookSessionId()
        || m_auditRevision != workspace->revision()
        || loadConfig(m_bookKey) != m_config) return false;
    for (auto it = m_sourceDigests.cbegin(); it != m_sourceDigests.cend(); ++it) {
        if (digest(workspace->workingText(it.key()).toUtf8()) != it.value()) return false;
    }
    return true;
}

ToolResult ProofAudit::decide(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    if (!sourceSnapshotCurrent(workspace)) {
        return ToolResult::failure(QStringLiteral("AUDIT_SNAPSHOT_STALE"),
                                   QStringLiteral("Book changed; scan again before deciding"));
    }
    const QString id = arguments.value(QStringLiteral("issue_id")).toString();
    const QString decision = arguments.value(QStringLiteral("decision")).toString();
    if (decision != QLatin1String("accept") && decision != QLatin1String("ignore")
        && decision != QLatin1String("pending")) {
        return ToolResult::failure(QStringLiteral("AUDIT_DECISION_INVALID"),
                                   QStringLiteral("decision must be accept, ignore, or pending"));
    }
    int index = -1;
    for (int i = 0; i < m_issues.size(); ++i) {
        if (m_issues.at(i).toObject().value(QStringLiteral("issue_id")).toString() == id) {
            index = i;
            break;
        }
    }
    if (index < 0) return ToolResult::failure(QStringLiteral("AUDIT_ISSUE_NOT_FOUND"), QStringLiteral("Issue is not in the current audit snapshot"));
    QJsonObject candidate = m_issues.at(index).toObject();
    QString replacement = arguments.value(QStringLiteral("replacement")).toString();
    if (decision == QLatin1String("accept")) {
        if (!arguments.contains(QStringLiteral("replacement"))) {
            replacement = candidate.value(QStringLiteral("suggestion")).toString();
            if (replacement.isEmpty()) {
                return ToolResult::failure(QStringLiteral("AUDIT_REPLACEMENT_REQUIRED"),
                                           QStringLiteral("Provide the reviewed replacement, including an explicit empty string to delete"));
            }
        }
        if (replacement.size() > 256 || !validXmlText(replacement)) {
            return ToolResult::failure(QStringLiteral("AUDIT_REPLACEMENT_INVALID"),
                                       QStringLiteral("Replacement must be at most 256 UTF-16 units and valid XHTML text"));
        }
    }
    const QString reviewer = arguments.value(QStringLiteral("reviewer")).toString(QStringLiteral("agent"));
    if (reviewer.size() > 64) return ToolResult::failure(QStringLiteral("AUDIT_REVIEWER_INVALID"), QStringLiteral("reviewer is too long"));
    const QString fingerprint = candidate.value(QStringLiteral("fingerprint")).toString();
    const QJsonObject prior = m_records.value(fingerprint);
    if (decision == QLatin1String("pending")) m_records.remove(fingerprint);
    else {
        m_records.insert(fingerprint, QJsonObject {
            { QStringLiteral("decision"), decision },
            { QStringLiteral("replacement"), decision == QLatin1String("accept") ? replacement : QString() },
            { QStringLiteral("reviewer"), reviewer },
            { QStringLiteral("rule_version"), kRuleVersion },
            { QStringLiteral("book_path"), candidate.value(QStringLiteral("book_path")) },
            { QStringLiteral("rule"), candidate.value(QStringLiteral("rule")) },
            { QStringLiteral("source_sha256"), candidate.value(QStringLiteral("source_sha256")) }
        });
    }
    if (m_records.size() > 5000 || !saveRecords(m_bookKey, m_records)) {
        if (prior.isEmpty()) m_records.remove(fingerprint);
        else m_records.insert(fingerprint, prior);
        return ToolResult::failure(QStringLiteral("AUDIT_RECORD_WRITE_FAILED"),
                                   QStringLiteral("Could not save the local proofreading decision"));
    }
    if (decision == QLatin1String("pending")) {
        m_decisions.remove(id);
        candidate.remove(QStringLiteral("decision"));
        candidate.remove(QStringLiteral("replacement"));
    } else {
        m_decisions.insert(id, m_records.value(fingerprint));
        candidate.insert(QStringLiteral("decision"), decision);
        if (decision == QLatin1String("accept")) candidate.insert(QStringLiteral("replacement"), replacement);
        else candidate.remove(QStringLiteral("replacement"));
    }
    m_issues.replace(index, candidate);
    m_planId.clear();
    return ToolResult::success(QJsonObject {
        { QStringLiteral("issue_id"), id },
        { QStringLiteral("decision"), decision },
        { QStringLiteral("reviewer"), reviewer },
        { QStringLiteral("recorded_locally"), true },
        { QStringLiteral("book_changed"), false }
    });
}

ToolResult ProofAudit::plan(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    if (!sourceSnapshotCurrent(workspace)) {
        return ToolResult::failure(QStringLiteral("AUDIT_SNAPSHOT_STALE"),
                                   QStringLiteral("Book changed; scan and review again"));
    }
    const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(20), 20);
    int offset = 0;
    const QString cursor = arguments.value(QStringLiteral("cursor")).toString();
    if (cursor.isEmpty()) {
        const QJsonValue values = arguments.value(QStringLiteral("accepted_issue_ids"));
        if (!values.isArray() || values.toArray().isEmpty() || values.toArray().size() > 100) {
            return ToolResult::failure(QStringLiteral("AUDIT_PLAN_INVALID"),
                                       QStringLiteral("Provide 1 to 100 accepted_issue_ids, preferably one chapter at a time"));
        }
        QHash<QString, QJsonObject> issuesById;
        for (const QJsonValue &value : m_issues) {
            const QJsonObject candidate = value.toObject();
            issuesById.insert(candidate.value(QStringLiteral("issue_id")).toString(), candidate);
        }
        QSet<QString> seen;
        QList<QJsonObject> items;
        QHash<QString, QString> sourceDigests;
        for (const QJsonValue &value : values.toArray()) {
            if (!value.isString() || seen.contains(value.toString())
                || !issuesById.contains(value.toString())) {
                return ToolResult::failure(QStringLiteral("AUDIT_PLAN_INVALID"),
                                           QStringLiteral("Plan contains an unknown or repeated issue ID"));
            }
            const QString id = value.toString();
            seen.insert(id);
            const QJsonObject candidate = issuesById.value(id);
            const QJsonObject decision = m_decisions.value(id);
            if (decision.value(QStringLiteral("decision")).toString() != QLatin1String("accept")) {
                return ToolResult::failure(QStringLiteral("AUDIT_NOT_ACCEPTED"),
                                           QStringLiteral("Every planned issue must have an accept decision"));
            }
            if (candidate.value(QStringLiteral("source_kind")).toString() == QLatin1String("markup")
                || candidate.value(QStringLiteral("source_kind")).toString() == QLatin1String("cdata")) {
                return ToolResult::failure(QStringLiteral("AUDIT_MARKUP_SPAN"),
                                           QStringLiteral("This candidate spans XHTML tags or CDATA; edit its source manually after review"));
            }
            const QString resourceId = candidate.value(QStringLiteral("resource_id")).toString();
            const QString source = workspace->workingText(resourceId);
            const int start = candidate.value(QStringLiteral("start")).toInt();
            const int end = candidate.value(QStringLiteral("end")).toInt();
            if (start < 0 || end <= start || end > source.size() || end - start > 256) {
                return ToolResult::failure(QStringLiteral("AUDIT_PLAN_INVALID"),
                                           QStringLiteral("Candidate source span is too large or invalid"));
            }
            const QString before = source.mid(start, end - start);
            if (digest(before.toUtf8()) != candidate.value(QStringLiteral("source_sha256")).toString()) {
                return ToolResult::failure(QStringLiteral("AUDIT_SNAPSHOT_STALE"),
                                           QStringLiteral("Candidate source changed; rescan"));
            }
            const QString after = escapeXmlText(decision.value(QStringLiteral("replacement")).toString());
            if (after == before) {
                return ToolResult::failure(QStringLiteral("AUDIT_NO_OP"),
                                           QStringLiteral("Accepted replacement does not change the source"));
            }
            sourceDigests.insert(resourceId, digest(source.toUtf8()));
            items.append(QJsonObject {
                { QStringLiteral("issue_id"), id },
                { QStringLiteral("resource_id"), resourceId },
                { QStringLiteral("book_path"), candidate.value(QStringLiteral("book_path")) },
                { QStringLiteral("start"), start },
                { QStringLiteral("end"), end },
                { QStringLiteral("before"), before },
                { QStringLiteral("after"), after },
                { QStringLiteral("replacement"), decision.value(QStringLiteral("replacement")) },
                { QStringLiteral("rule"), candidate.value(QStringLiteral("rule")) },
                { QStringLiteral("severity"), candidate.value(QStringLiteral("severity")) },
                { QStringLiteral("source_sha256"), candidate.value(QStringLiteral("source_sha256")) }
            });
        }
        std::sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const QString ap = a.value(QStringLiteral("book_path")).toString();
            const QString bp = b.value(QStringLiteral("book_path")).toString();
            return ap == bp ? a.value(QStringLiteral("start")).toInt() < b.value(QStringLiteral("start")).toInt()
                            : ap < bp;
        });
        for (int i = 1; i < items.size(); ++i) {
            if (items.at(i).value(QStringLiteral("resource_id")) == items.at(i - 1).value(QStringLiteral("resource_id"))
                && items.at(i).value(QStringLiteral("start")).toInt() < items.at(i - 1).value(QStringLiteral("end")).toInt()) {
                return ToolResult::failure(QStringLiteral("AUDIT_OVERLAPPING_EDITS"),
                                           QStringLiteral("Accepted issues overlap; resolve them before planning"));
            }
        }
        m_planItems = QJsonArray();
        for (const QJsonObject &item : items) m_planItems.append(item);
        m_planSourceDigests = sourceDigests;
        m_planSnapshot = m_snapshotId;
        m_planRevision = workspace->revision();
        m_planDigest = digest(QJsonDocument(m_planItems).toJson(QJsonDocument::Compact)
            + m_planSnapshot.toUtf8() + QByteArray::number(m_planRevision));
        m_planId = digest(m_planDigest.toUtf8() + workspace->bookSessionId().toUtf8());
        m_reviewedThrough = 0;
    } else {
        if (m_planId.isEmpty() || m_planSnapshot != m_snapshotId) {
            return ToolResult::failure(QStringLiteral("AUDIT_PLAN_STALE"), QStringLiteral("No current plan to continue"));
        }
        const QList<QByteArray> parts = QByteArray::fromBase64(cursor.toLatin1(), QByteArray::Base64UrlEncoding).split(':');
        bool ok = false;
        offset = parts.size() == 2 ? parts.first().toInt(&ok) : -1;
        if (!ok || offset != m_reviewedThrough || parts.size() != 2 || parts.last() != m_planDigest.toLatin1()) {
            return ToolResult::failure(QStringLiteral("AUDIT_PLAN_CURSOR_INVALID"),
                                       QStringLiteral("Read plan pages in order using next_cursor"));
        }
    }
    QJsonArray page;
    for (int i = offset; i < qMin(offset + limit, m_planItems.size()); ++i) page.append(m_planItems.at(i));
    m_reviewedThrough = offset + page.size();
    const bool hasMore = m_reviewedThrough < m_planItems.size();
    QJsonObject data {
        { QStringLiteral("plan_id"), m_planId },
        { QStringLiteral("plan_digest"), m_planDigest },
        { QStringLiteral("snapshot_id"), m_planSnapshot },
        { QStringLiteral("book_revision"), static_cast<qint64>(m_planRevision) },
        { QStringLiteral("items"), page },
        { QStringLiteral("total_count"), m_planItems.size() },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("has_more"), hasMore },
        { QStringLiteral("review_complete"), !hasMore }
    };
    if (hasMore) {
        const QByteArray bytes = QByteArray::number(m_reviewedThrough) + ':' + m_planDigest.toLatin1();
        data.insert(QStringLiteral("next_cursor"), QString::fromLatin1(bytes.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    }
    return ToolResult::success(data);
}

ToolResult ProofAudit::apply(IBookWorkspace *workspace, const QJsonObject &arguments)
{
    if (m_planId.isEmpty() || arguments.value(QStringLiteral("plan_id")).toString() != m_planId
        || arguments.value(QStringLiteral("plan_digest")).toString() != m_planDigest) {
        return ToolResult::failure(QStringLiteral("AUDIT_PLAN_STALE"), QStringLiteral("Plan ID or digest does not match"));
    }
    if (m_reviewedThrough != m_planItems.size()) {
        return ToolResult::failure(QStringLiteral("AUDIT_PLAN_NOT_REVIEWED"),
                                   QStringLiteral("Read every plan page before applying"));
    }
    const qint64 expected = arguments.value(QStringLiteral("expected_book_revision")).toInteger(-1);
    QSet<QString> changedResources;
    QJsonArray changedIssues;
    if (workspace) {
        for (auto it = m_planSourceDigests.cbegin(); it != m_planSourceDigests.cend(); ++it) {
            if (digest(workspace->workingText(it.key()).toUtf8()) != it.value()) {
                changedResources.insert(it.key());
            }
        }
        for (const QJsonValue &value : m_planItems) {
            const QJsonObject item = value.toObject();
            if (changedResources.contains(item.value(QStringLiteral("resource_id")).toString())) {
                changedIssues.append(item.value(QStringLiteral("issue_id")));
            }
        }
    }
    if (!workspace || expected < 0 || static_cast<quint64>(expected) != m_planRevision
        || !sourceSnapshotCurrent(workspace) || workspace->revision() != m_planRevision
        || m_planSnapshot != m_snapshotId) {
        return ToolResult::failure(QStringLiteral("AUDIT_PLAN_STALE"),
                                   QStringLiteral("Book or source changed; rescan and replan"),
                                   QJsonObject {
                                       { QStringLiteral("conflicting_issue_ids"), changedIssues },
                                       { QStringLiteral("expected_book_revision"), static_cast<qint64>(m_planRevision) },
                                       { QStringLiteral("actual_book_revision"), workspace ? static_cast<qint64>(workspace->revision()) : -1 }
                                   });
    }
    if (workspace->hasOpenTransaction()) {
        return ToolResult::failure(QStringLiteral("TRANSACTION_ALREADY_OPEN"),
                                   QStringLiteral("Close the current transaction before proof.apply"));
    }
    QHash<QString, QString> replacements;
    QStringList resourceOrder;
    for (const QJsonValue &value : m_planItems) {
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("resource_id")).toString();
        if (!replacements.contains(id)) {
            const QString source = workspace->workingText(id);
            if (digest(source.toUtf8()) != m_planSourceDigests.value(id)) {
                return ToolResult::failure(QStringLiteral("AUDIT_PLAN_STALE"),
                                           QStringLiteral("A planned resource changed; rescan"));
            }
            replacements.insert(id, source);
            resourceOrder.append(id);
        }
        const QString source = workspace->workingText(id);
        const int start = item.value(QStringLiteral("start")).toInt();
        const int end = item.value(QStringLiteral("end")).toInt();
        if (source.mid(start, end - start) != item.value(QStringLiteral("before")).toString()) {
            return ToolResult::failure(QStringLiteral("AUDIT_PLAN_STALE"), QStringLiteral("A planned source span changed"));
        }
    }
    for (int i = m_planItems.size() - 1; i >= 0; --i) {
        const QJsonObject item = m_planItems.at(i).toObject();
        const QString id = item.value(QStringLiteral("resource_id")).toString();
        QString source = replacements.value(id);
        source.replace(item.value(QStringLiteral("start")).toInt(),
                       item.value(QStringLiteral("end")).toInt() - item.value(QStringLiteral("start")).toInt(),
                       item.value(QStringLiteral("after")).toString());
        replacements.insert(id, source);
    }
    const BookOpResult begun = workspace->beginTransaction(QStringLiteral("Proofreading: %1 accepted issue(s)").arg(m_planItems.size()));
    if (!begun.ok) return ToolResult::failure(begun.code, begun.message, begun.data);
    for (const QString &id : resourceOrder) {
        const BookOpResult staged = workspace->replaceText(id, replacements.value(id), workspace->resourceRevision(id));
        if (!staged.ok) {
            workspace->rollbackTransaction();
            return ToolResult::failure(staged.code, staged.message, staged.data);
        }
    }
    const QJsonObject data {
        { QStringLiteral("plan_id"), m_planId },
        { QStringLiteral("plan_digest"), m_planDigest },
        { QStringLiteral("staged_issues"), m_planItems.size() },
        { QStringLiteral("staged_resources"), resourceOrder.size() },
        { QStringLiteral("transaction_id"), begun.data.value(QStringLiteral("transaction_id")) },
        { QStringLiteral("live_unchanged"), true },
        { QStringLiteral("requires_transaction_preview"), true },
        { QStringLiteral("requires_transaction_commit"), true },
        { QStringLiteral("requires_book_check_after_commit"), true }
    };
    m_planId.clear();
    return ToolResult::success(data, false, true);
}

} // namespace SigilAgent
