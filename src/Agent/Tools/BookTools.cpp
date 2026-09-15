/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/BookTools.h"

#include <QJsonArray>
#include <functional>

#include "Agent/Execution/BookEdits.h"
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

QJsonObject boundedLiteralSearchResult(const QJsonArray &source,
                                       const QString &query,
    int max_matches)
{
    QJsonArray matches;
    for (const QJsonValue &value : source) {
        if (matches.size() >= max_matches) break;
        QJsonObject match = value.toObject();
        const QString snippet = match.value(QStringLiteral("snippet")).toString();
        const bool truncated = snippet.size() > MAX_LITERAL_SEARCH_SNIPPET_LENGTH;
        match.insert(QStringLiteral("snippet"),
                     snippet.left(MAX_LITERAL_SEARCH_SNIPPET_LENGTH));
        match.insert(QStringLiteral("snippet_offset"), qMax(
            0, match.value(QStringLiteral("offset")).toInt() - 24));
        match.insert(QStringLiteral("snippet_length"), snippet.size());
        match.insert(QStringLiteral("match_length"), query.size());
        match.insert(QStringLiteral("snippet_truncated"), truncated);
        matches.append(match);
    }
    return QJsonObject {
        { QStringLiteral("matches"), matches },
        { QStringLiteral("match_count"), matches.size() },
        { QStringLiteral("max_matches"), max_matches },
        { QStringLiteral("match_limit_reached"), matches.size() >= max_matches },
        { QStringLiteral("query_length"), query.size() }
    };
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
            .arg(arguments.value(QStringLiteral("expected_revision")).toInteger());
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

    add(registry, QStringLiteral("book.summary"),
        QStringLiteral("Summarize the open EPUB: revision, version, title, spine/TOC counts, resource totals. Never returns file binaries."),
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
        QStringLiteral("Read package metadata such as title, language and creator."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(workspace->metadata());
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
            } }
        } },
        { QStringLiteral("required"), QJsonArray { QStringLiteral("query") } }
    };
    add(registry, QStringLiteral("book.search"),
        QStringLiteral("Case-insensitive literal search over text resources. Query length is capped at 512 characters. Returns at most 50 matches with bounded snippets, full offsets and match lengths, and explicit truncation flags. Use resource.read_fragment for truncated source."),
        ToolRisk::Read, false, false, search_schema,
        [workspace](const QJsonObject &arguments) {
            const QString query = arguments.value(QStringLiteral("query")).toString();
            if (query.size() > MAX_LITERAL_SEARCH_QUERY_LENGTH) {
                return ToolResult::failure(
                    QStringLiteral("SEARCH_QUERY_TOO_LONG"),
                    QStringLiteral("book.search query is capped at 512 characters. Search for a shorter distinctive literal."),
                    QJsonObject {
                        { QStringLiteral("query_length"), query.size() },
                        { QStringLiteral("max_query_length"),
                          MAX_LITERAL_SEARCH_QUERY_LENGTH }
                    });
            }
            const int requested_matches = arguments.contains(QStringLiteral("max_matches"))
                ? arguments.value(QStringLiteral("max_matches")).toInt(
                    DEFAULT_LITERAL_SEARCH_MATCHES)
                : DEFAULT_LITERAL_SEARCH_MATCHES;
            const int max_matches = qBound(
                1, requested_matches, MAX_LITERAL_SEARCH_MATCHES);
            return ToolResult::success(boundedLiteralSearchResult(
                workspace->search(query, max_matches), query, max_matches));
        });

    QJsonObject fragment_schema {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject {
            { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
            { QStringLiteral("offset"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
            { QStringLiteral("limit"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
        } },
        { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
    };
    add(registry, QStringLiteral("resource.read_fragment"),
        QStringLiteral("Read a bounded text fragment of an XHTML or CSS resource. Copy the `text` field into patch_fragment.expected_text (no line-number prefixes). `lines` gives 1-based line identity for start_line when the substring is not unique. Fonts and images are refused."),
        ToolRisk::Read, false, false, fragment_schema,
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->readFragment(
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("offset")).toInt(0),
                arguments.value(QStringLiteral("limit")).toInt(2048)));
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
        QStringLiteral("Preview staged changes without mutating the live book."),
        ToolRisk::Read, false, true, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return fromBook(workspace->previewTransaction());
        });

    add(registry, QStringLiteral("transaction.commit"),
        QStringLiteral("Commit staged changes. Fails with BOOK_REVISION_CONFLICT if the book changed since the transaction began."),
        ToolRisk::Bulk, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("expected_revision") } }
        },
        [workspace](const QJsonObject &arguments) {
            const quint64 expected = static_cast<quint64>(
                arguments.value(QStringLiteral("expected_revision")).toInteger());
            return fromBook(workspace->commitTransaction(expected));
        });

    add(registry, QStringLiteral("transaction.rollback"),
        QStringLiteral("Discard the open staged transaction. Live book is unchanged."),
        ToolRisk::ReversibleEdit, true, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return fromBook(workspace->rollbackTransaction());
        });

    add(registry, QStringLiteral("resource.patch_fragment"),
        QStringLiteral("Stage a replacement of an exact current substring. expected_text is required and must be copied from read_fragment.text (never invent UTF-16 offsets). If that substring appears more than once, pass start_line from read_fragment.lines. Optional start/end are ignored unless they exactly equal expected_text. Must not cut through a markup tag. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_text"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("text"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("expected_revision"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("start_line"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("start"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("end"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray {
                QStringLiteral("resource_id"), QStringLiteral("expected_text"),
                QStringLiteral("text"), QStringLiteral("expected_revision")
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->patchFragment(
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("start")).toInt(-1),
                arguments.value(QStringLiteral("end")).toInt(-1),
                arguments.value(QStringLiteral("text")).toString(),
                static_cast<quint64>(arguments.value(QStringLiteral("expected_revision")).toInteger()),
                arguments.value(QStringLiteral("expected_text")).toString(),
                arguments.value(QStringLiteral("start_line")).toInt(-1)));
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
        QStringLiteral("Parse a text/HTML resource already in the book into a compact structure (title, credits, chapter list, illustration names). Never returns chapter bodies. Optional heading_pattern and illustration_pattern are regexes (capture group 1 = name). Omit them to use built-in East-Asian volume heuristics. Omit manuscript_id to auto-detect the largest dropped text."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("heading_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("illustration_pattern"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            ParseOptions options;
            options.headingRegex = arguments.value(QStringLiteral("heading_pattern")).toString();
            options.illustrationRegex = arguments.value(QStringLiteral("illustration_pattern")).toString();
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
                return ToolResult::success(summary);
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
            return ToolResult::success(summary);
        });

    add(registry, QStringLiteral("content.fill_section"),
        QStringLiteral("Fill one template page from the parsed manuscript (chapter, credits, synopsis, toc, title, illustration, cover, start). Pass chapter_index for Section pages or image_name for illus/cover/start. Does not send chapter text through the model. Live book unchanged until transaction.commit."),
        ToolRisk::ReversibleEdit, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("resource_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("role"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("chapter_index"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } },
                { QStringLiteral("image_name"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("resource_id") } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(fillTemplateSection(
                workspace,
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("role")).toString(),
                arguments.value(QStringLiteral("manuscript_id")).toString(),
                arguments.value(QStringLiteral("chapter_index")).toInt(-1),
                arguments.value(QStringLiteral("image_name")).toString()));
        });

    add(registry, QStringLiteral("content.typeset_from_manuscript"),
        QStringLiteral("Fill the open 轻小说模板 from a dropped manuscript: parse TXT, copy extra Section pages, wrap every chapter, rewrite illus/cover/start image hrefs, and fill title/credits/synopsis/contents/metadata. Never dumps chapter bodies into the model. Call transaction.begin first (or the tool will). Preview with transaction.preview, then transaction.commit. retire_source defaults true for imported HTML that is not a template page."),
        ToolRisk::Bulk, true, true,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("retire_source"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } },
                { QStringLiteral("update_metadata"), QJsonObject { { QStringLiteral("type"), QStringLiteral("boolean") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            TypesetOptions options;
            options.manuscriptId = arguments.value(QStringLiteral("manuscript_id")).toString();
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
        QStringLiteral("Create a restore point of the live book."),
        ToolRisk::ReversibleEdit, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("label"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
            return fromBook(workspace->createCheckpoint(
                arguments.value(QStringLiteral("label")).toString()));
        });

    add(registry, QStringLiteral("checkpoint.list"),
        QStringLiteral("List Agent checkpoints for the open book."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(QJsonObject {
                { QStringLiteral("checkpoints"), workspace->listCheckpoints() }
            });
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
        QStringLiteral("Regex search over text resources. Returns at most 50 matches with bounded match/capture previews, full offsets and lengths, and explicit truncation flags. Use resource.read_fragment for truncated source. Optional resource_id limits the search."),
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
                } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("pattern") } }
        },
        [workspace](const QJsonObject &arguments) {
            const QJsonObject data = regexSearchInBook(
                workspace,
                arguments.value(QStringLiteral("pattern")).toString(),
                arguments.value(QStringLiteral("resource_id")).toString(),
                arguments.value(QStringLiteral("max_matches")).toInt(
                    DEFAULT_REGEX_SEARCH_MATCHES));
            if (data.value(QStringLiteral("ok")).toBool() == false
                && data.contains(QStringLiteral("code"))) {
                return ToolResult::failure(data.value(QStringLiteral("code")).toString(),
                                           data.value(QStringLiteral("message")).toString(), data);
            }
            return ToolResult::success(data);
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

    add(registry, QStringLiteral("python.run"),
        QStringLiteral("Run a Live Python v2 snippet against the in-memory Book. `plugin` is bound (plugin.book / plugin.editor); optional def run(plugin) or a `result` value. This is a code snippet, not a plugin package and not a ZIP snapshot. Applies immediately; commit or rollback any Agent transaction first. stdout/stderr are returned (truncated). Unavailable outside the Sigil GUI (LIVE_PYTHON_UNAVAILABLE). Capped at 64KiB."),
        ToolRisk::Bulk, true, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("script"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                { QStringLiteral("timeout_ms"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
            } },
            { QStringLiteral("required"), QJsonArray { QStringLiteral("script") } }
        },
        [workspace](const QJsonObject &arguments) {
            int timeout_ms = arguments.value(QStringLiteral("timeout_ms")).toInt(30000);
            if (timeout_ms < 1000) timeout_ms = 1000;
            if (timeout_ms > 300000) timeout_ms = 300000;
            return fromBook(workspace->runLivePython(
                arguments.value(QStringLiteral("script")).toString(), timeout_ms));
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
            QStringLiteral("Store a small note for this Agent session (user preferences, chosen heading regex, unfinished mapping). Value should be short JSON or a string. Cleared on New Session."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("key"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                    { QStringLiteral("value"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("key"), QStringLiteral("value") } }
            },
            [session](const QJsonObject &arguments) {
                const QString key = arguments.value(QStringLiteral("key")).toString();
                session->remember(key, arguments.value(QStringLiteral("value")));
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("key"), key },
                    { QStringLiteral("memory"), session->memory() }
                });
            });

        add(registry, QStringLiteral("session.recall"),
            QStringLiteral("Read session memory. Omit key to return all notes."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("key"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } }
            },
            [session](const QJsonObject &arguments) {
                const QString key = arguments.value(QStringLiteral("key")).toString();
                if (key.isEmpty()) {
                    return ToolResult::success(QJsonObject { { QStringLiteral("memory"), session->memory() } });
                }
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("key"), key },
                    { QStringLiteral("value"), session->recall(key) }
                });
            });

        add(registry, QStringLiteral("session.task_add"),
            QStringLiteral("Add an item to this session's task list (plan/progress). Returns id."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("title"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                    { QStringLiteral("note"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("title") } }
            },
            [session](const QJsonObject &arguments) {
                const QString id = session->addTask(arguments.value(QStringLiteral("title")).toString(),
                                                    arguments.value(QStringLiteral("note")).toString());
                return ToolResult::success(QJsonObject {
                    { QStringLiteral("id"), id },
                    { QStringLiteral("tasks"), session->tasks() }
                });
            });

        add(registry, QStringLiteral("session.task_update"),
            QStringLiteral("Update a session task. status: pending | in_progress | done | cancelled."),
            ToolRisk::Read, false, false,
            QJsonObject {
                { QStringLiteral("type"), QStringLiteral("object") },
                { QStringLiteral("properties"), QJsonObject {
                    { QStringLiteral("id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                    { QStringLiteral("status"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
                    { QStringLiteral("note"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
                } },
                { QStringLiteral("required"), QJsonArray { QStringLiteral("id") } }
            },
            [session](const QJsonObject &arguments) {
                if (!session->updateTask(arguments.value(QStringLiteral("id")).toString(),
                                         arguments.value(QStringLiteral("status")).toString(),
                                         arguments.value(QStringLiteral("note")).toString())) {
                    return ToolResult::failure(QStringLiteral("TASK_NOT_FOUND"),
                                               QStringLiteral("Unknown task id"));
                }
                return ToolResult::success(QJsonObject { { QStringLiteral("tasks"), session->tasks() } });
            });

        add(registry, QStringLiteral("session.tasks"),
            QStringLiteral("List this session's task checklist."),
            ToolRisk::Read, false, false, emptyObjectSchema(),
            [session](const QJsonObject &) {
                return ToolResult::success(QJsonObject { { QStringLiteral("tasks"), session->tasks() } });
            });
    }
}

} // namespace SigilAgent
