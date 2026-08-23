/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/MemoryBookWorkspace.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include "Agent/Execution/PatchRange.h"
#include "Agent/Execution/ResourceMutations.h"
#include "PluginAPI/PluginTextEdit.h"

namespace SigilAgent
{

namespace
{

const int kMaxFragment = 8192;
const QStringList kGenericFonts = {
    QStringLiteral("serif"), QStringLiteral("sans-serif"), QStringLiteral("monospace"),
    QStringLiteral("cursive"), QStringLiteral("fantasy"), QStringLiteral("system-ui")
};

QString sha256Text(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace

MemoryBookWorkspace::MemoryBookWorkspace()
{
    m_metadata = QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Untitled") },
        { QStringLiteral("language"), QStringLiteral("en") }
    };
}

MemoryBookWorkspace MemoryBookWorkspace::samplePhysicsBook()
{
    MemoryBookWorkspace book;
    book.setEpubVersion(QStringLiteral("3.0"));
    book.setMetadata(QJsonObject {
        { QStringLiteral("title"), QStringLiteral("Junior Physics") },
        { QStringLiteral("language"), QStringLiteral("zh-CN") },
        { QStringLiteral("creator"), QStringLiteral("rescueme") }
    });

    MemoryResource ch1;
    ch1.id = QStringLiteral("ch1");
    ch1.bookPath = QStringLiteral("OEBPS/Text/ch1.xhtml");
    ch1.mediaType = QStringLiteral("application/xhtml+xml");
    ch1.kind = QStringLiteral("xhtml");
    ch1.text = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Heat</title>"
        "<link rel=\"stylesheet\" href=\"../Styles/style.css\"/></head>"
        "<body><h1>Chapter 1 Heat</h1><p>Water boils at 100 degrees.</p></body></html>");
    book.addResource(ch1);

    MemoryResource ch2;
    ch2.id = QStringLiteral("ch2");
    ch2.bookPath = QStringLiteral("OEBPS/Text/ch2.xhtml");
    ch2.mediaType = QStringLiteral("application/xhtml+xml");
    ch2.kind = QStringLiteral("xhtml");
    ch2.text = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Light</title>"
        "<link rel=\"stylesheet\" href=\"../Styles/style.css\"/></head>"
        "<body><h1>Chapter 2 Light</h1><p>A lens can focus sunlight.</p></body></html>");
    book.addResource(ch2);

    MemoryResource css;
    css.id = QStringLiteral("css");
    css.bookPath = QStringLiteral("OEBPS/Styles/style.css");
    css.mediaType = QStringLiteral("text/css");
    css.kind = QStringLiteral("css");
    css.text = QStringLiteral(
        "@font-face { font-family: SerifFace; src: url(../Fonts/serif.ttf); }\n"
        "body { font-family: SerifFace, serif; line-height: 1.6; }\n"
        "h1 { font-family: SerifFace; font-size: 1.6em; }\n");
    book.addResource(css);

    MemoryResource font;
    font.id = QStringLiteral("font-serif");
    font.bookPath = QStringLiteral("OEBPS/Fonts/serif.ttf");
    font.mediaType = QStringLiteral("font/ttf");
    font.kind = QStringLiteral("font");
    font.binary = QByteArray("OTTO-FAKE-FONT");
    book.addResource(font);

    book.setSpine({ QStringLiteral("ch1"), QStringLiteral("ch2") });
    book.setToc(QJsonArray {
        QJsonObject {
            { QStringLiteral("label"), QStringLiteral("Chapter 1 Heat") },
            { QStringLiteral("href"), QStringLiteral("OEBPS/Text/ch1.xhtml") },
            { QStringLiteral("resource_id"), QStringLiteral("ch1") }
        },
        QJsonObject {
            { QStringLiteral("label"), QStringLiteral("Chapter 2 Light") },
            { QStringLiteral("href"), QStringLiteral("OEBPS/Text/ch2.xhtml") },
            { QStringLiteral("resource_id"), QStringLiteral("ch2") }
        }
    });
    return book;
}

void MemoryBookWorkspace::addResource(const MemoryResource &resource)
{
    m_resources.insert(resource.id, resource);
}

void MemoryBookWorkspace::setMetadata(const QJsonObject &metadata)
{
    m_metadata = metadata;
}

void MemoryBookWorkspace::setSpine(const QStringList &ids)
{
    m_spineIds = ids;
}

void MemoryBookWorkspace::setToc(const QJsonArray &toc)
{
    m_toc = toc;
}

void MemoryBookWorkspace::setEpubVersion(const QString &version)
{
    m_epubVersion = version;
}

void MemoryBookWorkspace::bumpRevision()
{
    ++m_revision;
}

quint64 MemoryBookWorkspace::revision() const
{
    return m_revision;
}

QJsonObject MemoryBookWorkspace::summary() const
{
    int xhtml = 0;
    int css = 0;
    int fonts = 0;
    int images = 0;
    for (const MemoryResource &resource : m_resources) {
        if (resource.kind == QLatin1String("xhtml")) ++xhtml;
        else if (resource.kind == QLatin1String("css")) ++css;
        else if (resource.kind == QLatin1String("font")) ++fonts;
        else if (resource.kind == QLatin1String("image")) ++images;
    }
    return QJsonObject {
        { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) },
        { QStringLiteral("epub_version"), m_epubVersion },
        { QStringLiteral("title"), m_metadata.value(QStringLiteral("title")).toString() },
        { QStringLiteral("language"), m_metadata.value(QStringLiteral("language")).toString() },
        { QStringLiteral("spine_count"), m_spineIds.size() },
        { QStringLiteral("toc_count"), m_toc.size() },
        { QStringLiteral("resources"), QJsonObject {
            { QStringLiteral("xhtml"), xhtml },
            { QStringLiteral("css"), css },
            { QStringLiteral("fonts"), fonts },
            { QStringLiteral("images"), images },
            { QStringLiteral("total"), m_resources.size() }
        } }
    };
}

QJsonArray MemoryBookWorkspace::resources() const
{
    QJsonArray array;
    for (const MemoryResource &resource : m_resources) {
        array.append(resourceJson(resource));
    }
    return array;
}

QJsonArray MemoryBookWorkspace::spine() const
{
    QJsonArray array;
    int index = 0;
    for (const QString &id : m_spineIds) {
        const MemoryResource *resource = findResource(id);
        QJsonObject item {
            { QStringLiteral("index"), index++ },
            { QStringLiteral("resource_id"), id }
        };
        if (resource) {
            item.insert(QStringLiteral("book_path"), resource->bookPath);
            item.insert(QStringLiteral("media_type"), resource->mediaType);
        }
        array.append(item);
    }
    return array;
}

QJsonArray MemoryBookWorkspace::toc() const
{
    return m_toc;
}

QJsonObject MemoryBookWorkspace::metadata() const
{
    QJsonObject object = m_hasStagedMetadata ? m_stagedMetadata : m_metadata;
    object.insert(QStringLiteral("book_revision"), static_cast<qint64>(m_revision));
    return object;
}

QJsonArray MemoryBookWorkspace::search(const QString &query, int max_matches) const
{
    QJsonArray matches;
    if (query.isEmpty() || max_matches <= 0) return matches;
    const int limit = qMin(max_matches, 50);
    for (const MemoryResource &resource : m_resources) {
        if (resource.text.isEmpty()) continue;
        const QString haystack = currentText(resource);
        int from = 0;
        while (matches.size() < limit) {
            const int found = haystack.indexOf(query, from, Qt::CaseInsensitive);
            if (found < 0) break;
            const int start = qMax(0, found - 24);
            const int length = qMin(haystack.size() - start, query.size() + 48);
            matches.append(QJsonObject {
                { QStringLiteral("resource_id"), resource.id },
                { QStringLiteral("book_path"), resource.bookPath },
                { QStringLiteral("offset"), found },
                { QStringLiteral("snippet"), haystack.mid(start, length) }
            });
            from = found + qMax(1, query.size());
        }
        if (matches.size() >= limit) break;
    }
    return matches;
}

BookOpResult MemoryBookWorkspace::readFragment(const QString &resource_id, int offset, int limit) const
{
    const MemoryResource *resource = findResource(resource_id);
    QString staged_added;
    quint64 staged_rev = 0;
    QString staged_path;
    if (!resource && m_transaction
        && m_transaction->ReadAddedText(resource_id, &staged_added, &staged_rev)) {
        for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
            if (addition.stagingId == resource_id) staged_path = addition.bookPath;
        }
    } else if (!resource) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                   QStringLiteral("Unknown resource"));
    }
    if (resource && (resource->kind == QLatin1String("font") || resource->kind == QLatin1String("image"))) {
        return BookOpResult::error(QStringLiteral("BINARY_NOT_IN_CONTEXT"),
                                   QStringLiteral("Font and image binaries are never returned to the model"));
    }
    const QString text = resource ? currentText(*resource) : staged_added;
    const int start = qMax(0, offset);
    int count = limit <= 0 ? 2048 : limit;
    count = qMin(count, kMaxFragment);
    const QString fragment = text.mid(start, count);
    const bool truncated = start + fragment.size() < text.size();
    QJsonObject data {
        { QStringLiteral("resource_id"), resource ? resource->id : resource_id },
        { QStringLiteral("book_path"), resource ? resource->bookPath : staged_path },
        { QStringLiteral("offset"), start },
        { QStringLiteral("end"), start + fragment.size() },
        { QStringLiteral("length"), fragment.size() },
        { QStringLiteral("total"), text.size() },
        { QStringLiteral("truncated"), truncated },
        { QStringLiteral("continuation"), truncated ? start + fragment.size() : QJsonValue() },
        { QStringLiteral("hash"), sha256Text(text) },
        { QStringLiteral("revision"), static_cast<qint64>(resource ? resource->revision : staged_rev) },
        { QStringLiteral("text"), fragment }
    };
    addFragmentLineMetadata(&data, text, start, fragment.size());
    return BookOpResult::success(data);
}

QJsonArray MemoryBookWorkspace::stylesheets() const
{
    QJsonArray array;
    for (const MemoryResource &resource : m_resources) {
        if (resource.kind != QLatin1String("css")) continue;
        const QString text = currentText(resource);
        array.append(QJsonObject {
            { QStringLiteral("resource_id"), resource.id },
            { QStringLiteral("book_path"), resource.bookPath },
            { QStringLiteral("revision"), static_cast<qint64>(resource.revision) },
            { QStringLiteral("length"), text.size() },
            { QStringLiteral("text"), text.left(kMaxFragment) },
            { QStringLiteral("truncated"), text.size() > kMaxFragment }
        });
    }
    return array;
}

QJsonObject MemoryBookWorkspace::fontInventory() const
{
    QJsonArray fonts;
    QJsonArray css_families;
    QSet<QString> declared;
    for (const MemoryResource &resource : m_resources) {
        if (resource.kind == QLatin1String("font")) {
            fonts.append(QJsonObject {
                { QStringLiteral("resource_id"), resource.id },
                { QStringLiteral("book_path"), resource.bookPath },
                { QStringLiteral("media_type"), resource.mediaType },
                { QStringLiteral("bytes"), resource.binary.size() }
            });
        } else if (resource.kind == QLatin1String("css")) {
            const QStringList families = extractFontFamilies(currentText(resource));
            for (const QString &family : families) {
                declared.insert(family);
                css_families.append(QJsonObject {
                    { QStringLiteral("resource_id"), resource.id },
                    { QStringLiteral("family"), family }
                });
            }
        }
    }
    QStringList declared_list = declared.values();
    declared_list.sort();
    QJsonArray declared_array;
    for (const QString &family : declared_list) declared_array.append(family);
    return QJsonObject {
        { QStringLiteral("embedded_fonts"), fonts },
        { QStringLiteral("css_families"), css_families },
        { QStringLiteral("declared_families"), declared_array }
    };
}

QJsonObject MemoryBookWorkspace::validate() const
{
    QJsonArray issues;
    if (m_spineIds.isEmpty()) {
        issues.append(QJsonObject {
            { QStringLiteral("severity"), QStringLiteral("error") },
            { QStringLiteral("code"), QStringLiteral("EMPTY_SPINE") },
            { QStringLiteral("message"), QStringLiteral("Spine has no items") }
        });
    }
    const QJsonObject inventory = fontInventory();
    QSet<QString> available;
    for (const QJsonValue &value : inventory.value(QStringLiteral("declared_families")).toArray()) {
        available.insert(value.toString().toLower());
    }
    for (const MemoryResource &resource : m_resources) {
        if (resource.kind == QLatin1String("xhtml") && !currentText(resource).contains(QLatin1String("<body"))) {
            issues.append(QJsonObject {
                { QStringLiteral("severity"), QStringLiteral("error") },
                { QStringLiteral("code"), QStringLiteral("MISSING_BODY") },
                { QStringLiteral("resource_id"), resource.id },
                { QStringLiteral("message"), QStringLiteral("XHTML resource has no body element") }
            });
        }
        if (resource.kind == QLatin1String("css")) {
            const QStringList families = extractFontFamilies(currentText(resource));
            for (const QString &family : families) {
                if (kGenericFonts.contains(family.toLower())) continue;
                bool embedded = false;
                for (const MemoryResource &candidate : m_resources) {
                    if (candidate.kind == QLatin1String("font")
                        && candidate.bookPath.contains(family, Qt::CaseInsensitive)) {
                        embedded = true;
                        break;
                    }
                }
                if (!embedded && !available.contains(family.toLower())
                    && !currentText(resource).contains(QStringLiteral("font-family: %1").arg(family))) {
                    Q_UNUSED(embedded);
                }
            }
        }
    }
    return QJsonObject {
        { QStringLiteral("ok"), issues.isEmpty() },
        { QStringLiteral("issue_count"), issues.size() },
        { QStringLiteral("issues"), issues }
    };
}

bool MemoryBookWorkspace::hasOpenTransaction() const
{
    return m_transaction != nullptr;
}

BookOpResult MemoryBookWorkspace::beginTransaction(const QString &label)
{
    if (m_transaction) {
        return BookOpResult::error(QStringLiteral("TRANSACTION_OPEN"),
                                   QStringLiteral("A transaction is already open"));
    }
    m_transaction = std::make_unique<PluginApi::TextTransaction>(
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        label.isEmpty() ? QStringLiteral("agent") : label,
        QStringLiteral("auto"),
        m_revision);
    m_hasStagedMetadata = false;
    m_stagedMetadata = QJsonObject();
    m_stagedAfterIds.clear();
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), m_transaction->Id() },
        { QStringLiteral("base_book_revision"), static_cast<qint64>(m_revision) }
    });
}

BookOpResult MemoryBookWorkspace::previewTransaction() const
{
    if (!m_transaction) {
        return BookOpResult::error(QStringLiteral("NO_TRANSACTION"),
                                   QStringLiteral("No open transaction"));
    }
    QJsonArray changes;
    for (const PluginApi::StagedTextChange &change : m_transaction->Changes()) {
        int diff = 0;
        const int limit = qMin(change.originalText.size(), change.stagedText.size());
        while (diff < limit && change.originalText.at(diff) == change.stagedText.at(diff)) ++diff;
        const int excerpt_from = qMax(0, diff - 24);
        changes.append(QJsonObject {
            { QStringLiteral("resource_id"), change.resourceId },
            { QStringLiteral("original_length"), change.originalText.size() },
            { QStringLiteral("staged_length"), change.stagedText.size() },
            { QStringLiteral("changed"), change.originalText != change.stagedText },
            { QStringLiteral("original_excerpt"), change.originalText.mid(excerpt_from, 80) },
            { QStringLiteral("staged_excerpt"), change.stagedText.mid(excerpt_from, 80) }
        });
    }
    for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
        changes.append(QJsonObject {
            { QStringLiteral("resource_id"), addition.stagingId },
            { QStringLiteral("book_path"), addition.bookPath },
            { QStringLiteral("added"), true },
            { QStringLiteral("staged_length"), addition.data.size() }
        });
    }
    QJsonObject data {
        { QStringLiteral("transaction_id"), m_transaction->Id() },
        { QStringLiteral("base_book_revision"), static_cast<qint64>(m_transaction->BaseBookRevision()) },
        { QStringLiteral("live_book_revision"), static_cast<qint64>(m_revision) },
        { QStringLiteral("changes"), changes },
        { QStringLiteral("metadata_changed"), m_hasStagedMetadata }
    };
    BookOpResult result = BookOpResult::success(data, false, true);
    return result;
}

BookOpResult MemoryBookWorkspace::commitTransaction(quint64 expected_revision)
{
    if (!m_transaction) {
        return BookOpResult::error(QStringLiteral("NO_TRANSACTION"),
                                   QStringLiteral("No open transaction"));
    }
    if (expected_revision != m_revision) {
        return BookOpResult::error(QStringLiteral("BOOK_REVISION_CONFLICT"),
                                   QStringLiteral("expected %1 actual %2")
                                       .arg(expected_revision).arg(m_revision));
    }

    QHash<QString, QString> originals;
    QList<PluginApi::StagedTextChange> changes = m_transaction->Changes();
    QJsonObject original_metadata = m_metadata;
    QStringList original_spine = m_spineIds;
    QHash<QString, MemoryResource> added_live;
    int applied = 0;
    QString fail_message;

    for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
        MemoryResource resource;
        resource.id = addition.stagingId;
        resource.bookPath = addition.bookPath;
        resource.mediaType = addition.mediaType;
        resource.kind = kindFromPathOrType(addition.bookPath, QString());
        if (addition.mediaType.contains(QLatin1String("css"))) resource.kind = QStringLiteral("css");
        resource.text = addition.isText ? QString::fromUtf8(addition.data) : QString();
        resource.binary = addition.isText ? QByteArray() : addition.data;
        addResource(resource);
        added_live.insert(resource.id, resource);
        if (addition.addToSpine && resource.kind == QLatin1String("xhtml")) {
            const QString after = m_stagedAfterIds.value(addition.stagingId);
            const int index = m_spineIds.indexOf(after);
            if (index >= 0) m_spineIds.insert(index + 1, resource.id);
            else m_spineIds.append(resource.id);
        }
        ++applied;
    }

    for (const PluginApi::StagedTextChange &change : changes) {
        MemoryResource *resource = findResource(change.resourceId);
        if (!resource) {
            fail_message = QStringLiteral("Missing resource %1").arg(change.resourceId);
            break;
        }
        originals.insert(resource->id, resource->text);
        if (m_failAfter >= 0 && applied >= m_failAfter) {
            fail_message = QStringLiteral("injected mid-batch failure");
            break;
        }
        resource->text = change.stagedText;
        resource->revision += 1;
        ++applied;
    }

    if (fail_message.isEmpty() && m_hasStagedMetadata) {
        if (m_failAfter >= 0 && applied >= m_failAfter) {
            fail_message = QStringLiteral("injected mid-batch failure");
        } else {
            m_metadata = m_stagedMetadata;
            ++applied;
        }
    }

    if (!fail_message.isEmpty()) {
        for (auto it = originals.constBegin(); it != originals.constEnd(); ++it) {
            if (MemoryResource *resource = findResource(it.key())) {
                resource->text = it.value();
                if (resource->revision > 1) resource->revision -= 1;
            }
        }
        m_metadata = original_metadata;
        m_spineIds = original_spine;
        for (auto it = added_live.constBegin(); it != added_live.constEnd(); ++it) {
            m_resources.remove(it.key());
        }
        m_transaction.reset();
        m_hasStagedMetadata = false;
        m_stagedMetadata = QJsonObject();
        m_stagedAfterIds.clear();
        return BookOpResult::error(QStringLiteral("TRANSACTION_ROLLED_BACK"), fail_message);
    }

    const QString txid = m_transaction->Id();
    m_transaction.reset();
    m_hasStagedMetadata = false;
    m_stagedMetadata = QJsonObject();
    m_stagedAfterIds.clear();
    ++m_revision;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), txid },
        { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) },
        { QStringLiteral("applied_changes"), applied }
    }, true);
}

BookOpResult MemoryBookWorkspace::rollbackTransaction()
{
    if (!m_transaction) {
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("rolled_back"), false }
        });
    }
    const QString txid = m_transaction->Id();
    m_transaction->Clear();
    m_transaction.reset();
    m_hasStagedMetadata = false;
    m_stagedMetadata = QJsonObject();
    m_stagedAfterIds.clear();
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), txid },
        { QStringLiteral("rolled_back"), true },
        { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
    });
}

BookOpResult MemoryBookWorkspace::patchFragment(const QString &resource_id,
                                                int start,
                                                int end,
                                                const QString &text,
                                                quint64 expected_resource_revision,
                                                const QString &expected_text,
                                                int start_line)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    MemoryResource *resource = findResource(resource_id);
    QString added_source;
    quint64 added_revision = 0;
    const bool staged_new = !resource && m_transaction
        && m_transaction->ReadAddedText(resource_id, &added_source, &added_revision);
    if (!resource && !staged_new) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                   QStringLiteral("Unknown resource"));
    }
    const QString source = resource ? currentText(*resource) : added_source;
    const PatchRangeResolution resolved = resolvePatchRange(
        source, start, end, expected_text, start_line);
    if (!resolved.ok) {
        return BookOpResult::error(resolved.code, resolved.message, resolved.data);
    }
    QString error;
    QJsonArray edits {
        QJsonObject {
            { QStringLiteral("start"), resolved.start },
            { QStringLiteral("end"), resolved.end },
            { QStringLiteral("text"), text }
        }
    };
    const bool applied_edits = staged_new
        ? m_transaction->ApplyAddedTextEdits(resource_id, expected_resource_revision, edits, &error)
        : m_transaction->ApplyEdits(resource->id, resource->text, resource->revision,
                                    expected_resource_revision, edits, &error);
    if (!applied_edits) {
        const QString code = error.contains(QLatin1String("Revision"))
            ? QStringLiteral("BOOK_REVISION_CONFLICT") : QStringLiteral("PATCH_FAILED");
        return BookOpResult::error(code, error);
    }
    QJsonObject data = resolved.data;
    data.insert(QStringLiteral("resource_id"), resource ? resource->id : resource_id);
    data.insert(QStringLiteral("staged"), true);
    data.insert(QStringLiteral("live_unchanged"), true);
    data.insert(QStringLiteral("replacement_length"), text.size());
    return BookOpResult::success(data, false, true);
}

BookOpResult MemoryBookWorkspace::updateCss(const QString &resource_id,
                                            const QString &text,
                                            quint64 expected_resource_revision)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    MemoryResource *resource = findResource(resource_id);
    if (!resource) {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                   QStringLiteral("Unknown resource"));
    }
    QString error;
    if (!m_transaction->ReplaceText(resource->id, resource->text, resource->revision,
                                    expected_resource_revision, text, &error)) {
        const QString code = error.contains(QLatin1String("Revision"))
            ? QStringLiteral("BOOK_REVISION_CONFLICT") : QStringLiteral("CSS_UPDATE_FAILED");
        return BookOpResult::error(code, error);
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("resource_id"), resource->id },
        { QStringLiteral("staged"), true }
    }, false, true);
}

BookOpResult MemoryBookWorkspace::updateMetadata(const QJsonObject &patch)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    QJsonObject next = m_hasStagedMetadata ? m_stagedMetadata : m_metadata;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        next.insert(it.key(), it.value());
    }
    m_stagedMetadata = next;
    m_hasStagedMetadata = true;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("metadata"), next }
    }, false, true);
}

QStringList MemoryBookWorkspace::allBookPaths() const
{
    QStringList paths;
    for (const MemoryResource &resource : m_resources) paths.append(resource.bookPath);
    if (m_transaction) {
        for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
            paths.append(addition.bookPath);
        }
    }
    return paths;
}

BookOpResult MemoryBookWorkspace::stageAddition(const QString &book_path,
                                                const QString &kind,
                                                const QString &text,
                                                bool add_to_spine,
                                                const QString &after_resource_id)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    const QString resolved_kind = kindFromPathOrType(book_path, kind);
    if (resolved_kind != QLatin1String("xhtml") && resolved_kind != QLatin1String("css")) {
        return BookOpResult::error(QStringLiteral("UNSUPPORTED_KIND"),
                                   QStringLiteral("Only xhtml and css resources can be created"));
    }
    if (book_path.trimmed().isEmpty()) {
        return BookOpResult::error(QStringLiteral("BOOK_PATH_REQUIRED"),
                                   QStringLiteral("book_path is required"));
    }
    if (bookPathTaken(book_path, allBookPaths())) {
        return BookOpResult::error(QStringLiteral("BOOK_PATH_EXISTS"),
                                   QStringLiteral("A resource already uses %1").arg(book_path));
    }
    PluginApi::StagedResourceAddition addition;
    addition.stagingId = QStringLiteral("new:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    addition.bookPath = book_path.trimmed();
    addition.mediaType = mediaTypeForKind(resolved_kind);
    addition.manifestId = QFileInfo(addition.bookPath).completeBaseName();
    addition.data = text.toUtf8();
    addition.stagedRevision = 1;
    addition.manifested = true;
    addition.addToSpine = add_to_spine && resolved_kind == QLatin1String("xhtml");
    addition.isText = true;
    QString error;
    if (!m_transaction->AddResource(addition, &error)) {
        return BookOpResult::error(QStringLiteral("ADD_RESOURCE_FAILED"), error);
    }
    if (!after_resource_id.isEmpty()) m_stagedAfterIds.insert(addition.stagingId, after_resource_id);
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staging_id"), addition.stagingId },
        { QStringLiteral("resource_id"), addition.stagingId },
        { QStringLiteral("book_path"), addition.bookPath },
        { QStringLiteral("kind"), resolved_kind },
        { QStringLiteral("add_to_spine"), addition.addToSpine },
        { QStringLiteral("revision"), static_cast<qint64>(addition.stagedRevision) },
        { QStringLiteral("staged"), true },
        { QStringLiteral("live_unchanged"), true }
    }, false, true);
}

BookOpResult MemoryBookWorkspace::createResource(const QString &book_path,
                                                 const QString &kind,
                                                 const QString &text,
                                                 bool add_to_spine,
                                                 const QString &after_resource_id)
{
    const QString resolved_kind = kindFromPathOrType(book_path, kind);
    const QString body = text.isEmpty() && resolved_kind == QLatin1String("xhtml")
        ? defaultXhtmlTemplate() : text;
    return stageAddition(book_path, resolved_kind, body, add_to_spine, after_resource_id);
}

BookOpResult MemoryBookWorkspace::copyResource(const QString &source_id,
                                               const QString &book_path,
                                               bool add_to_spine)
{
    const MemoryResource *source = findResource(source_id);
    QString staged_text;
    quint64 staged_rev = 0;
    QString source_path;
    QString source_kind;
    if (source) {
        source_path = source->bookPath;
        source_kind = source->kind;
        staged_text = currentText(*source);
    } else if (m_transaction && m_transaction->ReadAddedText(source_id, &staged_text, &staged_rev)) {
        for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
            if (addition.stagingId == source_id) {
                source_path = addition.bookPath;
                source_kind = kindFromPathOrType(addition.bookPath, QString());
            }
        }
    } else {
        return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"),
                                   QStringLiteral("Unknown source resource"));
    }
    if (source_kind == QLatin1String("font") || source_kind == QLatin1String("image")) {
        return BookOpResult::error(QStringLiteral("BINARY_NOT_IN_CONTEXT"),
                                   QStringLiteral("Copying font/image binaries is not supported"));
    }
    QString target = book_path.trimmed();
    if (target.isEmpty()) target = suggestCopyBookPath(source_path, allBookPaths());
    return stageAddition(target, source_kind, staged_text, add_to_spine, source ? source->id : source_id);
}

BookOpResult MemoryBookWorkspace::createCheckpoint(const QString &label)
{
    MemoryCheckpoint checkpoint;
    checkpoint.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    checkpoint.label = label.isEmpty() ? QStringLiteral("agent") : label;
    checkpoint.bookRevision = m_revision;
    checkpoint.resources = m_resources;
    checkpoint.metadata = m_metadata;
    checkpoint.spineIds = m_spineIds;
    checkpoint.toc = m_toc;
    m_checkpoints.append(checkpoint);
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("checkpoint_id"), checkpoint.id },
        { QStringLiteral("label"), checkpoint.label },
        { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
    });
}

QJsonArray MemoryBookWorkspace::listCheckpoints() const
{
    QJsonArray array;
    for (const MemoryCheckpoint &checkpoint : m_checkpoints) {
        array.append(QJsonObject {
            { QStringLiteral("checkpoint_id"), checkpoint.id },
            { QStringLiteral("label"), checkpoint.label },
            { QStringLiteral("book_revision"), static_cast<qint64>(checkpoint.bookRevision) }
        });
    }
    return array;
}

BookOpResult MemoryBookWorkspace::restoreCheckpoint(const QString &checkpoint_id)
{
    for (const MemoryCheckpoint &checkpoint : m_checkpoints) {
        if (checkpoint.id != checkpoint_id) continue;
        m_resources = checkpoint.resources;
        m_metadata = checkpoint.metadata;
        m_spineIds = checkpoint.spineIds;
        m_toc = checkpoint.toc;
        ++m_revision;
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("checkpoint_id"), checkpoint.id },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
        }, true);
    }
    return BookOpResult::error(QStringLiteral("CHECKPOINT_NOT_FOUND"),
                               QStringLiteral("Unknown checkpoint"));
}

QString MemoryBookWorkspace::resourceText(const QString &resource_id) const
{
    const MemoryResource *resource = findResource(resource_id);
    return resource ? resource->text : QString();
}

quint64 MemoryBookWorkspace::resourceRevision(const QString &resource_id) const
{
    const MemoryResource *resource = findResource(resource_id);
    return resource ? resource->revision : 0;
}

void MemoryBookWorkspace::setInjectedFailureIndex(int index)
{
    m_failAfter = index;
}

MemoryResource *MemoryBookWorkspace::findResource(const QString &id_or_path)
{
    auto found = m_resources.find(id_or_path);
    if (found != m_resources.end()) return &found.value();
    for (auto it = m_resources.begin(); it != m_resources.end(); ++it) {
        if (it->bookPath == id_or_path) return &it.value();
    }
    return nullptr;
}

const MemoryResource *MemoryBookWorkspace::findResource(const QString &id_or_path) const
{
    auto found = m_resources.constFind(id_or_path);
    if (found != m_resources.constEnd()) return &found.value();
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        if (it->bookPath == id_or_path) return &it.value();
    }
    return nullptr;
}

BookOpResult MemoryBookWorkspace::ensureTransaction()
{
    if (m_transaction) {
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("transaction_id"), m_transaction->Id() }
        });
    }
    return beginTransaction(QStringLiteral("auto"));
}

QString MemoryBookWorkspace::currentText(const MemoryResource &resource) const
{
    if (!m_transaction) return resource.text;
    quint64 ignored = 0;
    return m_transaction->ReadText(resource.id, resource.text, resource.revision, &ignored);
}

QJsonObject MemoryBookWorkspace::resourceJson(const MemoryResource &resource) const
{
    return QJsonObject {
        { QStringLiteral("resource_id"), resource.id },
        { QStringLiteral("book_path"), resource.bookPath },
        { QStringLiteral("media_type"), resource.mediaType },
        { QStringLiteral("kind"), resource.kind },
        { QStringLiteral("revision"), static_cast<qint64>(resource.revision) },
        { QStringLiteral("text_length"), resource.text.size() },
        { QStringLiteral("binary_bytes"), resource.binary.size() }
    };
}

QStringList MemoryBookWorkspace::extractFontFamilies(const QString &css) const
{
    QStringList families;
    QRegularExpression face(QStringLiteral("font-family\\s*:\\s*([^;}{]+)"));
    auto it = face.globalMatch(css);
    while (it.hasNext()) {
        const QString raw = it.next().captured(1);
        const QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString part : parts) {
            part = part.trimmed();
            if (part.startsWith(QLatin1Char('"')) || part.startsWith(QLatin1Char('\''))) {
                part = part.mid(1);
            }
            if (part.endsWith(QLatin1Char('"')) || part.endsWith(QLatin1Char('\''))) {
                part.chop(1);
            }
            part = part.trimmed();
            if (!part.isEmpty() && !families.contains(part)) families.append(part);
        }
    }
    return families;
}

} // namespace SigilAgent
