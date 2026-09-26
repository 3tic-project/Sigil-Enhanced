/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/BookTools.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <functional>

#include "Agent/Execution/BookEdits.h"
#include "Agent/Execution/ContentOps.h"
#include "Agent/Execution/PatchRange.h"
#include "Agent/Execution/ProofAudit.h"
#include "Agent/Typeset/ManuscriptParser.h"
#include "Agent/Typeset/TypesetEngine.h"

namespace SigilAgent
{

namespace
{

constexpr int DEFAULT_INVENTORY_PAGE_SIZE = 100;
constexpr int MAX_INVENTORY_PAGE_SIZE = 200;
constexpr int DEFAULT_STYLESHEET_PAGE_SIZE = 12;
constexpr int MAX_STYLESHEET_PAGE_SIZE = 50;
constexpr int DEFAULT_DIAGNOSTIC_PAGE_SIZE = 100;
constexpr int MAX_DIAGNOSTIC_PAGE_SIZE = 200;
constexpr int DEFAULT_LITERAL_SEARCH_MATCHES = 20;
constexpr int MAX_LITERAL_SEARCH_MATCHES = 50;
constexpr int MAX_LITERAL_SEARCH_QUERY_LENGTH = 512;
constexpr int MAX_LITERAL_SEARCH_SNIPPET_LENGTH = 240;
constexpr int DEFAULT_SESSION_TASK_PAGE_SIZE = 20;
constexpr int MAX_SESSION_TASK_PAGE_SIZE = 50;
constexpr int DEFAULT_SESSION_MEMORY_PAGE_SIZE = 16;
constexpr int MAX_SESSION_MEMORY_PAGE_SIZE = 32;
constexpr int DEFAULT_CHECKPOINT_PAGE_SIZE = 20;
constexpr int MAX_CHECKPOINT_PAGE_SIZE = 50;
constexpr int MAX_CHECKPOINT_LABEL_LENGTH = 256;
constexpr int MAX_CHECKPOINT_AFFECTED_RESOURCES = 32;
constexpr int MAX_CHECKPOINT_RESOURCE_ID_LENGTH = 256;
constexpr int DEFAULT_MANUSCRIPT_SUMMARY_PAGE_SIZE = 40;
constexpr int MAX_MANUSCRIPT_SUMMARY_PAGE_SIZE = 100;
constexpr int DEFAULT_TRANSACTION_PREVIEW_PAGE_SIZE = 50;
constexpr int MAX_TRANSACTION_PREVIEW_PAGE_SIZE = 100;
constexpr int DEFAULT_METADATA_PAGE_SIZE = 20;
constexpr int MAX_METADATA_PAGE_SIZE = 50;
constexpr int MAX_METADATA_PREVIEW_LENGTH = 512;
constexpr int MAX_METADATA_NAME_PREVIEW_LENGTH = 256;
constexpr int DEFAULT_METADATA_FRAGMENT_LENGTH = 2048;
constexpr int MAX_METADATA_FRAGMENT_LENGTH = 8192;
constexpr int DEFAULT_RESOURCE_FRAGMENT_LENGTH = 2048;
constexpr int MAX_RESOURCE_FRAGMENT_LENGTH = 8192;

QJsonObject emptyObjectSchema()
{
    return QJsonObject {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject() }
    };
}

QJsonObject paginationSchema(int default_limit, int max_limit)
{
    return QJsonObject {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject {
            { QStringLiteral("offset"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 0 },
                { QStringLiteral("default"), 0 }
            } },
            { QStringLiteral("limit"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 1 },
                { QStringLiteral("maximum"), max_limit },
                { QStringLiteral("default"), default_limit }
            } }
        } }
    };
}

QJsonObject paginatedArray(const QString &key,
                           const QJsonArray &all,
                           const QJsonObject &arguments,
                           int default_limit,
                           int max_limit)
{
    const int offset = qBound(
        0, arguments.value(QStringLiteral("offset")).toInt(0), all.size());
    const int requested_limit = arguments.contains(QStringLiteral("limit"))
        ? arguments.value(QStringLiteral("limit")).toInt(default_limit)
        : default_limit;
    const int limit = qBound(1, requested_limit, max_limit);
    const int end = qMin(all.size(), offset + limit);
    QJsonArray page;
    for (int index = offset; index < end; ++index) page.append(all.at(index));
    const bool has_more = end < all.size();
    QJsonObject result {
        { key, page },
        { QStringLiteral("total_count"), all.size() },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("has_more"), has_more }
    };
    if (has_more) result.insert(QStringLiteral("next_offset"), end);
    return result;
}

QJsonObject paginatedArrays(QJsonObject result,
                            const QStringList &keys,
                            const QJsonObject &arguments,
                            int default_limit,
                            int max_limit)
{
    int total_count = 0;
    for (const QString &key : keys) {
        total_count = qMax(total_count, result.value(key).toArray().size());
    }
    const int offset = qBound(
        0, arguments.value(QStringLiteral("offset")).toInt(0), total_count);
    const int requested_limit = arguments.contains(QStringLiteral("limit"))
        ? arguments.value(QStringLiteral("limit")).toInt(default_limit)
        : default_limit;
    const int limit = qBound(1, requested_limit, max_limit);
    QJsonObject total_counts;
    QJsonObject returned_counts;
    for (const QString &key : keys) {
        const QJsonArray all = result.value(key).toArray();
        const int count = qMin(limit, qMax(0, all.size() - offset));
        QJsonArray page;
        for (int index = offset; index < offset + count; ++index) {
            page.append(all.at(index));
        }
        result.insert(key, page);
        total_counts.insert(key, all.size());
        returned_counts.insert(key, page.size());
    }
    const bool has_more = offset + qMin(limit, total_count - offset) < total_count;
    result.insert(QStringLiteral("total_counts"), total_counts);
    result.insert(QStringLiteral("returned_counts"), returned_counts);
    result.insert(QStringLiteral("offset"), offset);
    result.insert(QStringLiteral("limit"), limit);
    result.insert(QStringLiteral("has_more"), has_more);
    if (has_more) {
        result.insert(QStringLiteral("next_offset"), offset + limit);
    }
    return result;
}

ToolResult paginatedSearch(IBookWorkspace *workspace, const QJsonObject &arguments,
                           bool regex_search)
{
    const QString query = arguments.value(regex_search ? QStringLiteral("pattern")
                                                       : QStringLiteral("query")).toString();
    if (query.isEmpty()) {
        return ToolResult::failure(QStringLiteral("SEARCH_QUERY_REQUIRED"),
                                   QStringLiteral("A nonempty query or pattern is required"));
    }
    if (!regex_search && query.size() > MAX_LITERAL_SEARCH_QUERY_LENGTH) {
        return ToolResult::failure(
            QStringLiteral("SEARCH_QUERY_TOO_LONG"),
            QStringLiteral("book.search query is capped at 512 characters. Search for a shorter distinctive literal."),
            QJsonObject {
                { QStringLiteral("query_length"), query.size() },
                { QStringLiteral("max_query_length"), MAX_LITERAL_SEARCH_QUERY_LENGTH }
            });
    }
    const QRegularExpression pattern = regex_search ? compileRegex(query, nullptr)
                                                    : QRegularExpression();
    if (regex_search && !pattern.isValid()) {
        return ToolResult::failure(QStringLiteral("REGEX_INVALID"), pattern.errorString(),
                                   QJsonObject {{ QStringLiteral("error_offset"),
                                                  pattern.patternErrorOffset() }});
    }

    const int default_limit = regex_search ? DEFAULT_REGEX_SEARCH_MATCHES
                                           : DEFAULT_LITERAL_SEARCH_MATCHES;
    const int limit = qBound(1, arguments.value(QStringLiteral("limit"))
                                   .toInt(arguments.value(QStringLiteral("max_matches"))
                                              .toInt(default_limit)), 50);
    const bool count_only = arguments.value(QStringLiteral("count_only")).toBool();
    if ((arguments.contains(QStringLiteral("resource_id"))
         && !arguments.value(QStringLiteral("resource_id")).isString())
        || (arguments.contains(QStringLiteral("resource_ids"))
            && !arguments.value(QStringLiteral("resource_ids")).isArray())) {
        return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                   QStringLiteral("Search resource scope has an invalid type"));
    }
    const QString single_id = arguments.value(QStringLiteral("resource_id")).toString();
    const QJsonArray requested_resources = arguments.value(QStringLiteral("resource_ids")).toArray();
    if (!single_id.isEmpty() && !requested_resources.isEmpty()) {
        return ToolResult::failure(QStringLiteral("INVALID_ARGUMENT"),
                                   QStringLiteral("Use resource_id or resource_ids, not both"));
    }

    QList<QJsonObject> resources;
    QHash<QString, QString> ids_by_name;
    for (const QJsonValue &value : workspace->resources()) {
        const QJsonObject resource = value.toObject();
        const QString id = resource.value(QStringLiteral("resource_id")).toString();
        const QString path = resource.value(QStringLiteral("book_path")).toString();
        const QString kind = resource.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("image") || kind == QLatin1String("font")) continue;
        ids_by_name.insert(id, id);
        ids_by_name.insert(path, id);
        resources.append(resource);
    }
    const BookOpResult staged_preview = workspace->previewTransaction();
    if (staged_preview.ok) {
        for (const QJsonValue &value : staged_preview.data.value(
                 QStringLiteral("changes")).toArray()) {
            const QJsonObject addition = value.toObject();
            if (!addition.value(QStringLiteral("added")).toBool()) continue;
            const QString id = addition.value(QStringLiteral("resource_id")).toString();
            if (id.isEmpty() || ids_by_name.contains(id)) continue;
            const BookOpResult fragment = workspace->readFragment(id, 0, 1);
            if (!fragment.ok) continue;
            const QString path = fragment.data.value(QStringLiteral("book_path")).toString();
            ids_by_name.insert(id, id);
            ids_by_name.insert(path, id);
            resources.append(QJsonObject {
                { QStringLiteral("resource_id"), id },
                { QStringLiteral("book_path"), path },
                { QStringLiteral("kind"), QStringLiteral("text") }
            });
        }
    }
    QSet<QString> selected;
    const auto select = [&ids_by_name, &selected](const QString &name) {
        if (name.isEmpty() || !ids_by_name.contains(name)) return false;
        selected.insert(ids_by_name.value(name));
        return true;
    };
    if (!single_id.isEmpty() && !select(single_id)) {
        return ToolResult::failure(QStringLiteral("RESOURCE_NOT_FOUND"),
                                   QStringLiteral("Unknown text resource: %1").arg(single_id));
    }
    for (const QJsonValue &value : requested_resources) {
        if (!value.isString() || !select(value.toString())) {
            return ToolResult::failure(QStringLiteral("RESOURCE_NOT_FOUND"),
                                       QStringLiteral("Unknown text resource in resource_ids"));
        }
    }
    std::sort(resources.begin(), resources.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString ap = a.value(QStringLiteral("book_path")).toString();
        const QString bp = b.value(QStringLiteral("book_path")).toString();
        return ap == bp
            ? a.value(QStringLiteral("resource_id")).toString()
                  < b.value(QStringLiteral("resource_id")).toString()
            : ap < bp;
    });

    QJsonArray selected_ids;
    for (const QJsonObject &resource : resources) {
        const QString id = resource.value(QStringLiteral("resource_id")).toString();
        if (selected.isEmpty() && single_id.isEmpty() && requested_resources.isEmpty()) {
            selected_ids.append(id);
        } else if (selected.contains(id)) {
            selected_ids.append(id);
        }
    }
    const QJsonObject identity {
        { QStringLiteral("regex"), regex_search },
        { QStringLiteral("query"), query },
        { QStringLiteral("resource_ids"), selected_ids }
    };
    const QByteArray query_digest = QCryptographicHash::hash(
        QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    QCryptographicHash snapshot_hash(QCryptographicHash::Sha256);
    snapshot_hash.addData(query_digest);
    snapshot_hash.addData(workspace->bookSessionId().toUtf8());
    snapshot_hash.addData(QByteArray::number(workspace->revision()));
    for (const QJsonValue &value : selected_ids) {
        const QString id = value.toString();
        snapshot_hash.addData(id.toUtf8());
        snapshot_hash.addData(workspace->workingText(id).toUtf8());
    }
    const QByteArray snapshot = snapshot_hash.result().toHex();
    int offset = 0;
    const QString cursor = arguments.value(QStringLiteral("cursor")).toString();
    if (!cursor.isEmpty()) {
        const QByteArray decoded = QByteArray::fromBase64(
            cursor.toLatin1(), QByteArray::Base64UrlEncoding);
        const QList<QByteArray> parts = decoded.split(':');
        bool index_ok = false;
        const int index = parts.size() == 2 ? parts.first().toInt(&index_ok) : -1;
        if (!index_ok || index < 0 || parts.size() != 2 || parts.last().size() != snapshot.size()) {
            return ToolResult::failure(QStringLiteral("SEARCH_CURSOR_INVALID"),
                                       QStringLiteral("Invalid search cursor"));
        }
        if (parts.last() != snapshot) {
            return ToolResult::failure(QStringLiteral("SEARCH_SNAPSHOT_STALE"),
                                       QStringLiteral("Book or search scope changed; restart at the first page"));
        }
        offset = index;
    }

    QJsonArray matches;
    int total_count = 0;
    for (const QJsonObject &resource : resources) {
        const QString id = resource.value(QStringLiteral("resource_id")).toString();
        if (!selected_ids.contains(id)) continue;
        const QString path = resource.value(QStringLiteral("book_path")).toString();
        const QString source = workspace->workingText(id);
        if (regex_search) {
            auto it = pattern.globalMatch(source);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                if (match.capturedLength() == 0) {
                    return ToolResult::failure(QStringLiteral("ZERO_LENGTH_MATCH"),
                                               QStringLiteral("Zero-length regex matches are not supported by book.search_regex"),
                                               QJsonObject {{ QStringLiteral("zero_length_policy"),
                                                              QStringLiteral("reject") }});
                }
                if (!count_only && total_count >= offset && matches.size() < limit) {
                    RegexHit hit;
                    hit.offset = match.capturedStart();
                    hit.length = match.capturedLength();
                    hit.match = match.captured();
                    hit.line = source.left(hit.offset).count(QLatin1Char('\n')) + 1;
                    for (int capture = 1; capture <= match.lastCapturedIndex(); ++capture) {
                        hit.captures.append(match.captured(capture));
                    }
                    matches.append(regexHitsJson({hit}, id, path).first());
                }
                ++total_count;
            }
        } else {
            int from = 0;
            while (true) {
                const int found = source.indexOf(query, from, Qt::CaseInsensitive);
                if (found < 0) break;
                if (!count_only && total_count >= offset && matches.size() < limit) {
                    const int start = qMax(0, found - 24);
                    const QString snippet = source.mid(
                        start, qMin(source.size() - start, query.size() + 48));
                    matches.append(QJsonObject {
                        { QStringLiteral("resource_id"), id },
                        { QStringLiteral("book_path"), path },
                        { QStringLiteral("offset"), found },
                        { QStringLiteral("snippet"), snippet.left(MAX_LITERAL_SEARCH_SNIPPET_LENGTH) },
                        { QStringLiteral("snippet_offset"), start },
                        { QStringLiteral("snippet_length"), snippet.size() },
                        { QStringLiteral("match_length"), query.size() },
                        { QStringLiteral("snippet_truncated"),
                          snippet.size() > MAX_LITERAL_SEARCH_SNIPPET_LENGTH }
                    });
                }
                ++total_count;
                from = found + qMax(1, query.size());
            }
        }
    }
    offset = qMin(offset, total_count);
    const bool has_more = !count_only && offset + matches.size() < total_count;
    QJsonObject data {
        { QStringLiteral("matches"), matches },
        { QStringLiteral("match_count"), matches.size() },
        { QStringLiteral("max_matches"), limit },
        { QStringLiteral("match_limit_reached"), total_count >= limit },
        { QStringLiteral("query_length"), query.size() },
        { QStringLiteral("total_count"), total_count },
        { QStringLiteral("returned_count"), matches.size() },
        { QStringLiteral("has_more"), has_more },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("book_revision"), static_cast<qint64>(workspace->revision()) },
        { QStringLiteral("query_digest"), QString::fromLatin1(query_digest) },
        { QStringLiteral("zero_length_policy"), QStringLiteral("reject") }
    };
    if (has_more) {
        data.insert(QStringLiteral("next_cursor"), QString::fromLatin1(
            (QByteArray::number(offset + matches.size()) + ':' + snapshot)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    }
    return ToolResult::success(data);
}

QJsonObject paginatedSessionMemory(const AgentSession *session,
                                   const QJsonObject &arguments)
{
    const QJsonObject all = session->memory();
    const QStringList keys = session->memoryKeys();
    const int offset = qBound(
        0, arguments.value(QStringLiteral("offset")).toInt(0), keys.size());
    const int requested_limit = arguments.contains(QStringLiteral("limit"))
        ? arguments.value(QStringLiteral("limit")).toInt(
            DEFAULT_SESSION_MEMORY_PAGE_SIZE)
        : DEFAULT_SESSION_MEMORY_PAGE_SIZE;
    const int limit = qBound(
        1, requested_limit, MAX_SESSION_MEMORY_PAGE_SIZE);
    const int end = qMin(keys.size(), offset + limit);
    QJsonObject page;
    for (int index = offset; index < end; ++index) {
        const QString &key = keys.at(index);
        page.insert(key, all.value(key));
    }
    const bool has_more = end < keys.size();
    QJsonObject result {
        { QStringLiteral("memory"), page },
        { QStringLiteral("total_count"), keys.size() },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("has_more"), has_more }
    };
    if (has_more) result.insert(QStringLiteral("next_offset"), end);
    return result;
}

QJsonObject taskById(const QJsonArray &tasks, const QString &id)
{
    for (const QJsonValue &value : tasks) {
        const QJsonObject task = value.toObject();
        if (task.value(QStringLiteral("id")).toString() == id) return task;
    }
    return QJsonObject();
}

QJsonObject paginatedCheckpointList(const QJsonArray &all,
                                    const QJsonObject &arguments)
{
    QJsonObject result = paginatedArray(
        QStringLiteral("checkpoints"), all, arguments,
        DEFAULT_CHECKPOINT_PAGE_SIZE, MAX_CHECKPOINT_PAGE_SIZE);
    QJsonArray checkpoints;
    for (const QJsonValue &value : result.value(
             QStringLiteral("checkpoints")).toArray()) {
        QJsonObject checkpoint = value.toObject();
        const QString label = checkpoint.value(QStringLiteral("label")).toString();
        checkpoint.insert(QStringLiteral("label"),
                          label.left(MAX_CHECKPOINT_LABEL_LENGTH));
        checkpoint.insert(QStringLiteral("label_length"), label.size());
        checkpoint.insert(QStringLiteral("label_truncated"),
                          label.size() > MAX_CHECKPOINT_LABEL_LENGTH);
        if (checkpoint.contains(QStringLiteral("affected_resources"))) {
            const QJsonArray all_resources = checkpoint.value(
                QStringLiteral("affected_resources")).toArray();
            QJsonArray resources;
            bool id_truncated = false;
            const int count = qMin(
                all_resources.size(), MAX_CHECKPOINT_AFFECTED_RESOURCES);
            for (int index = 0; index < count; ++index) {
                const QString id = all_resources.at(index).toString();
                resources.append(id.left(MAX_CHECKPOINT_RESOURCE_ID_LENGTH));
                id_truncated = id_truncated
                    || id.size() > MAX_CHECKPOINT_RESOURCE_ID_LENGTH;
            }
            checkpoint.insert(QStringLiteral("affected_resources"), resources);
            checkpoint.insert(QStringLiteral("affected_resource_count"),
                              all_resources.size());
            checkpoint.insert(QStringLiteral("returned_affected_resource_count"),
                              resources.size());
            checkpoint.insert(QStringLiteral("affected_resource_ids_truncated"),
                              id_truncated);
            checkpoint.insert(QStringLiteral("affected_resources_truncated"),
                              id_truncated
                                  || all_resources.size()
                                      > MAX_CHECKPOINT_AFFECTED_RESOURCES);
        }
        checkpoints.append(checkpoint);
    }
    result.insert(QStringLiteral("checkpoints"), checkpoints);
    return result;
}

QJsonObject paginatedManuscriptSummary(QJsonObject summary,
                                       const QJsonObject &arguments)
{
    const QString template_illustrations_key = QStringLiteral(
        "template.illustrations");
    const QString template_chapters_key = QStringLiteral("template.chapters");
    const bool has_template = summary.contains(QStringLiteral("template"));
    QJsonObject template_data = summary.value(QStringLiteral("template")).toObject();
    summary.insert(template_illustrations_key,
                   template_data.value(QStringLiteral("illustrations")));
    summary.insert(template_chapters_key,
                   template_data.value(QStringLiteral("chapters")));
    summary = paginatedArrays(
        summary,
        { QStringLiteral("toc"), QStringLiteral("chapters"),
          QStringLiteral("front_illustrations"), QStringLiteral("illustrations"),
          QStringLiteral("images_in_book"), QStringLiteral("resolved_images"),
          template_illustrations_key, template_chapters_key },
        arguments, DEFAULT_MANUSCRIPT_SUMMARY_PAGE_SIZE,
        MAX_MANUSCRIPT_SUMMARY_PAGE_SIZE);
    template_data.insert(QStringLiteral("illustrations"),
                         summary.take(template_illustrations_key));
    template_data.insert(QStringLiteral("chapters"),
                         summary.take(template_chapters_key));
    if (has_template) summary.insert(QStringLiteral("template"), template_data);
    return summary;
}

QJsonObject paginatedTransactionPreview(QJsonObject preview,
                                        const QJsonObject &arguments)
{
    QList<QJsonObject> ordered_changes;
    for (const QJsonValue &value : preview.value(
             QStringLiteral("changes")).toArray()) {
        ordered_changes.append(value.toObject());
    }
    std::sort(ordered_changes.begin(), ordered_changes.end(),
              [](const QJsonObject &left, const QJsonObject &right) {
        const QString left_id = left.value(
            QStringLiteral("resource_id")).toString();
        const QString right_id = right.value(
            QStringLiteral("resource_id")).toString();
        if (left_id != right_id) return left_id < right_id;
        return QJsonDocument(left).toJson(QJsonDocument::Compact)
            < QJsonDocument(right).toJson(QJsonDocument::Compact);
    });
    QJsonArray changes;
    for (const QJsonObject &change : ordered_changes) changes.append(change);
    preview.insert(QStringLiteral("changes"), changes);

    QStringList ordered_removals;
    for (const QJsonValue &value : preview.value(
             QStringLiteral("removed")).toArray()) {
        ordered_removals.append(value.toString());
    }
    std::sort(ordered_removals.begin(), ordered_removals.end());
    preview.insert(QStringLiteral("removed"),
                   QJsonArray::fromStringList(ordered_removals));

    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(preview).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
    preview = paginatedArrays(
        preview,
        { QStringLiteral("changes"), QStringLiteral("removed") },
        arguments, DEFAULT_TRANSACTION_PREVIEW_PAGE_SIZE,
        MAX_TRANSACTION_PREVIEW_PAGE_SIZE);
    preview.insert(QStringLiteral("preview_digest"),
                   QString::fromLatin1(digest));
    return preview;
}

QString metadataValueText(const QJsonValue &value)
{
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                              : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 17);
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(
            QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(
            QJsonDocument::Compact));
    }
    return QString();
}

bool metadataNameMatchesField(const QString &name, const QString &field)
{
    return name == field || name.endsWith(QLatin1Char(':') + field);
}

QJsonArray effectiveMetadataEntries(const QJsonObject &metadata)
{
    QJsonArray entries;
    const QJsonArray stored_entries = metadata.value(
        QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : stored_entries) {
        if (!value.isObject()) continue;
        const QJsonObject stored = value.toObject();
        entries.append(QJsonObject {
            { QStringLiteral("name"), metadataValueText(
                  stored.value(QStringLiteral("name"))) },
            { QStringLiteral("content"), metadataValueText(
                  stored.value(QStringLiteral("content"))) }
        });
    }

    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        if (it.key() == QLatin1String("entries")
            || it.key() == QLatin1String("book_revision")) {
            continue;
        }
        const QString content = metadataValueText(it.value());
        bool replaced = false;
        for (int index = 0; index < entries.size(); ++index) {
            QJsonObject entry = entries.at(index).toObject();
            if (!metadataNameMatchesField(
                    entry.value(QStringLiteral("name")).toString(), it.key())) {
                continue;
            }
            entry.insert(QStringLiteral("content"), content);
            entries.replace(index, entry);
            replaced = true;
            break;
        }
        if (!replaced) {
            entries.append(QJsonObject {
                { QStringLiteral("name"), it.key() },
                { QStringLiteral("content"), content }
            });
        }
    }
    return entries;
}

QString metadataDigest(const QJsonArray &entries)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("sigil-agent-metadata-v1:"));
    hash.addData(QJsonDocument(entries).toJson(QJsonDocument::Compact));
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject boundedMetadataEntry(const QJsonObject &entry, int index)
{
    const QString name = entry.value(QStringLiteral("name")).toString();
    const QString content = entry.value(QStringLiteral("content")).toString();
    return QJsonObject {
        { QStringLiteral("index"), index },
        { QStringLiteral("name"), name.left(MAX_METADATA_NAME_PREVIEW_LENGTH) },
        { QStringLiteral("name_length"), name.size() },
        { QStringLiteral("name_truncated"),
          name.size() > MAX_METADATA_NAME_PREVIEW_LENGTH },
        { QStringLiteral("content"), content.left(MAX_METADATA_PREVIEW_LENGTH) },
        { QStringLiteral("content_length"), content.size() },
        { QStringLiteral("content_truncated"),
          content.size() > MAX_METADATA_PREVIEW_LENGTH },
        { QStringLiteral("content_hash"), QString::fromLatin1(
              QCryptographicHash::hash(content.toUtf8(),
                                       QCryptographicHash::Sha256).toHex()) }
    };
}

QJsonObject paginatedMetadata(const QJsonObject &metadata,
                              const QJsonObject &arguments)
{
    const QJsonArray entries = effectiveMetadataEntries(metadata);
    const int offset = qBound(
        0, arguments.value(QStringLiteral("offset")).toInt(0), entries.size());
    const int requested_limit = arguments.contains(QStringLiteral("limit"))
        ? arguments.value(QStringLiteral("limit")).toInt(
              DEFAULT_METADATA_PAGE_SIZE)
        : DEFAULT_METADATA_PAGE_SIZE;
    const int limit = qBound(1, requested_limit, MAX_METADATA_PAGE_SIZE);
    const int end = qMin(entries.size(), offset + limit);
    QJsonArray page;
    for (int index = offset; index < end; ++index) {
        page.append(boundedMetadataEntry(entries.at(index).toObject(), index));
    }

    QJsonObject result {
        { QStringLiteral("book_revision"),
          metadata.value(QStringLiteral("book_revision")) },
        { QStringLiteral("metadata_digest"), metadataDigest(entries) },
        { QStringLiteral("entries"), page },
        { QStringLiteral("total_count"), entries.size() },
        { QStringLiteral("offset"), offset },
        { QStringLiteral("limit"), limit },
        { QStringLiteral("returned_count"), page.size() },
        { QStringLiteral("has_more"), end < entries.size() }
    };
    if (end < entries.size()) result.insert(QStringLiteral("next_offset"), end);

    const QStringList summary_fields {
        QStringLiteral("title"), QStringLiteral("language"),
        QStringLiteral("creator"), QStringLiteral("contributor"),
        QStringLiteral("publisher"), QStringLiteral("description"),
        QStringLiteral("subject"), QStringLiteral("date"),
        QStringLiteral("identifier"), QStringLiteral("rights"),
        QStringLiteral("source"), QStringLiteral("coverage"),
        QStringLiteral("type"), QStringLiteral("format"),
        QStringLiteral("relation")
    };
    QJsonArray truncated_fields;
    for (const QString &field : summary_fields) {
        if (!metadata.contains(field)) continue;
        const QString text = metadataValueText(metadata.value(field));
        result.insert(field, text.left(MAX_METADATA_PREVIEW_LENGTH));
        if (text.size() > MAX_METADATA_PREVIEW_LENGTH) {
            truncated_fields.append(field);
        }
    }
    result.insert(QStringLiteral("summary_truncated_fields"), truncated_fields);
    return result;
}

class LambdaTool : public IAgentTool
{
public:
    using Fn = std::function<ToolResult(const QJsonObject &)>;
    LambdaTool(AgentToolDescriptor descriptor, Fn fn) :
        m_descriptor(std::move(descriptor)),
        m_fn(std::move(fn))
    {
    }

    AgentToolDescriptor descriptor() const override { return m_descriptor; }
    ToolResult execute(const QJsonObject &arguments) override { return m_fn(arguments); }

private:
    AgentToolDescriptor m_descriptor;
    Fn m_fn;
};

ToolResult fromBook(const BookOpResult &result)
{
    if (!result.ok) {
        return ToolResult::failure(result.code, result.message, result.data);
    }
    return ToolResult::success(result.data, result.applied, result.previewOnly);
}

void add(ToolRegistry *registry,
         const QString &name,
         const QString &description,
         ToolRisk risk,
         bool mutates,
         bool preview,
         const QJsonObject &schema,
         LambdaTool::Fn fn)
{
    AgentToolDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = description;
    descriptor.inputSchema = schema;
    descriptor.risk = risk;
    descriptor.mutatesBook = mutates;
    descriptor.supportsPreview = preview;
    registry->add(std::make_unique<LambdaTool>(descriptor, std::move(fn)));
}

} // namespace

QString humanReadableImpact(const QString &name, const QJsonObject &arguments)
{
    if (name == QLatin1String("resource.patch_fragment")) {
        const QString expected = arguments.value(QStringLiteral("expected_text")).toString();
        const QString snippet = expected.size() > 40 ? expected.left(40) + QStringLiteral("…") : expected;
        return QStringLiteral("Replace %1 in %2. Staged until transaction.commit; reversible via Undo after apply.")
            .arg(snippet.isEmpty() ? QStringLiteral("a fragment") : QStringLiteral("“%1”").arg(snippet),
                 arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("css.update_rules")) {
        return QStringLiteral("Replace stylesheet %1. Staged until commit; reversible via Undo.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("metadata.update")) {
        return QStringLiteral("Update package metadata fields. Staged until commit.");
    }
    if (name == QLatin1String("transaction.commit")) {
        return QStringLiteral("Commit staged EPUB edits to the open book (revision %1). Already-committed steps stay in Undo.")
            .arg(arguments.value(QStringLiteral("expected_book_revision"))
                     .toInteger(arguments.value(QStringLiteral("expected_revision")).toInteger()));
    }
    if (name == QLatin1String("checkpoint.restore")) {
        return QStringLiteral("Restore checkpoint %1, replacing live book content.")
            .arg(arguments.value(QStringLiteral("checkpoint_id")).toString());
    }
    if (name == QLatin1String("resource.create")) {
        return QStringLiteral("Create %1. Staged until commit; reversible via Undo after apply.")
            .arg(arguments.value(QStringLiteral("book_path")).toString());
    }
    if (name == QLatin1String("resource.copy")) {
        return QStringLiteral("Copy %1 to a new resource. Staged until commit; reversible via Undo after apply.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("resource.replace_text")) {
        return QStringLiteral("Replace all text of %1. Staged until commit; reversible via Undo after apply.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("content.typeset_from_manuscript")) {
        return QStringLiteral("Fill the open light-novel template from the dropped manuscript. Staged until commit.");
    }
    if (name == QLatin1String("content.fill_section")) {
        return QStringLiteral("Fill %1 from the parsed manuscript. Staged until commit.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("resource.delete")) {
        return QStringLiteral("Delete %1. Staged until commit.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString());
    }
    if (name == QLatin1String("resource.rename")) {
        return QStringLiteral("Rename %1 to %2. Staged until commit.")
            .arg(arguments.value(QStringLiteral("resource_id")).toString(),
                 arguments.value(QStringLiteral("book_path")).toString());
    }
    if (name == QLatin1String("spine.set") || name == QLatin1String("spine.sort")) {
        return QStringLiteral("Reorder the spine. Staged until commit.");
    }
    if (name == QLatin1String("style.link")) {
        return QStringLiteral("Link stylesheets. Staged until commit.");
    }
    if (name == QLatin1String("python.run")) {
        return QStringLiteral("Run a Live Python v2 snippet on the open book. Applies immediately (not staged).");
    }
    if (name == QLatin1String("content.split") || name == QLatin1String("content.merge")) {
        return QStringLiteral("Restructure chapters. Staged until commit.");
    }
    if (name == QLatin1String("content.replace_regex") || name == QLatin1String("content.wrap")) {
        return QStringLiteral("Batch edit matching text. Staged until commit.");
    }
    if (name == QLatin1String("paragraphs.apply")) {
        return QStringLiteral("Stage reviewed DIV paragraph plan %1 with digest %2 for book revision %3. "
                              "The live book stays unchanged until transaction.commit.")
            .arg(arguments.value(QStringLiteral("plan_id")).toString(),
                 arguments.value(QStringLiteral("plan_digest")).toString(),
                 QString::number(arguments.value(QStringLiteral("expected_book_revision")).toInteger()));
    }
    if (name == QLatin1String("proof.apply")) {
        return QStringLiteral("Stage reviewed proofreading plan %1 with digest %2 for book revision %3. "
                              "The live book stays unchanged until transaction.commit.")
            .arg(arguments.value(QStringLiteral("plan_id")).toString(),
                 arguments.value(QStringLiteral("plan_digest")).toString(),
                 QString::number(arguments.value(QStringLiteral("expected_book_revision")).toInteger()));
    }
    if (name == QLatin1String("proof.configure")) {
        const QJsonObject scope = arguments.value(QStringLiteral("scope")).toObject();
        return QStringLiteral("Save local proofreading conventions for %1%2 and invalidate prior audit snapshots. The EPUB is unchanged.")
            .arg(scope.value(QStringLiteral("kind")).toString(),
                 scope.value(QStringLiteral("resource_id")).toString().isEmpty()
                     ? QString() : QStringLiteral(" %1").arg(scope.value(QStringLiteral("resource_id")).toString()));
    }
    if (name == QLatin1String("proof.decide")) {
        return QStringLiteral("Record local proofreading decision %1 for issue %2. The EPUB is unchanged.")
            .arg(arguments.value(QStringLiteral("decision")).toString(),
                 arguments.value(QStringLiteral("issue_id")).toString());
    }
    if (name == QLatin1String("toc.apply_transform")) {
        return QStringLiteral("Stage reviewed native TOC hierarchy plan %1 with digest %2 for book revision %3. "
                              "This reparents Nav/NCX entries only; labels, targets, preorder, XHTML headings, and the live book remain unchanged until transaction.commit.")
            .arg(arguments.value(QStringLiteral("plan_id")).toString(),
                 arguments.value(QStringLiteral("plan_digest")).toString(),
                 QString::number(arguments.value(QStringLiteral("expected_book_revision")).toInteger()));
    }
    return QStringLiteral("Run %1 on the current book.").arg(name);
}

void registerBookTools(ToolRegistry *registry, IBookWorkspace *workspace, AgentSession *session)
{
    if (!registry || !workspace) return;

    const auto proof_audit = std::make_shared<ProofAudit>();

    add(registry, QStringLiteral("proof.audit"),
        QStringLiteral("Scan visible XHTML body text for review candidates. The default ruleset reports replacement/invisible characters and repeated Chinese comma/full stop. Supports whole_book, file, selection, resources, and zero-based spine ranges. Follow next_cursor until has_more is false. Offsets are UTF-16 XHTML source positions; candidates spanning markup or entities require source inspection. Results are suggestions, not confirmed errors."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("scope"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("object") },
                    { QStringLiteral("properties"), QJsonObject {
                        { QStringLiteral("kind"), QJsonObject {
                            { QStringLiteral("type"), QStringLiteral("string") },
                            { QStringLiteral("enum"), QJsonArray { QStringLiteral("whole_book"), QStringLiteral("file"), QStringLiteral("selection"), QStringLiteral("resources"), QStringLiteral("spine") } }
                        } },
                        { QStringLiteral("resource_id"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                        { QStringLiteral("resource_ids"), QJsonObject { { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} } } },
                        { QStringLiteral("start"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} },
                        { QStringLiteral("end"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} },
                        { QStringLiteral("start_index"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} },
                        { QStringLiteral("end_index"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} }
                    } }
                } },
                { QStringLiteral("ruleset"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("enum"), QJsonArray { QStringLiteral("default") } } } },
                { QStringLiteral("limit"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1 }, { QStringLiteral("maximum"), 50 }, { QStringLiteral("default"), 20 } } },
                { QStringLiteral("cursor"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
            } }
        },
        [workspace, proof_audit](const QJsonObject &arguments) {
            return proof_audit->audit(workspace, arguments);
        });

    add(registry, QStringLiteral("proof.settings"),
        QStringLiteral("Read the local, book-bound proofreading configuration: allowed repeated punctuation, allowed terms, and explicitly configured term variants. No EPUB content or body text is stored here."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace, proof_audit](const QJsonObject &) {
            return proof_audit->settings(workspace);
        });

    add(registry, QStringLiteral("proof.configure"),
        QStringLiteral("Save user-reviewed proofreading conventions for this book or one XHTML file. allow_repeats may contain ，， or 。。; allowed_terms suppress configured variant candidates inside exact visible terms; variant_pairs contain observed/preferred text and create low-confidence review candidates. Supplied arrays replace that field; [] clears it. Settings stay local and invalidate prior audit cursors, decisions and plans."),
        ToolRisk::ReversibleEdit, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("scope"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("object") },
                    { QStringLiteral("properties"), QJsonObject {
                        { QStringLiteral("kind"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("enum"), QJsonArray { QStringLiteral("book"), QStringLiteral("file") } } } },
                        { QStringLiteral("resource_id"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
                    } },
                    { QStringLiteral("required"), QJsonArray { QStringLiteral("kind") } }
                } },
                { QStringLiteral("allow_repeats"), QJsonObject { { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }, { QStringLiteral("maxItems"), 2 } } },
                { QStringLiteral("allowed_terms"), QJsonObject { { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }, { QStringLiteral("maxItems"), 100 } } },
                { QStringLiteral("variant_pairs"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("maxItems"), 32 },
                    { QStringLiteral("items"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("object") },
                        { QStringLiteral("properties"), QJsonObject {
                            { QStringLiteral("observed"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                            { QStringLiteral("preferred"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
                        } },
                        { QStringLiteral("required"), QJsonArray { QStringLiteral("observed"), QStringLiteral("preferred") } }
                    } }
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("scope") } }
        },
        [workspace, proof_audit](const QJsonObject &arguments) {
            return proof_audit->configure(workspace, arguments);
        });

    add(registry, QStringLiteral("proof.decide"),
        QStringLiteral("Record a review decision for one issue from the current proof.audit snapshot. accept needs a reviewed replacement when no suggestion exists; pass an explicit empty replacement to delete. ignore and pending are also supported. Records are local to the book and never modify the EPUB."),
        ToolRisk::ReversibleEdit, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("issue_id"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                { QStringLiteral("decision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("enum"), QJsonArray { QStringLiteral("accept"), QStringLiteral("ignore"), QStringLiteral("pending") } } } },
                { QStringLiteral("replacement"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }, { QStringLiteral("maxLength"), 256 }} },
                { QStringLiteral("reviewer"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("issue_id"), QStringLiteral("decision") } }
        },
        [workspace, proof_audit](const QJsonObject &arguments) {
            return proof_audit->decide(workspace, arguments);
        });

    add(registry, QStringLiteral("proof.plan"),
        QStringLiteral("Create a bounded, paginated plan from 1 to 100 explicitly accepted issue IDs in the current proof.audit snapshot. Review every items page with next_cursor before proof.apply. Markup-spanning or overlapping candidates are rejected. The book is unchanged."),
        ToolRisk::Read, false, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("accepted_issue_ids"), QJsonObject { { QStringLiteral("type"), QStringLiteral("array") }, { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} } } },
                { QStringLiteral("limit"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") }, { QStringLiteral("minimum"), 1 }, { QStringLiteral("maximum"), 20 }, { QStringLiteral("default"), 20 } } },
                { QStringLiteral("cursor"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
            } }
        },
        [workspace, proof_audit](const QJsonObject &arguments) {
            return proof_audit->plan(workspace, arguments);
        });

    add(registry, QStringLiteral("proof.apply"),
        QStringLiteral("Stage exactly the fully reviewed proof.plan in a new exclusive transaction. Requires plan_id, plan_digest and expected_book_revision. Rechecks all source hashes; never commits itself. Then read all transaction.preview pages and call transaction.commit. Accepted style suggestions require user review."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("plan_id"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                { QStringLiteral("plan_digest"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                { QStringLiteral("expected_book_revision"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("plan_id"), QStringLiteral("plan_digest"), QStringLiteral("expected_book_revision") } }
        },
        [workspace, proof_audit](const QJsonObject &arguments) {
            return proof_audit->apply(workspace, arguments);
        });

    add(registry, QStringLiteral("book.summary"),
        QStringLiteral("Summarize the open EPUB: revision, bounded version/title/language previews, spine/TOC counts, and resource totals. Use book.metadata when title or language is truncated. Never returns file binaries."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(workspace->summary());
        });

    add(registry, QStringLiteral("book.resources"),
        QStringLiteral("List a bounded page of manifest resources with id, path, media type and kind. Font/image bytes are omitted. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArray(
                QStringLiteral("resources"), workspace->resources(), arguments,
                DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE));
        });

    add(registry, QStringLiteral("book.spine"),
        QStringLiteral("List a bounded page of spine reading order. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArray(
                QStringLiteral("spine"), workspace->spine(), arguments,
                DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE));
        });

    add(registry, QStringLiteral("book.toc"),
        QStringLiteral("List a bounded page of table of contents entries. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArray(
                QStringLiteral("toc"), workspace->toc(), arguments,
                DEFAULT_INVENTORY_PAGE_SIZE, MAX_INVENTORY_PAGE_SIZE));
        });

    add(registry, QStringLiteral("book.metadata"),
        QStringLiteral("Read a bounded page of package metadata previews such as title, language and creator. Use next_offset while has_more is true; use metadata.read_fragment for truncated content."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_METADATA_PAGE_SIZE, MAX_METADATA_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedMetadata(
                workspace->metadata(), arguments));
        });

    add(registry, QStringLiteral("metadata.read_fragment"),
        QStringLiteral("Read an exact bounded fragment of one metadata entry selected from book.metadata. Reuse the same metadata_digest and entry index; continue at continuation while truncated is true."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("index"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 0 }
                } },
                { QStringLiteral("metadata_digest"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("minLength"), 64 },
                    { QStringLiteral("maxLength"), 64 }
                } },
                { QStringLiteral("offset"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 0 },
                    { QStringLiteral("default"), 0 }
                } },
                { QStringLiteral("limit"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 1 },
                    { QStringLiteral("maximum"), MAX_METADATA_FRAGMENT_LENGTH },
                    { QStringLiteral("default"), DEFAULT_METADATA_FRAGMENT_LENGTH }
                } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("index"), QStringLiteral("metadata_digest")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QJsonArray entries = effectiveMetadataEntries(
                workspace->metadata());
            const QString digest = metadataDigest(entries);
            const QString expected_digest = arguments.value(
                QStringLiteral("metadata_digest")).toString();
            if (expected_digest != digest) {
                return ToolResult::failure(
                    QStringLiteral("METADATA_CHANGED"),
                    QStringLiteral("Package metadata changed; restart book.metadata at offset 0"),
                    QJsonObject {
                        { QStringLiteral("expected_metadata_digest"),
                          expected_digest },
                        { QStringLiteral("actual_metadata_digest"), digest }
                    });
            }
            const int index = arguments.value(QStringLiteral("index")).toInt(-1);
            if (index < 0 || index >= entries.size()) {
                return ToolResult::failure(
                    QStringLiteral("METADATA_ENTRY_NOT_FOUND"),
                    QStringLiteral("Metadata entry index is outside the current inventory"),
                    QJsonObject {
                        { QStringLiteral("index"), index },
                        { QStringLiteral("total_count"), entries.size() },
                        { QStringLiteral("metadata_digest"), digest }
                    });
            }
            const QJsonObject entry = entries.at(index).toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const QString content = entry.value(
                QStringLiteral("content")).toString();
            const int offset = qBound(
                0, arguments.value(QStringLiteral("offset")).toInt(0),
                content.size());
            const int requested_limit = arguments.contains(QStringLiteral("limit"))
                ? arguments.value(QStringLiteral("limit")).toInt(
                      DEFAULT_METADATA_FRAGMENT_LENGTH)
                : DEFAULT_METADATA_FRAGMENT_LENGTH;
            const int limit = qBound(
                1, requested_limit, MAX_METADATA_FRAGMENT_LENGTH);
            const QString text = content.mid(offset, limit);
            const int end = offset + text.size();
            const bool truncated = end < content.size();
            QJsonObject data {
                { QStringLiteral("index"), index },
                { QStringLiteral("name"),
                  name.left(MAX_METADATA_NAME_PREVIEW_LENGTH) },
                { QStringLiteral("name_length"), name.size() },
                { QStringLiteral("name_truncated"),
                  name.size() > MAX_METADATA_NAME_PREVIEW_LENGTH },
                { QStringLiteral("metadata_digest"), digest },
                { QStringLiteral("content_hash"), QString::fromLatin1(
                      QCryptographicHash::hash(content.toUtf8(),
                                               QCryptographicHash::Sha256).toHex()) },
                { QStringLiteral("offset"), offset },
                { QStringLiteral("end"), end },
                { QStringLiteral("length"), text.size() },
                { QStringLiteral("total"), content.size() },
                { QStringLiteral("limit"), limit },
                { QStringLiteral("truncated"), truncated },
                { QStringLiteral("text"), text }
            };
            if (truncated) data.insert(QStringLiteral("continuation"), end);
            return ToolResult::success(data);
        });

    QJsonObject search_schema {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject {
            { QStringLiteral("query"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("string") },
                { QStringLiteral("minLength"), 1 },
                { QStringLiteral("maxLength"), MAX_LITERAL_SEARCH_QUERY_LENGTH }
            } },
            { QStringLiteral("max_matches"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 1 },
                { QStringLiteral("maximum"), MAX_LITERAL_SEARCH_MATCHES },
                { QStringLiteral("default"), DEFAULT_LITERAL_SEARCH_MATCHES }
            } },
            { QStringLiteral("limit"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 1 },
                { QStringLiteral("maximum"), MAX_LITERAL_SEARCH_MATCHES }
            } },
            { QStringLiteral("cursor"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
            { QStringLiteral("count_only"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("boolean") }} },
            { QStringLiteral("resource_id"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
            { QStringLiteral("resource_ids"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("array") },
                { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
            } }
        } },
        { QStringLiteral("required"), QJsonArray { QStringLiteral("query") } }
    };
    add(registry, QStringLiteral("book.search"),
        QStringLiteral("Case-insensitive literal search over text resources. Query length is capped at 512 characters. Follow next_cursor while has_more to inspect all matches; count_only returns an exact total without snippets. Each page returns at most 50 bounded matches with UTF-16 source offsets. Use resource.read_fragment for exact source."),
        ToolRisk::Read, false, false, search_schema,
        [workspace](const QJsonObject &arguments) {
            return paginatedSearch(workspace, arguments, false);
        });

    QJsonObject fragment_schema {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject {
            { QStringLiteral("resource_id"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("string") },
                { QStringLiteral("minLength"), 1 }
            } },
            { QStringLiteral("offset"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 0 },
                { QStringLiteral("default"), 0 }
            } },
            { QStringLiteral("limit"), QJsonObject {
                { QStringLiteral("type"), QStringLiteral("integer") },
                { QStringLiteral("minimum"), 1 },
                { QStringLiteral("maximum"), MAX_RESOURCE_FRAGMENT_LENGTH },
                { QStringLiteral("default"), DEFAULT_RESOURCE_FRAGMENT_LENGTH }
            } }
        } },
        { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
    };
    add(registry, QStringLiteral("resource.read_fragment"),
        QStringLiteral("Read a bounded text fragment of an XHTML or CSS resource, including staged edits during an Agent transaction. Follow continuation while truncated is true. Copy text into patch_fragment.expected_text and resource_revision into patch_fragment.expected_resource_revision; book_revision is for transaction.commit. lines gives 1-based start_line for repeated substrings. Fonts and images are refused."),
        ToolRisk::Read, false, false, fragment_schema,
        [workspace](const QJsonObject &arguments) {
            const int offset = qMax(
                0, arguments.value(QStringLiteral("offset")).toInt(0));
            const int requested_limit = arguments.contains(QStringLiteral("limit"))
                ? arguments.value(QStringLiteral("limit")).toInt(
                      DEFAULT_RESOURCE_FRAGMENT_LENGTH)
                : DEFAULT_RESOURCE_FRAGMENT_LENGTH;
            const int limit = qBound(
                1, requested_limit, MAX_RESOURCE_FRAGMENT_LENGTH);
            return fromBook(workspace->readFragment(
                arguments.value(QStringLiteral("resource_id")).toString(),
                offset, limit));
        });

    add(registry, QStringLiteral("style.stylesheets"),
        QStringLiteral("List a bounded page of CSS stylesheets with bounded text. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_STYLESHEET_PAGE_SIZE, MAX_STYLESHEET_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArray(
                QStringLiteral("stylesheets"), workspace->stylesheets(), arguments,
                DEFAULT_STYLESHEET_PAGE_SIZE, MAX_STYLESHEET_PAGE_SIZE));
        });

    add(registry, QStringLiteral("font.inventory"),
        QStringLiteral("List a bounded shared-offset page of embedded fonts, CSS font-family references, and declared family names. Never returns font file bytes. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_DIAGNOSTIC_PAGE_SIZE, MAX_DIAGNOSTIC_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArrays(
                workspace->fontInventory(),
                { QStringLiteral("embedded_fonts"),
                  QStringLiteral("css_families"),
                  QStringLiteral("declared_families") },
                arguments, DEFAULT_DIAGNOSTIC_PAGE_SIZE,
                MAX_DIAGNOSTIC_PAGE_SIZE));
        });

    add(registry, QStringLiteral("book.validate"),
        QStringLiteral("Run structural checks on the open book (spine, body, basic CSS) and return a bounded page of issues. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_DIAGNOSTIC_PAGE_SIZE, MAX_DIAGNOSTIC_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArrays(
                workspace->validate(), { QStringLiteral("issues") }, arguments,
                DEFAULT_DIAGNOSTIC_PAGE_SIZE, MAX_DIAGNOSTIC_PAGE_SIZE));
        });

    add(registry, QStringLiteral("transaction.begin"),
        QStringLiteral("Begin a staged transaction. Later patches stay off the live book until commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("label"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->beginTransaction(
                arguments.value(QStringLiteral("label")).toString()));
        });

    add(registry, QStringLiteral("transaction.preview"),
        QStringLiteral("Preview a bounded shared-offset page of staged changes and removals without mutating the live book. Read every page using next_offset before commit, without staging more edits between pages; preview_digest must remain unchanged."),
        ToolRisk::Read, false, true,
        paginationSchema(DEFAULT_TRANSACTION_PREVIEW_PAGE_SIZE,
                         MAX_TRANSACTION_PREVIEW_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            BookOpResult preview = workspace->previewTransaction();
            if (preview.ok) {
                preview.data = paginatedTransactionPreview(
                    preview.data, arguments);
            }
            return fromBook(preview);
        });

    add(registry, QStringLiteral("transaction.commit"),
        QStringLiteral("Commit staged changes. Pass expected_book_revision from book.summary or transaction.preview.live_book_revision (legacy expected_revision is accepted). Fails with BOOK_REVISION_CONFLICT if the live book changed since the transaction began."),
        ToolRisk::Bulk, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("expected_book_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const bool has_book = arguments.contains(QStringLiteral("expected_book_revision"));
            const bool has_legacy = arguments.contains(QStringLiteral("expected_revision"));
            if (!has_book && !has_legacy) {
                return ToolResult::failure(QStringLiteral("EXPECTED_BOOK_REVISION_REQUIRED"),
                    QStringLiteral("Pass expected_book_revision from the current book or transaction preview."));
            }
            const qint64 book_value = arguments.value(QStringLiteral("expected_book_revision")).toInteger(-1);
            const qint64 legacy_value = arguments.value(QStringLiteral("expected_revision")).toInteger(-1);
            if (has_book && has_legacy && book_value != legacy_value) {
                return ToolResult::failure(QStringLiteral("REVISION_ARGUMENT_CONFLICT"),
                    QStringLiteral("expected_book_revision and expected_revision disagree."));
            }
            if ((has_book ? book_value : legacy_value) < 0) {
                return ToolResult::failure(QStringLiteral("EXPECTED_BOOK_REVISION_REQUIRED"),
                    QStringLiteral("expected_book_revision must be a non-negative integer."));
            }
            const quint64 expected = static_cast<quint64>(has_book ? book_value : legacy_value);
            return fromBook(workspace->commitTransaction(expected));
        });

    add(registry, QStringLiteral("transaction.rollback"),
        QStringLiteral("Discard the open staged transaction. Live book is unchanged."),
        ToolRisk::ReversibleEdit, true, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return fromBook(workspace->rollbackTransaction());
        });

    add(registry, QStringLiteral("resource.patch_fragment"),
        QStringLiteral("Stage a bounded replacement of an exact current substring. Pass expected_resource_revision from resource.read_fragment.resource_revision (legacy expected_revision is accepted); this is NOT the book revision used by transaction.commit. expected_text must be copied from read_fragment.text; each text is capped at 8192 UTF-16 units. If repeated, pass start_line from read_fragment.lines. Optional start/end are ignored unless they exactly match. Must not cut through markup. Reads during a transaction see staged text; Live book changes only at commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_text"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("maxLength"), MAX_PATCH_FRAGMENT_LENGTH }
                } },
                { QStringLiteral("text"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("maxLength"), MAX_PATCH_FRAGMENT_LENGTH }
                } },
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("expected_resource_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("start_line"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("start"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("end"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("expected_text"),
                QStringLiteral("text")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString expected_text = arguments.value(
                QStringLiteral("expected_text")).toString();
            if (expected_text.size() > MAX_PATCH_FRAGMENT_LENGTH) {
                return ToolResult::failure(
                    QStringLiteral("PATCH_EXPECTED_TEXT_TOO_LARGE"),
                    QStringLiteral("expected_text is capped at 8192 UTF-16 units. Read and patch a smaller fragment."),
                    QJsonObject {
                        { QStringLiteral("expected_text_length"),
                          expected_text.size() },
                        { QStringLiteral("max_expected_text_length"),
                          MAX_PATCH_FRAGMENT_LENGTH }
                    });
            }
            const QString replacement = arguments.value(
                QStringLiteral("text")).toString();
            if (replacement.size() > MAX_PATCH_FRAGMENT_LENGTH) {
                return ToolResult::failure(
                    QStringLiteral("PATCH_REPLACEMENT_TOO_LARGE"),
                    QStringLiteral("replacement text is capped at 8192 UTF-16 units. Use smaller patches."),
                    QJsonObject {
                        { QStringLiteral("replacement_length"),
                          replacement.size() },
                        { QStringLiteral("max_replacement_length"),
                          MAX_PATCH_FRAGMENT_LENGTH }
                    });
            }
            const bool has_resource = arguments.contains(QStringLiteral("expected_resource_revision"));
            const bool has_legacy = arguments.contains(QStringLiteral("expected_revision"));
            if (!has_resource && !has_legacy) {
                return ToolResult::failure(QStringLiteral("EXPECTED_RESOURCE_REVISION_REQUIRED"),
                    QStringLiteral("Read the resource and pass resource_revision as expected_resource_revision."));
            }
            const qint64 resource_value = arguments.value(QStringLiteral("expected_resource_revision")).toInteger(-1);
            const qint64 legacy_value = arguments.value(QStringLiteral("expected_revision")).toInteger(-1);
            if (has_resource && has_legacy && resource_value != legacy_value) {
                return ToolResult::failure(QStringLiteral("REVISION_ARGUMENT_CONFLICT"),
                    QStringLiteral("expected_resource_revision and expected_revision disagree."));
            }
            if ((has_resource ? resource_value : legacy_value) < 0) {
                return ToolResult::failure(QStringLiteral("EXPECTED_RESOURCE_REVISION_REQUIRED"),
                    QStringLiteral("expected_resource_revision must be a non-negative integer."));
            }
            const QString resource_id = arguments.value(QStringLiteral("resource_id")).toString();
            const quint64 expected = static_cast<quint64>(has_resource ? resource_value : legacy_value);
            const BookOpResult result = workspace->patchFragment(
                resource_id,
                arguments.value(QStringLiteral("start")).toInt(-1),
                arguments.value(QStringLiteral("end")).toInt(-1),
                replacement,
                expected,
                expected_text,
                arguments.value(QStringLiteral("start_line")).toInt(-1));
            if (!result.ok
                && (result.code == QLatin1String("RESOURCE_REVISION_CONFLICT")
                    || result.code == QLatin1String("BOOK_REVISION_CONFLICT"))) {
                const BookOpResult current = workspace->readFragment(resource_id, 0, 1);
                const qint64 actual = current.ok
                    ? current.data.value(QStringLiteral("resource_revision")).toInteger(-1)
                    : static_cast<qint64>(workspace->resourceRevision(resource_id));
                QJsonObject details {
                    { QStringLiteral("resource_id"), resource_id },
                    { QStringLiteral("expected_resource_revision"), static_cast<qint64>(expected) },
                    { QStringLiteral("actual_resource_revision"), actual },
                    { QStringLiteral("book_revision"), static_cast<qint64>(workspace->revision()) }
                };
                if (result.code == QLatin1String("BOOK_REVISION_CONFLICT")) {
                    details.insert(QStringLiteral("legacy_code"), result.code);
                }
                return ToolResult::failure(QStringLiteral("RESOURCE_REVISION_CONFLICT"),
                    QStringLiteral("Resource revision changed. Re-read this resource before retrying; do not use the book revision."),
                    details);
            }
            return fromBook(result);
        });

    add(registry, QStringLiteral("css.update_rules"),
        QStringLiteral("Stage a full replacement of a CSS stylesheet. Live book unchanged until commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("text"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("text"), QStringLiteral("expected_revision")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->updateCss(
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("text")).toString(),
                static_cast<quint64>(arguments.value(QStringLiteral("expected_revision")).toInteger())));
        });

    add(registry, QStringLiteral("metadata.update"),
        QStringLiteral("Stage package metadata. patch is a map of Dublin Core fields (title, language, creator, contributor, publisher, description, subject, date, identifier, rights, …). Pass _remove: [\"subject\"] to delete fields."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("patch"), QJsonObject { { QStringLiteral("type"), QStringLiteral("object") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("patch") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->updateMetadata(
                arguments.value(QStringLiteral("patch")).toObject()));
        });

    add(registry, QStringLiteral("resource.create"),
        QStringLiteral("Stage a new XHTML or CSS file. book_path is the EPUB-relative path (e.g. OEBPS/Text/Section0002.xhtml). Optional text is the full file contents; XHTML defaults to an empty HTML5 document. add_to_spine defaults true for XHTML. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("book_path"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("kind"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("text"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("add_to_spine"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } },
                { QStringLiteral("after_resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("book_path") } }
        },
        [workspace](const QJsonObject &arguments) {
            const bool add_to_spine = arguments.contains(QStringLiteral("add_to_spine"))
                ? arguments.value(QStringLiteral("add_to_spine")).toBool() : true;
            return fromBook(workspace->createResource(
                arguments.value(QStringLiteral("book_path")).toString(),
                arguments.value(QStringLiteral("kind")).toString(),
                arguments.value(QStringLiteral("text")).toString(),
                add_to_spine,
                arguments.value(QStringLiteral("after_resource_id")).toString()));
        });

    add(registry, QStringLiteral("resource.replace_text"),
        QStringLiteral("Stage a full-file replacement of an XHTML or CSS resource. For chapter-length bodies use content.typeset_from_manuscript instead; this tool rejects replacements larger than 64KiB so the model cannot dump a novel into the prompt. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("text"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("text"), QStringLiteral("expected_revision")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString text = arguments.value(QStringLiteral("text")).toString();
            if (text.size() > 65536) {
                return ToolResult::failure(
                    QStringLiteral("REPLACE_TOO_LARGE"),
                    QStringLiteral("resource.replace_text is capped at 65536 characters. For long text already in the book, use content.replace_body with source_resource_id, content.wrap_plain, or content.split."));
            }
            return fromBook(workspace->replaceText(
                arguments.value(QStringLiteral("resource_id")).toString(),
                text,
                static_cast<quint64>(arguments.value(QStringLiteral("expected_revision")).toInteger())));
        });

    add(registry, QStringLiteral("manuscript.parse"),
        QStringLiteral("Parse a text/HTML resource already in the book into a bounded shared-offset page of title/credits plus chapter, TOC, illustration, image, and template arrays. Never returns chapter bodies. Use next_offset while has_more is true. Optional heading_pattern and illustration_pattern are regexes (capture group 1 = name). Omit them to use built-in East-Asian volume heuristics. Omit manuscript_id to auto-detect the largest dropped text."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("illustration_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("offset"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 0 },
                    { QStringLiteral("default"), 0 }
                } },
                { QStringLiteral("limit"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 1 },
                    { QStringLiteral("maximum"),
                      MAX_MANUSCRIPT_SUMMARY_PAGE_SIZE },
                    { QStringLiteral("default"),
                      DEFAULT_MANUSCRIPT_SUMMARY_PAGE_SIZE }
                } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            ParseOptions options;
            options.headingRegex = arguments.value(QStringLiteral("heading_pattern")).toString();
            options.illustrationRegex = arguments.value(QStringLiteral("illustration_pattern")).toString();
            for (const auto &entry : {
                     qMakePair(QStringLiteral("heading_pattern"), options.headingRegex),
                     qMakePair(QStringLiteral("illustration_pattern"), options.illustrationRegex) }) {
                if (entry.second.isEmpty()) continue;
                const QRegularExpression expression(
                    entry.second, QRegularExpression::UseUnicodePropertiesOption);
                if (!expression.isValid()) {
                    return ToolResult::failure(QStringLiteral("REGEX_INVALID"),
                        QStringLiteral("%1: %2").arg(entry.first, expression.errorString()),
                        QJsonObject {
                            { QStringLiteral("field"), entry.first },
                            { QStringLiteral("error_offset"), expression.patternErrorOffset() }
                        });
                }
            }
            if (options.headingRegex.isEmpty() && options.illustrationRegex.isEmpty()) {
                const QJsonObject summary = parseManuscriptInBook(
                    workspace, arguments.value(QStringLiteral("manuscript_id")).toString());
                if (!summary.value(QStringLiteral("ok")).toBool()
                    && summary.contains(QStringLiteral("code"))) {
                    return ToolResult::failure(
                        summary.value(QStringLiteral("code")).toString(),
                        summary.value(QStringLiteral("message")).toString(),
                        summary);
                }
                return ToolResult::success(
                    paginatedManuscriptSummary(summary, arguments));
            }
            const QString id = findManuscriptResourceId(
                workspace, arguments.value(QStringLiteral("manuscript_id")).toString());
            if (id.isEmpty()) {
                return ToolResult::failure(QStringLiteral("MANUSCRIPT_NOT_FOUND"),
                                           QStringLiteral("No text resource found to parse"));
            }
            const ParsedManuscript parsed = parseManuscriptText(workspace->workingText(id), QString(), options);
            QJsonObject summary = manuscriptSummaryJson(parsed);
            summary.insert(QStringLiteral("ok"), true);
            summary.insert(QStringLiteral("resource_id"), id);
            return ToolResult::success(
                paginatedManuscriptSummary(summary, arguments));
        });

    add(registry, QStringLiteral("content.fill_section"),
        QStringLiteral("Fill one template page from the parsed manuscript (chapter, credits, synopsis, toc, title, illustration, cover, start). Pass chapter_index for Section pages or image_name for illus/cover/start. Pass the same heading_pattern and illustration_pattern used with manuscript.parse when its custom parsing is needed. Does not send chapter text through the model. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("role"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("chapter_index"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("image_name"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("illustration_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            ParseOptions parse_options;
            parse_options.headingRegex = arguments.value(QStringLiteral("heading_pattern")).toString();
            parse_options.illustrationRegex = arguments.value(QStringLiteral("illustration_pattern")).toString();
            return fromBook(fillTemplateSection(
                workspace,
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("role")).toString(),
                arguments.value(QStringLiteral("manuscript_id")).toString(),
                arguments.value(QStringLiteral("chapter_index")).toInt(-1),
                arguments.value(QStringLiteral("image_name")).toString(),
                parse_options));
        });

    add(registry, QStringLiteral("content.typeset_from_manuscript"),
        QStringLiteral("Fill the open 轻小说模板 from a dropped manuscript: parse TXT, copy extra Section pages, wrap every chapter, rewrite illus/cover/start image hrefs, and fill title/credits/synopsis/contents/metadata. Pass the same heading_pattern and illustration_pattern used with manuscript.parse when its custom parsing is needed. Never dumps chapter bodies into the model. Call transaction.begin first (or the tool will). Preview with transaction.preview, then transaction.commit. retire_source defaults true for imported HTML that is not a template page."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("illustration_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("retire_source"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } },
                { QStringLiteral("update_metadata"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            TypesetOptions options;
            options.manuscriptId = arguments.value(QStringLiteral("manuscript_id")).toString();
            options.parseOptions.headingRegex = arguments.value(QStringLiteral("heading_pattern")).toString();
            options.parseOptions.illustrationRegex = arguments.value(QStringLiteral("illustration_pattern")).toString();
            if (arguments.contains(QStringLiteral("retire_source"))) {
                options.retireSource = arguments.value(QStringLiteral("retire_source")).toBool();
            }
            if (arguments.contains(QStringLiteral("update_metadata"))) {
                options.updateMetadata = arguments.value(QStringLiteral("update_metadata")).toBool();
            }
            return fromBook(typesetFromManuscript(workspace, options));
        });

    add(registry, QStringLiteral("resource.copy"),
        QStringLiteral("Stage a copy of an existing XHTML or CSS resource (same as Book Browser Add Copy). If book_path is omitted, a unique sibling path is chosen (Section0001.xhtml → Section0002.xhtml). The copy is inserted in the spine after the source when add_to_spine is true. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("book_path"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("add_to_spine"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            const bool add_to_spine = arguments.contains(QStringLiteral("add_to_spine"))
                ? arguments.value(QStringLiteral("add_to_spine")).toBool() : true;
            return fromBook(workspace->copyResource(
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("book_path")).toString(),
                add_to_spine));
        });

    add(registry, QStringLiteral("checkpoint.create"),
        QStringLiteral("Create a restore point of the live book. Label is capped at 256 characters."),
        ToolRisk::ReversibleEdit, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("label"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("maxLength"), MAX_CHECKPOINT_LABEL_LENGTH }
                } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString label = arguments.value(QStringLiteral("label")).toString();
            if (label.size() > MAX_CHECKPOINT_LABEL_LENGTH) {
                return ToolResult::failure(
                    QStringLiteral("CHECKPOINT_LABEL_TOO_LONG"),
                    QStringLiteral("Checkpoint label is capped at 256 characters."),
                    QJsonObject {
                        { QStringLiteral("label_length"), label.size() },
                        { QStringLiteral("max_label_length"),
                          MAX_CHECKPOINT_LABEL_LENGTH }
                    });
            }
            return fromBook(workspace->createCheckpoint(label));
        });

    add(registry, QStringLiteral("checkpoint.list"),
        QStringLiteral("List a bounded page of Agent checkpoints. Guarded task restore points include at most 32 bounded affected-resource ID previews. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_CHECKPOINT_PAGE_SIZE, MAX_CHECKPOINT_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedCheckpointList(
                workspace->listCheckpoints(), arguments));
        });

    add(registry, QStringLiteral("checkpoint.restore"),
        QStringLiteral("Restore a previously created checkpoint."),
        ToolRisk::Bulk, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("checkpoint_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("checkpoint_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->restoreCheckpoint(
                arguments.value(QStringLiteral("checkpoint_id")).toString()));
        });

    add(registry, QStringLiteral("book.search_regex"),
        QStringLiteral("Regex search over text resources. Follow next_cursor while has_more to inspect all matches; count_only returns an exact total. Each page returns at most 50 bounded matches with UTF-16 source offsets. Invalid or zero-length patterns fail explicitly. Use resource.read_fragment for exact source."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("max_matches"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 1 },
                    { QStringLiteral("maximum"), MAX_REGEX_SEARCH_MATCHES },
                    { QStringLiteral("default"), DEFAULT_REGEX_SEARCH_MATCHES }
                } },
                { QStringLiteral("limit"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("integer") },
                    { QStringLiteral("minimum"), 1 },
                    { QStringLiteral("maximum"), MAX_REGEX_SEARCH_MATCHES }
                } },
                { QStringLiteral("cursor"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                { QStringLiteral("count_only"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("boolean") }} },
                { QStringLiteral("resource_ids"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("items"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} }
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("pattern") } }
        },
        [workspace](const QJsonObject &arguments) {
            return paginatedSearch(workspace, arguments, true);
        });

    add(registry, QStringLiteral("book.check"),
        QStringLiteral("Structural QA with bounded shared-offset pages of validation issues, unused images, and XHTML wellformedness. Prefer this over claiming the book is fine from memory. Use next_offset while has_more is true."),
        ToolRisk::Read, false, false,
        paginationSchema(DEFAULT_DIAGNOSTIC_PAGE_SIZE, MAX_DIAGNOSTIC_PAGE_SIZE),
        [workspace](const QJsonObject &arguments) {
            return ToolResult::success(paginatedArrays(
                inspectBook(workspace),
                { QStringLiteral("issues"), QStringLiteral("unused_images"),
                  QStringLiteral("wellformed") },
                arguments, DEFAULT_DIAGNOSTIC_PAGE_SIZE,
                MAX_DIAGNOSTIC_PAGE_SIZE));
        });

    add(registry, QStringLiteral("content.replace_body"),
        QStringLiteral("Replace the <body> inner HTML of an XHTML resource. Prefer source_resource_id pointing at text already in the book (uncapped). Direct `inner` is capped at 16KiB so novels are not pasted through the model."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("inner"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("source_resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString inner = arguments.value(QStringLiteral("inner")).toString();
            const QString source = arguments.value(QStringLiteral("source_resource_id")).toString();
            if (source.isEmpty() && inner.size() > 16384) {
                return ToolResult::failure(QStringLiteral("REPLACE_TOO_LARGE"),
                                           QStringLiteral("inner is capped at 16384. Put the text in a book resource and pass source_resource_id."));
            }
            return fromBook(replaceBody(workspace,
                                        arguments.value(QStringLiteral("resource_id")).toString(),
                                        inner, source));
        });

    add(registry, QStringLiteral("content.insert"),
        QStringLiteral("Insert HTML before or after a unique anchor substring copied from the current file. Use for img tags, wrappers, or short markup. html is capped at 8KiB."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("anchor"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("html"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("before"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("anchor"), QStringLiteral("html")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString html = arguments.value(QStringLiteral("html")).toString();
            if (html.size() > 8192) {
                return ToolResult::failure(QStringLiteral("INSERT_TOO_LARGE"),
                                           QStringLiteral("html is capped at 8192. Use content.replace_body with source_resource_id for large inserts."));
            }
            return fromBook(insertHtml(workspace,
                                       arguments.value(QStringLiteral("resource_id")).toString(),
                                       arguments.value(QStringLiteral("anchor")).toString(),
                                       html,
                                       arguments.value(QStringLiteral("before")).toBool(false)));
        });

    add(registry, QStringLiteral("content.wrap"),
        QStringLiteral("Wrap every regex match with open/close tags (batch class/tag application). Omit resource_id to apply to all text resources. Pattern is regex. Example: wrap ^\\s*<p> lines or a phrase with <span class=\"em\">."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("open"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("close"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("max_matches"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("pattern"), QStringLiteral("open"), QStringLiteral("close")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(wrapInResource(
                workspace,
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("pattern")).toString(),
                arguments.value(QStringLiteral("open")).toString(),
                arguments.value(QStringLiteral("close")).toString(),
                arguments.value(QStringLiteral("max_matches")).toInt(0)));
        });

    add(registry, QStringLiteral("content.replace_regex"),
        QStringLiteral("Regex replace in one resource or every text resource. Replacement may use $1 capture refs. Long-form rewrite of text already in the book; do not put novel bodies in the replacement string."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("replacement"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("max_matches"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("pattern"), QStringLiteral("replacement")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            const QString replacement = arguments.value(QStringLiteral("replacement")).toString();
            if (replacement.size() > 8192) {
                return ToolResult::failure(QStringLiteral("REPLACE_TOO_LARGE"),
                                           QStringLiteral("replacement is capped at 8192 characters"));
            }
            return fromBook(regexReplaceInBook(
                workspace,
                arguments.value(QStringLiteral("pattern")).toString(),
                replacement,
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("max_matches")).toInt(0)));
        });

    add(registry, QStringLiteral("content.wrap_plain"),
        QStringLiteral("Turn a plain-text (or ImportTXT) resource already in the book into tagged XHTML using caller-supplied regexes. rules: heading_pattern, heading_open/close, paragraph_open/close, illustration_pattern, illustration_html ($1 = capture), keep_blank, title. Writes into target_id (defaults to source). Never send the novel through the model."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("source_resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("target_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("rules"), QJsonObject { { QStringLiteral("type"), QStringLiteral("object") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("source_resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(wrapPlainResource(
                workspace,
                arguments.value(QStringLiteral("source_resource_id")).toString(),
                arguments.value(QStringLiteral("target_id")).toString(),
                arguments.value(QStringLiteral("rules")).toObject()));
        });

    add(registry, QStringLiteral("content.split"),
        QStringLiteral("Split one XHTML file into multiple spine items on a heading regex (default: h1–h6 tags). Copies follow the source. Heading text becomes each file's title. Use after wrap_plain when one imported file holds every chapter."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(splitResourceByHeading(
                workspace,
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("heading_pattern")).toString()));
        });

    add(registry, QStringLiteral("content.merge"),
        QStringLiteral("Merge two or more XHTML resources in listed order into the first. delete_sources defaults true (staged delete of the rest)."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_ids"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } },
                { QStringLiteral("delete_sources"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_ids") } }
        },
        [workspace](const QJsonObject &arguments) {
            QStringList ids;
            for (const QJsonValue &value : arguments.value(QStringLiteral("resource_ids")).toArray()) {
                ids.append(value.toString());
            }
            const bool del = arguments.contains(QStringLiteral("delete_sources"))
                ? arguments.value(QStringLiteral("delete_sources")).toBool() : true;
            return fromBook(mergeResources(workspace, ids, del));
        });

    add(registry, QStringLiteral("image.insert"),
        QStringLiteral("Insert an <img> for an image already in the book (dropped into Images). page_id is the XHTML file; image_id is the image resource. Unique `anchor` text locates the insert point. Optional class and alt."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("page_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("image_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("anchor"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("before"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } },
                { QStringLiteral("class"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("alt"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("page_id"), QStringLiteral("image_id"), QStringLiteral("anchor")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(insertImageTag(
                workspace,
                arguments.value(QStringLiteral("page_id")).toString(),
                arguments.value(QStringLiteral("image_id")).toString(),
                arguments.value(QStringLiteral("anchor")).toString(),
                arguments.value(QStringLiteral("before")).toBool(false),
                arguments.value(QStringLiteral("class")).toString(),
                arguments.value(QStringLiteral("alt")).toString()));
        });

    add(registry, QStringLiteral("resource.delete"),
        QStringLiteral("Stage deletion of a resource (not OPF/NCX/Nav, not the last XHTML). Live book unchanged until commit. Undo after apply via Sigil."),
        ToolRisk::Destructive, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->deleteResource(
                arguments.value(QStringLiteral("resource_id")).toString()));
        });

    add(registry, QStringLiteral("resource.rename"),
        QStringLiteral("Stage a rename or move. book_path may be a new filename in the same folder (Heat.xhtml) or a full EPUB-relative path (OEBPS/Text/Heat.xhtml, OEBPS/Misc/ch1.xhtml). OPF/NCX/Nav cannot be renamed. Href updates are applied on commit. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("book_path"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("book_path")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->renameResource(
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("book_path")).toString()));
        });

    add(registry, QStringLiteral("spine.set"),
        QStringLiteral("Stage a new spine order. resource_ids is the full XHTML reading order. Files omitted are removed from the spine (not deleted)."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_ids"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_ids") } }
        },
        [workspace](const QJsonObject &arguments) {
            QStringList ids;
            for (const QJsonValue &value : arguments.value(QStringLiteral("resource_ids")).toArray()) {
                ids.append(value.toString());
            }
            return fromBook(workspace->updateSpine(ids));
        });

    add(registry, QStringLiteral("spine.sort"),
        QStringLiteral("Stage an alphanumeric spine order by book_path (numeric-aware, same as Book Browser Sort). Live book unchanged until transaction.commit."),
        ToolRisk::Bulk, true, true, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return fromBook(sortSpine(workspace));
        });

    add(registry, QStringLiteral("style.link"),
        QStringLiteral("Replace stylesheet <link> tags in XHTML files with the given CSS resources (same as Link Stylesheets). Omit html_ids to apply to every XHTML file. css_ids is the new link order. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("html_ids"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } },
                { QStringLiteral("css_ids"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("array") },
                    { QStringLiteral("items"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("css_ids") } }
        },
        [workspace](const QJsonObject &arguments) {
            QStringList html_ids;
            QStringList css_ids;
            for (const QJsonValue &value : arguments.value(QStringLiteral("html_ids")).toArray()) {
                html_ids.append(value.toString());
            }
            for (const QJsonValue &value : arguments.value(QStringLiteral("css_ids")).toArray()) {
                css_ids.append(value.toString());
            }
            if (css_ids.isEmpty()) {
                return ToolResult::failure(QStringLiteral("CSS_REQUIRED"),
                                           QStringLiteral("css_ids must list at least one stylesheet"));
            }
            return fromBook(linkStylesheets(workspace, html_ids, css_ids));
        });

    add(registry, QStringLiteral("python.inspect"),
        QStringLiteral("Run a read-only Live Python v2 snippet for custom Book statistics. Book and editor writes are rejected; the Agent revision stays unchanged. Use plugin.book.text_resources() and plugin.book.read_many([Resource]) to inspect XHTML. Print a compact report; stdout is returned on success. `result` and def run(plugin) return EXIT STATUS 0/None, not report data. Available in the Sigil GUI only."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("script"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("string") }} },
                { QStringLiteral("timeout_ms"), QJsonObject {{ QStringLiteral("type"), QStringLiteral("integer") }} }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("script") } }
        },
        [workspace](const QJsonObject &arguments) {
            const int timeout_ms = qBound(1000,
                arguments.value(QStringLiteral("timeout_ms")).toInt(30000), 300000);
            return fromBook(workspace->runLivePython(
                arguments.value(QStringLiteral("script")).toString(), timeout_ms,
                QStringLiteral("read")));
        });

    add(registry, QStringLiteral("python.run"),
        QStringLiteral("Run a Live Python v2 snippet against the in-memory Book. Use mode=read for statistics; it rejects Book writes and preserves revision. mode=edit is the compatibility default. `plugin` is bound; plugin.book.text_resources() and plugin.book.read_many([Resource]) read XHTML. Print reports to stdout. Optional def run(plugin) or `result` is an EXIT STATUS (0/None means success), not report data. Commit or rollback any Agent transaction first. Output is bounded. Unavailable outside Sigil GUI; script capped at 64KiB."),
        ToolRisk::Bulk, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("script"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("timeout_ms"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("mode"), QJsonObject {
                    { QStringLiteral("type"), QStringLiteral("string") },
                    { QStringLiteral("enum"), QJsonArray { QStringLiteral("read"), QStringLiteral("edit") } },
                    { QStringLiteral("default"), QStringLiteral("edit") }
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("script") } }
        },
        [workspace](const QJsonObject &arguments) {
            int timeout_ms = arguments.value(QStringLiteral("timeout_ms")).toInt(30000);
            if (timeout_ms < 1000) timeout_ms = 1000;
            if (timeout_ms > 300000) timeout_ms = 300000;
            return fromBook(workspace->runLivePython(
                arguments.value(QStringLiteral("script")).toString(), timeout_ms,
                arguments.value(QStringLiteral("mode")).toString(QStringLiteral("edit"))));
        });

    add(registry, QStringLiteral("toc.generate"),
        QStringLiteral("Build TOC entries from headings in spine order. heading_pattern is regex (default h1–h6). Stages NCX/toc; commit to apply."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(generateTocFromHeadings(
                workspace, arguments.value(QStringLiteral("heading_pattern")).toString()));
        });

    if (session) {
        add(registry, QStringLiteral("session.remember"),
            QStringLiteral("Store one bounded note for this Agent session (user preferences, chosen heading regex, unfinished mapping). At most 64 notes; key at most 64 characters and value at most 2048. Returns only the changed note. Cleared on New Session."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("key"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("minLength"), 1 },
                        { QStringLiteral("maxLength"), MAX_SESSION_MEMORY_KEY_LENGTH }
                    } },
                    { QStringLiteral("value"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("maxLength"), MAX_SESSION_MEMORY_VALUE_LENGTH }
                    } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("key"), QStringLiteral("value") } }
            },
            [session](const QJsonObject &arguments) {
                const QString key = arguments.value(QStringLiteral("key")).toString().trimmed();
                const QString value = arguments.value(QStringLiteral("value")).toString();
                if (key.isEmpty() || key.size() > MAX_SESSION_MEMORY_KEY_LENGTH
                    || value.size() > MAX_SESSION_MEMORY_VALUE_LENGTH) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_MEMORY_ARGUMENT_INVALID"),
                        QStringLiteral("Memory key must be 1-64 characters and value at most 2048 characters."),
                        QJsonObject {
                            { QStringLiteral("key_length"), key.size() },
                            { QStringLiteral("value_length"), value.size() },
                            { QStringLiteral("max_key_length"),
                              MAX_SESSION_MEMORY_KEY_LENGTH },
                            { QStringLiteral("max_value_length"),
                              MAX_SESSION_MEMORY_VALUE_LENGTH }
                        });
                }
                const bool existing = !session->recall(key).isUndefined();
                if (!existing
                    && session->memory().size() >= MAX_SESSION_MEMORY_ENTRIES) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_MEMORY_LIMIT_REACHED"),
                        QStringLiteral("This session already contains 64 memory notes. Update an existing key or start a new session."),
                        QJsonObject {
                            { QStringLiteral("memory_count"), session->memory().size() },
                            { QStringLiteral("max_memory_count"),
                              MAX_SESSION_MEMORY_ENTRIES }
                        });
                }
                if (!session->remember(key, value)) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_MEMORY_ARGUMENT_INVALID"),
                        QStringLiteral("The session memory note was rejected by its storage bounds."));
                }
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("key"), key },
                    { QStringLiteral("value"), session->recall(key) },
                    { QStringLiteral("memory_count"), session->memory().size() }
                });
            });

        add(registry, QStringLiteral("session.recall"),
            QStringLiteral("Read one session memory key, or omit key to list a bounded insertion-order page. Use next_offset while has_more is true."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("key"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("maxLength"), MAX_SESSION_MEMORY_KEY_LENGTH }
                    } },
                    { QStringLiteral("offset"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("integer") },
                        { QStringLiteral("minimum"), 0 },
                        { QStringLiteral("default"), 0 }
                    } },
                    { QStringLiteral("limit"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("integer") },
                        { QStringLiteral("minimum"), 1 },
                        { QStringLiteral("maximum"), MAX_SESSION_MEMORY_PAGE_SIZE },
                        { QStringLiteral("default"), DEFAULT_SESSION_MEMORY_PAGE_SIZE }
                    } }
                } }
            },
            [session](const QJsonObject &arguments) {
                const QString key = arguments.value(QStringLiteral("key")).toString().trimmed();
                if (key.isEmpty()) {
                    return ToolResult::success(paginatedSessionMemory(session, arguments));
                }
                if (key.size() > MAX_SESSION_MEMORY_KEY_LENGTH) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_MEMORY_ARGUMENT_INVALID"),
                        QStringLiteral("Memory key must be at most 64 characters."),
                        QJsonObject {
                            { QStringLiteral("key_length"), key.size() },
                            { QStringLiteral("max_key_length"),
                              MAX_SESSION_MEMORY_KEY_LENGTH }
                        });
                }
                const QJsonValue value = session->recall(key);
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("key"), key },
                    { QStringLiteral("found"), !value.isUndefined() },
                    { QStringLiteral("value"), value }
                });
            });

        add(registry, QStringLiteral("session.task_add"),
            QStringLiteral("Add a bounded item to this session's task list (plan/progress). At most 128 tasks; title at most 256 characters and note at most 2048. Returns only the added task."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("title"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("minLength"), 1 },
                        { QStringLiteral("maxLength"), MAX_SESSION_TASK_TITLE_LENGTH }
                    } },
                    { QStringLiteral("note"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("maxLength"), MAX_SESSION_TASK_NOTE_LENGTH }
                    } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("title") } }
            },
            [session](const QJsonObject &arguments) {
                const QString title = arguments.value(QStringLiteral("title")).toString();
                const QString note = arguments.value(QStringLiteral("note")).toString();
                if (title.trimmed().isEmpty()
                    || title.size() > MAX_SESSION_TASK_TITLE_LENGTH
                    || note.size() > MAX_SESSION_TASK_NOTE_LENGTH) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_TASK_ARGUMENT_INVALID"),
                        QStringLiteral("Task title must be 1-256 characters and note at most 2048 characters."),
                        QJsonObject {
                            { QStringLiteral("title_length"), title.size() },
                            { QStringLiteral("note_length"), note.size() },
                            { QStringLiteral("max_title_length"),
                              MAX_SESSION_TASK_TITLE_LENGTH },
                            { QStringLiteral("max_note_length"),
                              MAX_SESSION_TASK_NOTE_LENGTH }
                        });
                }
                const QJsonArray before = session->tasks();
                if (before.size() >= MAX_SESSION_TASKS) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_TASK_LIMIT_REACHED"),
                        QStringLiteral("This session already contains 128 tasks. Start a new session to create more."),
                        QJsonObject {
                            { QStringLiteral("task_count"), before.size() },
                            { QStringLiteral("max_task_count"), MAX_SESSION_TASKS }
                        });
                }
                const QString id = session->addTask(title, note);
                if (id.isEmpty()) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_TASK_LIMIT_REACHED"),
                        QStringLiteral("The session task was rejected by its storage bounds."));
                }
                const QJsonArray tasks = session->tasks();
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("id"), id },
                    { QStringLiteral("task"), taskById(tasks, id) },
                    { QStringLiteral("task_count"), tasks.size() }
                });
            });

        add(registry, QStringLiteral("session.task_update"),
            QStringLiteral("Update a session task. status: pending | in_progress | done | cancelled."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("id"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("maxLength"), 64 }
                    } },
                    { QStringLiteral("status"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("enum"), QJsonArray {
                            QStringLiteral("pending"), QStringLiteral("in_progress"),
                            QStringLiteral("done"), QStringLiteral("cancelled")
                        } }
                    } },
                    { QStringLiteral("note"), QJsonObject {
                        { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("maxLength"), MAX_SESSION_TASK_NOTE_LENGTH }
                    } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("id") } }
            },
            [session](const QJsonObject &arguments) {
                const QString id = arguments.value(QStringLiteral("id")).toString();
                const QString status = arguments.value(QStringLiteral("status")).toString();
                const QString note = arguments.value(QStringLiteral("note")).toString();
                const QStringList statuses {
                    QStringLiteral("pending"), QStringLiteral("in_progress"),
                    QStringLiteral("done"), QStringLiteral("cancelled")
                };
                if ((!status.isEmpty() && !statuses.contains(status))
                    || note.size() > MAX_SESSION_TASK_NOTE_LENGTH) {
                    return ToolResult::failure(
                        QStringLiteral("SESSION_TASK_ARGUMENT_INVALID"),
                        QStringLiteral("Task status must be pending, in_progress, done, or cancelled; note is capped at 2048 characters."),
                        QJsonObject {
                            { QStringLiteral("status"), status },
                            { QStringLiteral("note_length"), note.size() },
                            { QStringLiteral("max_note_length"),
                              MAX_SESSION_TASK_NOTE_LENGTH }
                        });
                }
                if (!session->updateTask(id, status, note)) {
                    return ToolResult::failure(QStringLiteral("TASK_NOT_FOUND"),
                                               QStringLiteral("Unknown task id"));
                }
                const QJsonArray tasks = session->tasks();
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("task"), taskById(tasks, id) },
                    { QStringLiteral("task_count"), tasks.size() }
                });
            });

        add(registry, QStringLiteral("session.tasks"),
            QStringLiteral("List a bounded insertion-order page of this session's task checklist. Use next_offset while has_more is true."),
            ToolRisk::Read, false, false,
            paginationSchema(DEFAULT_SESSION_TASK_PAGE_SIZE,
                             MAX_SESSION_TASK_PAGE_SIZE),
            [session](const QJsonObject &arguments) {
                return ToolResult::success(paginatedArray(
                    QStringLiteral("tasks"), session->tasks(), arguments,
                    DEFAULT_SESSION_TASK_PAGE_SIZE,
                    MAX_SESSION_TASK_PAGE_SIZE));
            });
    }
}

} // namespace SigilAgent
