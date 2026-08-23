/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/SigilBookWorkspace.h"

#include <QCryptographicHash>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QXmlStreamReader>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/NcxNavigation.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"

namespace SigilAgent
{

namespace
{

const int kMaxFragment = 8192;

QString sha256Text(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace

void SigilBookWorkspace::setBook(QSharedPointer<Book> book)
{
    if (m_transaction) {
        m_transaction->Clear();
        m_transaction.reset();
    }
    m_book = book;
    m_tracked.clear();
    m_checkpoints.clear();
    m_hasStagedMetadata = false;
    m_stagedMetadata = QJsonObject();
    m_revision = 1;
}

QSharedPointer<Book> SigilBookWorkspace::book() const
{
    return m_book;
}

BookOpResult SigilBookWorkspace::invokeOp(const std::function<BookOpResult()> &fn) const
{
    if (!m_book) return BookOpResult::error(QStringLiteral("NO_BOOK"), QStringLiteral("No book is open"));
    if (QThread::currentThread() == m_book->thread()) return fn();
    BookOpResult result;
    QMetaObject::invokeMethod(m_book.data(), [&result, &fn]() { result = fn(); },
                              Qt::BlockingQueuedConnection);
    return result;
}

QJsonObject SigilBookWorkspace::invokeJson(const std::function<QJsonObject()> &fn) const
{
    if (!m_book) return QJsonObject();
    if (QThread::currentThread() == m_book->thread()) return fn();
    QJsonObject result;
    QMetaObject::invokeMethod(m_book.data(), [&result, &fn]() { result = fn(); },
                              Qt::BlockingQueuedConnection);
    return result;
}

QJsonArray SigilBookWorkspace::invokeArray(const std::function<QJsonArray()> &fn) const
{
    if (!m_book) return QJsonArray();
    if (QThread::currentThread() == m_book->thread()) return fn();
    QJsonArray result;
    QMetaObject::invokeMethod(m_book.data(), [&result, &fn]() { result = fn(); },
                              Qt::BlockingQueuedConnection);
    return result;
}

QString SigilBookWorkspace::invokeString(const std::function<QString()> &fn) const
{
    if (!m_book) return QString();
    if (QThread::currentThread() == m_book->thread()) return fn();
    QString result;
    QMetaObject::invokeMethod(m_book.data(), [&result, &fn]() { result = fn(); },
                              Qt::BlockingQueuedConnection);
    return result;
}

Resource *SigilBookWorkspace::findResource(const QString &id_or_path) const
{
    if (!m_book) return nullptr;
    FolderKeeper *keeper = m_book->GetFolderKeeper();
    Resource *by_id = keeper->GetResourceByIdentifier(id_or_path);
    if (by_id) return by_id;
    return keeper->GetResourceByBookPathNoThrow(id_or_path);
}

TextResource *SigilBookWorkspace::textResource(const QString &id_or_path) const
{
    return qobject_cast<TextResource *>(findResource(id_or_path));
}

QString SigilBookWorkspace::liveText(TextResource *resource) const
{
    return resource ? resource->GetText() : QString();
}

QString SigilBookWorkspace::currentText(TextResource *resource) const
{
    if (!resource) return QString();
    const QString live = liveText(resource);
    if (!m_transaction) return live;
    quint64 ignored = 0;
    return m_transaction->ReadText(resource->GetIdentifier(), live, trackedRevision(resource), &ignored);
}

QString SigilBookWorkspace::kindOf(Resource *resource) const
{
    if (!resource) return QStringLiteral("other");
    switch (resource->Type()) {
        case Resource::HTMLResourceType: return QStringLiteral("xhtml");
        case Resource::CSSResourceType: return QStringLiteral("css");
        case Resource::FontResourceType: return QStringLiteral("font");
        case Resource::ImageResourceType:
        case Resource::SVGResourceType: return QStringLiteral("image");
        case Resource::OPFResourceType: return QStringLiteral("opf");
        case Resource::NCXResourceType: return QStringLiteral("ncx");
        default: return QStringLiteral("other");
    }
}

quint64 SigilBookWorkspace::trackedRevision(Resource *resource) const
{
    if (!resource) return 0;
    const QString id = resource->GetIdentifier();
    TextResource *text = qobject_cast<TextResource *>(resource);
    const QString live = text ? text->GetText() : QString();
    TrackedResource &tracked = m_tracked[id];
    if (!tracked.lastText.isEmpty() && text && tracked.lastText != live) {
        tracked.revision += 1;
    }
    if (text) tracked.lastText = live;
    if (tracked.revision == 0) tracked.revision = 1;
    return tracked.revision;
}

void SigilBookWorkspace::noteText(Resource *resource, const QString &text) const
{
    if (!resource) return;
    TrackedResource &tracked = m_tracked[resource->GetIdentifier()];
    tracked.lastText = text;
    tracked.revision += 1;
}

QJsonObject SigilBookWorkspace::resourceJson(Resource *resource) const
{
    if (!resource) return QJsonObject();
    TextResource *text = qobject_cast<TextResource *>(resource);
    return QJsonObject {
        { QStringLiteral("resource_id"), resource->GetIdentifier() },
        { QStringLiteral("book_path"), resource->GetRelativePath() },
        { QStringLiteral("media_type"), resource->GetMediaType() },
        { QStringLiteral("kind"), kindOf(resource) },
        { QStringLiteral("revision"), static_cast<qint64>(trackedRevision(resource)) },
        { QStringLiteral("text_length"), text ? text->GetText().size() : 0 }
    };
}

QStringList SigilBookWorkspace::extractFontFamilies(const QString &css) const
{
    QStringList families;
    QRegularExpression face(QStringLiteral("font-family\\s*:\\s*([^;}{]+)"));
    auto it = face.globalMatch(css);
    while (it.hasNext()) {
        const QStringList parts = it.next().captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString part : parts) {
            part = part.trimmed();
            if (part.startsWith(QLatin1Char('"')) || part.startsWith(QLatin1Char('\''))) part = part.mid(1);
            if (part.endsWith(QLatin1Char('"')) || part.endsWith(QLatin1Char('\''))) part.chop(1);
            part = part.trimmed();
            if (!part.isEmpty() && !families.contains(part)) families.append(part);
        }
    }
    return families;
}

quint64 SigilBookWorkspace::revision() const
{
    return m_revision;
}

QJsonObject SigilBookWorkspace::summary() const
{
    return invokeJson([this]() {
        int xhtml = 0, css = 0, fonts = 0, images = 0;
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            const QString kind = kindOf(resource);
            if (kind == QLatin1String("xhtml")) ++xhtml;
            else if (kind == QLatin1String("css")) ++css;
            else if (kind == QLatin1String("font")) ++fonts;
            else if (kind == QLatin1String("image")) ++images;
        }
        QString title;
        QString language;
        for (const MetaEntry &entry : m_book->GetMetadata()) {
            if (entry.m_name.endsWith(QLatin1String("title")) && title.isEmpty()) title = entry.m_content;
            if (entry.m_name.endsWith(QLatin1String("language")) && language.isEmpty()) language = entry.m_content;
        }
        return QJsonObject {
            { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) },
            { QStringLiteral("epub_version"), m_book->GetConstOPF() ? m_book->GetConstOPF()->GetEpubVersion() : QString() },
            { QStringLiteral("title"), title },
            { QStringLiteral("language"), language },
            { QStringLiteral("spine_count"), spine().size() },
            { QStringLiteral("toc_count"), toc().size() },
            { QStringLiteral("resources"), QJsonObject {
                { QStringLiteral("xhtml"), xhtml },
                { QStringLiteral("css"), css },
                { QStringLiteral("fonts"), fonts },
                { QStringLiteral("images"), images },
                { QStringLiteral("total"), m_book->GetFolderKeeper()->GetResourceList().size() }
            } }
        };
    });
}

QJsonArray SigilBookWorkspace::resources() const
{
    return invokeArray([this]() {
        QJsonArray array;
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            array.append(resourceJson(resource));
        }
        return array;
    });
}

QJsonArray SigilBookWorkspace::spine() const
{
    return invokeArray([this]() {
        QJsonArray array;
        const QStringList paths = m_book->GetOPF()->GetSpineOrderBookPaths();
        int index = 0;
        for (const QString &path : paths) {
            Resource *resource = m_book->GetFolderKeeper()->GetResourceByBookPathNoThrow(path);
            QJsonObject item {
                { QStringLiteral("index"), index++ },
                { QStringLiteral("book_path"), path }
            };
            if (resource) {
                item.insert(QStringLiteral("resource_id"), resource->GetIdentifier());
                item.insert(QStringLiteral("media_type"), resource->GetMediaType());
            }
            array.append(item);
        }
        return array;
    });
}

QJsonArray SigilBookWorkspace::toc() const
{
    return invokeArray([this]() {
        QJsonArray array;
        HTMLResource *nav = m_book->GetOPF()->GetNavResource();
        if (nav) {
            QXmlStreamReader reader(nav->GetText());
            QString href;
            QString label;
            bool in_a = false;
            while (!reader.atEnd()) {
                reader.readNext();
                if (reader.isStartElement() && reader.name() == QLatin1String("a")) {
                    href = reader.attributes().value(QStringLiteral("href")).toString();
                    label.clear();
                    in_a = true;
                } else if (reader.isCharacters() && in_a) {
                    label += reader.text();
                } else if (reader.isEndElement() && reader.name() == QLatin1String("a") && in_a) {
                    array.append(QJsonObject {
                        { QStringLiteral("label"), label.trimmed() },
                        { QStringLiteral("href"), href }
                    });
                    in_a = false;
                }
            }
            if (!array.isEmpty()) return array;
        }
        NCXResource *ncx = m_book->GetNCX();
        if (ncx) {
            const NcxNavigation parsed = NcxNavigation::parse(ncx->GetText());
            for (const NcxNavPoint &point : parsed.toc) {
                array.append(QJsonObject {
                    { QStringLiteral("label"), point.label },
                    { QStringLiteral("href"), point.src },
                    { QStringLiteral("level"), point.level }
                });
            }
        }
        return array;
    });
}

QJsonObject SigilBookWorkspace::metadata() const
{
    return invokeJson([this]() {
        QJsonObject object;
        QJsonArray entries;
        for (const MetaEntry &entry : m_book->GetMetadata()) {
            entries.append(QJsonObject {
                { QStringLiteral("name"), entry.m_name },
                { QStringLiteral("content"), entry.m_content }
            });
            if (entry.m_name.endsWith(QLatin1String("title")) && !object.contains(QStringLiteral("title"))) {
                object.insert(QStringLiteral("title"), entry.m_content);
            }
            if (entry.m_name.endsWith(QLatin1String("language")) && !object.contains(QStringLiteral("language"))) {
                object.insert(QStringLiteral("language"), entry.m_content);
            }
        }
        if (m_hasStagedMetadata) {
            for (auto it = m_stagedMetadata.begin(); it != m_stagedMetadata.end(); ++it) {
                object.insert(it.key(), it.value());
            }
        }
        object.insert(QStringLiteral("entries"), entries);
        object.insert(QStringLiteral("book_revision"), static_cast<qint64>(m_revision));
        return object;
    });
}

QJsonArray SigilBookWorkspace::search(const QString &query, int max_matches) const
{
    return invokeArray([this, query, max_matches]() {
        QJsonArray matches;
        if (query.isEmpty()) return matches;
        const int limit = qMin(qMax(max_matches, 1), 50);
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            TextResource *text = qobject_cast<TextResource *>(resource);
            if (!text) continue;
            const QString haystack = currentText(text);
            int from = 0;
            while (matches.size() < limit) {
                const int found = haystack.indexOf(query, from, Qt::CaseInsensitive);
                if (found < 0) break;
                const int start = qMax(0, found - 24);
                matches.append(QJsonObject {
                    { QStringLiteral("resource_id"), resource->GetIdentifier() },
                    { QStringLiteral("book_path"), resource->GetRelativePath() },
                    { QStringLiteral("offset"), found },
                    { QStringLiteral("snippet"), haystack.mid(start, qMin(haystack.size() - start, query.size() + 48)) }
                });
                from = found + qMax(1, query.size());
            }
            if (matches.size() >= limit) break;
        }
        return matches;
    });
}

BookOpResult SigilBookWorkspace::readFragment(const QString &resource_id, int offset, int limit) const
{
    return invokeOp([this, resource_id, offset, limit]() {
        Resource *resource = findResource(resource_id);
        if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        if (kindOf(resource) == QLatin1String("font") || kindOf(resource) == QLatin1String("image")) {
            return BookOpResult::error(QStringLiteral("BINARY_NOT_IN_CONTEXT"),
                                       QStringLiteral("Font and image binaries are never returned to the model"));
        }
        TextResource *text = qobject_cast<TextResource *>(resource);
        if (!text) {
            return BookOpResult::error(QStringLiteral("NOT_TEXT"), QStringLiteral("Resource is not text"));
        }
        const QString all = currentText(text);
        const int start = qMax(0, offset);
        int count = limit <= 0 ? 2048 : qMin(limit, kMaxFragment);
        const QString fragment = all.mid(start, count);
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("book_path"), resource->GetRelativePath() },
            { QStringLiteral("offset"), start },
            { QStringLiteral("length"), fragment.size() },
            { QStringLiteral("total"), all.size() },
            { QStringLiteral("truncated"), start + fragment.size() < all.size() },
            { QStringLiteral("hash"), sha256Text(all) },
            { QStringLiteral("revision"), static_cast<qint64>(trackedRevision(resource)) },
            { QStringLiteral("text"), fragment }
        });
    });
}

QJsonArray SigilBookWorkspace::stylesheets() const
{
    return invokeArray([this]() {
        QJsonArray array;
        for (CSSResource *css : m_book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true)) {
            const QString text = currentText(css);
            array.append(QJsonObject {
                { QStringLiteral("resource_id"), css->GetIdentifier() },
                { QStringLiteral("book_path"), css->GetRelativePath() },
                { QStringLiteral("revision"), static_cast<qint64>(trackedRevision(css)) },
                { QStringLiteral("text"), text.left(kMaxFragment) },
                { QStringLiteral("truncated"), text.size() > kMaxFragment }
            });
        }
        return array;
    });
}

QJsonObject SigilBookWorkspace::fontInventory() const
{
    return invokeJson([this]() {
        QJsonArray fonts;
        QJsonArray css_families;
        QSet<QString> declared;
        for (FontResource *font : m_book->GetFolderKeeper()->GetResourceTypeList<FontResource>()) {
            fonts.append(QJsonObject {
                { QStringLiteral("resource_id"), font->GetIdentifier() },
                { QStringLiteral("book_path"), font->GetRelativePath() },
                { QStringLiteral("media_type"), font->GetMediaType() }
            });
        }
        for (CSSResource *css : m_book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true)) {
            for (const QString &family : extractFontFamilies(currentText(css))) {
                declared.insert(family);
                css_families.append(QJsonObject {
                    { QStringLiteral("resource_id"), css->GetIdentifier() },
                    { QStringLiteral("family"), family }
                });
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
    });
}

QJsonObject SigilBookWorkspace::validate() const
{
    return invokeJson([this]() {
        QJsonArray issues;
        if (m_book->GetOPF()->GetSpineOrderBookPaths().isEmpty()) {
            issues.append(QJsonObject {
                { QStringLiteral("severity"), QStringLiteral("error") },
                { QStringLiteral("code"), QStringLiteral("EMPTY_SPINE") },
                { QStringLiteral("message"), QStringLiteral("Spine has no items") }
            });
        }
        for (HTMLResource *html : m_book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true)) {
            if (!currentText(html).contains(QLatin1String("<body"))) {
                issues.append(QJsonObject {
                    { QStringLiteral("severity"), QStringLiteral("error") },
                    { QStringLiteral("code"), QStringLiteral("MISSING_BODY") },
                    { QStringLiteral("resource_id"), html->GetIdentifier() }
                });
            }
        }
        return QJsonObject {
            { QStringLiteral("ok"), issues.isEmpty() },
            { QStringLiteral("issue_count"), issues.size() },
            { QStringLiteral("issues"), issues }
        };
    });
}

bool SigilBookWorkspace::hasOpenTransaction() const
{
    return m_transaction != nullptr;
}

BookOpResult SigilBookWorkspace::ensureTransaction()
{
    if (m_transaction) {
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("transaction_id"), m_transaction->Id() }
        });
    }
    return beginTransaction(QStringLiteral("auto"));
}

BookOpResult SigilBookWorkspace::beginTransaction(const QString &label)
{
    return invokeOp([this, label]() {
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
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("transaction_id"), m_transaction->Id() },
            { QStringLiteral("base_book_revision"), static_cast<qint64>(m_revision) }
        });
    });
}

BookOpResult SigilBookWorkspace::previewTransaction() const
{
    if (!m_transaction) {
        return BookOpResult::error(QStringLiteral("NO_TRANSACTION"), QStringLiteral("No open transaction"));
    }
    QJsonArray changes;
    for (const PluginApi::StagedTextChange &change : m_transaction->Changes()) {
        changes.append(QJsonObject {
            { QStringLiteral("resource_id"), change.resourceId },
            { QStringLiteral("original_length"), change.originalText.size() },
            { QStringLiteral("staged_length"), change.stagedText.size() },
            { QStringLiteral("changed"), change.originalText != change.stagedText }
        });
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), m_transaction->Id() },
        { QStringLiteral("live_book_revision"), static_cast<qint64>(m_revision) },
        { QStringLiteral("changes"), changes },
        { QStringLiteral("metadata_changed"), m_hasStagedMetadata }
    }, false, true);
}

BookOpResult SigilBookWorkspace::commitTransaction(quint64 expected_revision)
{
    return invokeOp([this, expected_revision]() {
        if (!m_transaction) {
            return BookOpResult::error(QStringLiteral("NO_TRANSACTION"), QStringLiteral("No open transaction"));
        }
        if (expected_revision != m_revision) {
            return BookOpResult::error(QStringLiteral("BOOK_REVISION_CONFLICT"),
                                       QStringLiteral("expected %1 actual %2")
                                           .arg(expected_revision).arg(m_revision));
        }
        QHash<QString, QString> originals;
        int applied = 0;
        QString fail;
        for (const PluginApi::StagedTextChange &change : m_transaction->Changes()) {
            TextResource *resource = textResource(change.resourceId);
            if (!resource) {
                fail = QStringLiteral("Missing resource");
                break;
            }
            originals.insert(resource->GetIdentifier(), resource->GetText());
            resource->SetTextAsUndoableEdit(change.stagedText);
            noteText(resource, change.stagedText);
            ++applied;
        }
        if (!fail.isEmpty()) {
            for (auto it = originals.constBegin(); it != originals.constEnd(); ++it) {
                if (TextResource *resource = textResource(it.key())) {
                    resource->SetText(it.value());
                }
            }
            return BookOpResult::error(QStringLiteral("TRANSACTION_ROLLED_BACK"), fail);
        }
        if (m_hasStagedMetadata) {
            QList<MetaEntry> entries = m_book->GetMetadata();
            auto upsert = [&entries](const QString &suffix, const QString &value) {
                for (MetaEntry &entry : entries) {
                    if (entry.m_name.endsWith(suffix)) {
                        entry.m_content = value;
                        return;
                    }
                }
                MetaEntry entry;
                entry.m_name = QStringLiteral("dc:") + suffix;
                entry.m_content = value;
                entries.append(entry);
            };
            if (m_stagedMetadata.contains(QStringLiteral("title"))) {
                upsert(QStringLiteral("title"), m_stagedMetadata.value(QStringLiteral("title")).toString());
            }
            if (m_stagedMetadata.contains(QStringLiteral("language"))) {
                upsert(QStringLiteral("language"), m_stagedMetadata.value(QStringLiteral("language")).toString());
            }
            m_book->SetMetadata(entries);
        }
        const QString txid = m_transaction->Id();
        m_transaction.reset();
        m_hasStagedMetadata = false;
        ++m_revision;
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("transaction_id"), txid },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) },
            { QStringLiteral("applied_changes"), applied }
        }, true);
    });
}

BookOpResult SigilBookWorkspace::rollbackTransaction()
{
    if (!m_transaction) {
        return BookOpResult::success(QJsonObject { { QStringLiteral("rolled_back"), false } });
    }
    const QString txid = m_transaction->Id();
    m_transaction->Clear();
    m_transaction.reset();
    m_hasStagedMetadata = false;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), txid },
        { QStringLiteral("rolled_back"), true }
    });
}

BookOpResult SigilBookWorkspace::patchFragment(const QString &resource_id,
                                               int start,
                                               int end,
                                               const QString &text,
                                               quint64 expected_resource_revision)
{
    return invokeOp([this, resource_id, start, end, text, expected_resource_revision]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        TextResource *resource = textResource(resource_id);
        if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        QString error;
        QJsonArray edits {
            QJsonObject {
                { QStringLiteral("start"), start },
                { QStringLiteral("end"), end },
                { QStringLiteral("text"), text }
            }
        };
        if (!m_transaction->ApplyEdits(resource->GetIdentifier(), resource->GetText(),
                                       trackedRevision(resource), expected_resource_revision,
                                       edits, &error)) {
            const QString code = error.contains(QLatin1String("Revision"))
                ? QStringLiteral("BOOK_REVISION_CONFLICT") : QStringLiteral("PATCH_FAILED");
            return BookOpResult::error(code, error);
        }
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("staged"), true },
            { QStringLiteral("live_unchanged"), true }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::updateCss(const QString &resource_id,
                                           const QString &text,
                                           quint64 expected_resource_revision)
{
    return invokeOp([this, resource_id, text, expected_resource_revision]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        TextResource *resource = textResource(resource_id);
        if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        QString error;
        if (!m_transaction->ReplaceText(resource->GetIdentifier(), resource->GetText(),
                                        trackedRevision(resource), expected_resource_revision,
                                        text, &error)) {
            return BookOpResult::error(QStringLiteral("BOOK_REVISION_CONFLICT"), error);
        }
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("staged"), true }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::updateMetadata(const QJsonObject &patch)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    QJsonObject next = m_hasStagedMetadata ? m_stagedMetadata : QJsonObject();
    for (auto it = patch.begin(); it != patch.end(); ++it) next.insert(it.key(), it.value());
    m_stagedMetadata = next;
    m_hasStagedMetadata = true;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("metadata"), next }
    }, false, true);
}

BookOpResult SigilBookWorkspace::createCheckpoint(const QString &label)
{
    return invokeOp([this, label]() {
        Checkpoint checkpoint;
        checkpoint.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        checkpoint.label = label.isEmpty() ? QStringLiteral("agent") : label;
        checkpoint.bookRevision = m_revision;
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            if (TextResource *text = qobject_cast<TextResource *>(resource)) {
                checkpoint.texts.insert(resource->GetIdentifier(), text->GetText());
            }
        }
        m_checkpoints.append(checkpoint);
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("checkpoint_id"), checkpoint.id },
            { QStringLiteral("label"), checkpoint.label },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
        });
    });
}

QJsonArray SigilBookWorkspace::listCheckpoints() const
{
    QJsonArray array;
    for (const Checkpoint &checkpoint : m_checkpoints) {
        array.append(QJsonObject {
            { QStringLiteral("checkpoint_id"), checkpoint.id },
            { QStringLiteral("label"), checkpoint.label },
            { QStringLiteral("book_revision"), static_cast<qint64>(checkpoint.bookRevision) }
        });
    }
    return array;
}

BookOpResult SigilBookWorkspace::restoreCheckpoint(const QString &checkpoint_id)
{
    return invokeOp([this, checkpoint_id]() {
        for (const Checkpoint &checkpoint : m_checkpoints) {
            if (checkpoint.id != checkpoint_id) continue;
            for (auto it = checkpoint.texts.constBegin(); it != checkpoint.texts.constEnd(); ++it) {
                if (TextResource *resource = textResource(it.key())) {
                    resource->SetTextAsUndoableEdit(it.value());
                    noteText(resource, it.value());
                }
            }
            ++m_revision;
            return BookOpResult::success(QJsonObject {
                { QStringLiteral("checkpoint_id"), checkpoint.id },
                { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
            }, true);
        }
        return BookOpResult::error(QStringLiteral("CHECKPOINT_NOT_FOUND"),
                                   QStringLiteral("Unknown checkpoint"));
    });
}

QString SigilBookWorkspace::resourceText(const QString &resource_id) const
{
    return invokeString([this, resource_id]() {
        TextResource *resource = textResource(resource_id);
        return resource ? resource->GetText() : QString();
    });
}

quint64 SigilBookWorkspace::resourceRevision(const QString &resource_id) const
{
    Resource *resource = findResource(resource_id);
    return trackedRevision(resource);
}

} // namespace SigilAgent
