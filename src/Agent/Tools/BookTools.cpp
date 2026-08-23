/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Tools/BookTools.h"

#include <QJsonArray>
#include <functional>

#include "Agent/Typeset/TypesetEngine.h"

namespace SigilAgent
{

namespace
{

QJsonObject emptyObjectSchema()
{
    return QJsonObject {
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject() }
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
    return QStringLiteral("Run %1 on the current book.").arg(name);
}

void registerBookTools(ToolRegistry *registry, IBookWorkspace *workspace)
{
    if (!registry || !workspace) return;

    add(registry, QStringLiteral("book.summary"),
        QStringLiteral("Summarize the open EPUB: revision, version, title, spine/TOC counts, resource totals. Never returns file binaries."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(workspace->summary());
        });

    add(registry, QStringLiteral("book.resources"),
        QStringLiteral("List manifest resources with id, path, media type and kind. Font/image bytes are omitted."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(QJsonObject { { QStringLiteral("resources"), workspace->resources() } });
        });

    add(registry, QStringLiteral("book.spine"),
        QStringLiteral("List spine reading order."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(QJsonObject { { QStringLiteral("spine"), workspace->spine() } });
        });

    add(registry, QStringLiteral("book.toc"),
        QStringLiteral("List table of contents entries."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(QJsonObject { { QStringLiteral("toc"), workspace->toc() } });
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
            { QStringLiteral("query"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } },
            { QStringLiteral("max_matches"), QJsonObject { { QStringLiteral("type"), QStringLiteral("integer") } } }
        } },
        { QStringLiteral("required"), QJsonArray { QStringLiteral("query") } }
    };
    add(registry, QStringLiteral("book.search"),
        QStringLiteral("Search text resources. Returns snippets, never whole files."),
        ToolRisk::Read, false, false, search_schema,
        [workspace](const QJsonObject &arguments) {
            const QString query = arguments.value(QStringLiteral("query")).toString();
            const int max_matches = arguments.value(QStringLiteral("max_matches")).toInt(20);
            return ToolResult::success(QJsonObject {
                { QStringLiteral("matches"), workspace->search(query, max_matches) }
            });
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
        QStringLiteral("List CSS stylesheets with bounded text."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(QJsonObject {
                { QStringLiteral("stylesheets"), workspace->stylesheets() }
            });
        });

    add(registry, QStringLiteral("font.inventory"),
        QStringLiteral("List embedded fonts and CSS font-family names. Never returns font file bytes."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(workspace->fontInventory());
        });

    add(registry, QStringLiteral("book.validate"),
        QStringLiteral("Run structural checks on the open book (spine, body, basic CSS)."),
        ToolRisk::Read, false, false, emptyObjectSchema(),
        [workspace](const QJsonObject &) {
            return ToolResult::success(workspace->validate());
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
        QStringLiteral("Stage package metadata updates such as title or language."),
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
                    QStringLiteral("resource.replace_text is capped at 65536 characters. Use content.typeset_from_manuscript or content.fill_section so chapter bodies never enter the model context."));
            }
            return fromBook(workspace->replaceText(
                arguments.value(QStringLiteral("resource_id")).toString(),
                text,
                static_cast<quint64>(arguments.value(QStringLiteral("expected_revision")).toInteger())));
        });

    add(registry, QStringLiteral("manuscript.parse"),
        QStringLiteral("Parse a dropped light-novel TXT (or ImportTXT HTML) into title, credits, synopsis, TOC, chapter list, and illustration names. Returns a compact summary only — never chapter bodies. Omit manuscript_id to auto-detect. Call this before content.typeset_from_manuscript."),
        ToolRisk::Read, false, false,
        QJsonObject {
            { QStringLiteral("type"), QStringLiteral("object") },
            { QStringLiteral("properties"), QJsonObject {
                { QStringLiteral("manuscript_id"), QJsonObject { { QStringLiteral("type"), QStringLiteral("string") } } }
            } }
        },
        [workspace](const QJsonObject &arguments) {
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
}

} // namespace SigilAgent
