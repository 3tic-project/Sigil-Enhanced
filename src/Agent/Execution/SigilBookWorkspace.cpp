/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/SigilBookWorkspace.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>
#include <QXmlStreamReader>

#include "Agent/Execution/ContentOps.h"
#include "Agent/Execution/PatchRange.h"
#include "Agent/Execution/ResourceMutations.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/NcxNavigation.h"
#include "PluginAPI/PluginSessionManager.h"
#include "SourceUpdates/UniversalUpdates.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "Misc/Utility.h"

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
    m_stagedMetadataRemove.clear();
    m_stagedRemovals.clear();
    m_hasStagedSpine = false;
    m_stagedSpine.clear();
    m_hasStagedToc = false;
    m_stagedToc = QJsonArray();
    m_transactionPackageResourceId.clear();
    m_transactionPackageSource.clear();
    m_transactionTocResourceId.clear();
    m_transactionTocSource.clear();
    m_revision = 1;
}

void SigilBookWorkspace::setPluginSessionManager(PluginSessionManager *manager)
{
    m_pluginSessions = manager;
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
        case Resource::MiscTextResourceType: return QStringLiteral("text");
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
    if (tracked.initialized && text && tracked.lastText != live) {
        tracked.revision += 1;
    }
    if (text) {
        tracked.lastText = live;
        tracked.initialized = true;
    }
    if (tracked.revision == 0) tracked.revision = 1;
    return tracked.revision;
}

void SigilBookWorkspace::noteText(Resource *resource, const QString &text) const
{
    if (!resource) return;
    TrackedResource &tracked = m_tracked[resource->GetIdentifier()];
    tracked.lastText = text;
    tracked.initialized = true;
    tracked.revision += 1;
}

QJsonObject SigilBookWorkspace::resourceJson(Resource *resource) const
{
    if (!resource) return QJsonObject();
    TextResource *text = qobject_cast<TextResource *>(resource);
    QString path = resource->GetRelativePath();
    if (m_transaction) {
        for (const PluginApi::StagedResourceRelocation &reloc : m_transaction->Relocations()) {
            if (reloc.resourceId == resource->GetIdentifier()) path = reloc.targetBookPath;
        }
    }
    return QJsonObject {
        { QStringLiteral("resource_id"), resource->GetIdentifier() },
        { QStringLiteral("book_path"), path },
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
        int xhtml = 0, css = 0, fonts = 0, images = 0, text = 0;
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            const QString kind = kindOf(resource);
            if (kind == QLatin1String("xhtml")) ++xhtml;
            else if (kind == QLatin1String("css")) ++css;
            else if (kind == QLatin1String("font")) ++fonts;
            else if (kind == QLatin1String("image")) ++images;
            else if (kind == QLatin1String("text")) ++text;
        }
        QString title;
        QString language;
        if (m_book->GetConstOPF()) {
            for (const MetaEntry &entry : m_book->GetMetadata()) {
                if (entry.m_name.endsWith(QLatin1String("title")) && title.isEmpty()) title = entry.m_content;
                if (entry.m_name.endsWith(QLatin1String("language")) && language.isEmpty()) language = entry.m_content;
            }
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
                { QStringLiteral("text"), text },
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
        QString staged_added;
        quint64 staged_rev = 0;
        QString staged_path;
        if (!resource && m_transaction
            && m_transaction->ReadAddedText(resource_id, &staged_added, &staged_rev)) {
            for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
                if (addition.stagingId == resource_id) staged_path = addition.bookPath;
            }
        } else if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        if (resource && (kindOf(resource) == QLatin1String("font") || kindOf(resource) == QLatin1String("image"))) {
            return BookOpResult::error(QStringLiteral("BINARY_NOT_IN_CONTEXT"),
                                       QStringLiteral("Font and image binaries are never returned to the model"));
        }
        TextResource *text = qobject_cast<TextResource *>(resource);
        if (resource && !text) {
            return BookOpResult::error(QStringLiteral("NOT_TEXT"), QStringLiteral("Resource is not text"));
        }
        const QString all = text ? currentText(text) : staged_added;
        const int start = qMax(0, offset);
        int count = limit <= 0 ? 2048 : qMin(limit, kMaxFragment);
        const QString fragment = all.mid(start, count);
        QJsonObject data {
            { QStringLiteral("resource_id"), resource ? resource->GetIdentifier() : resource_id },
            { QStringLiteral("book_path"), resource ? resource->GetRelativePath() : staged_path },
            { QStringLiteral("offset"), start },
            { QStringLiteral("end"), start + fragment.size() },
            { QStringLiteral("length"), fragment.size() },
            { QStringLiteral("total"), all.size() },
            { QStringLiteral("truncated"), start + fragment.size() < all.size() },
            { QStringLiteral("hash"), sha256Text(all) },
            { QStringLiteral("revision"), static_cast<qint64>(resource ? trackedRevision(resource) : staged_rev) },
            { QStringLiteral("text"), fragment }
        };
        addFragmentLineMetadata(&data, all, start, fragment.size());
        return BookOpResult::success(data);
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
        QStringList image_names;
        for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
            if (kindOf(resource) == QLatin1String("image")) image_names.append(resource->GetRelativePath());
        }
        for (HTMLResource *html : m_book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true)) {
            if (!currentText(html).contains(QLatin1String("<body"))) {
                issues.append(QJsonObject {
                    { QStringLiteral("severity"), QStringLiteral("error") },
                    { QStringLiteral("code"), QStringLiteral("MISSING_BODY") },
                    { QStringLiteral("resource_id"), html->GetIdentifier() }
                });
            }
            const QJsonArray missing = brokenImageRefs(currentText(html), image_names);
            for (const QJsonValue &value : missing) {
                QJsonObject issue = value.toObject();
                issue.insert(QStringLiteral("severity"), QStringLiteral("warning"));
                issue.insert(QStringLiteral("code"), QStringLiteral("BROKEN_IMAGE"));
                issue.insert(QStringLiteral("resource_id"), html->GetIdentifier());
                issues.append(issue);
            }
        }
        bool has_error = false;
        for (const QJsonValue &value : issues) {
            if (value.toObject().value(QStringLiteral("severity")).toString() == QLatin1String("error")) {
                has_error = true;
            }
        }
        return QJsonObject {
            { QStringLiteral("ok"), !has_error },
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
        m_stagedMetadataRemove.clear();
        m_stagedRemovals.clear();
        m_hasStagedSpine = false;
        m_stagedSpine.clear();
        m_hasStagedToc = false;
        m_stagedToc = QJsonArray();
        m_stagedAfterIds.clear();
        OPFResource *opf = m_book->GetOPF();
        m_transactionPackageResourceId = opf ? opf->GetIdentifier() : QString();
        m_transactionPackageSource = opf ? opf->GetSourceText() : QString();
        NCXResource *ncx = m_book->GetNCX();
        m_transactionTocResourceId = ncx ? ncx->GetIdentifier() : QString();
        m_transactionTocSource = ncx ? ncx->GetText() : QString();
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
    for (const PluginApi::StagedResourceRelocation &reloc : m_transaction->Relocations()) {
        changes.append(QJsonObject {
            { QStringLiteral("resource_id"), reloc.resourceId },
            { QStringLiteral("from"), reloc.originalBookPath },
            { QStringLiteral("book_path"), reloc.targetBookPath },
            { QStringLiteral("renamed"), true }
        });
    }
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), m_transaction->Id() },
        { QStringLiteral("live_book_revision"), static_cast<qint64>(m_revision) },
        { QStringLiteral("changes"), changes },
        { QStringLiteral("metadata_changed"), m_hasStagedMetadata },
        { QStringLiteral("removed"), QJsonArray::fromStringList(m_stagedRemovals) },
        { QStringLiteral("spine_changed"), m_hasStagedSpine },
        { QStringLiteral("toc_changed"), m_hasStagedToc }
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
        OPFResource *live_opf = m_book->GetOPF();
        if (!live_opf || live_opf->GetIdentifier() != m_transactionPackageResourceId
            || live_opf->GetSourceText() != m_transactionPackageSource) {
            return BookOpResult::error(
                QStringLiteral("BOOK_REVISION_CONFLICT"),
                QStringLiteral("The package source changed after the transaction began"),
                QJsonObject {
                    { QStringLiteral("resource_id"), live_opf ? live_opf->GetIdentifier() : QString() },
                    { QStringLiteral("reason"), QStringLiteral("package_source_changed") }
                });
        }
        for (const PluginApi::StagedTextChange &change : m_transaction->Changes()) {
            TextResource *resource = textResource(change.resourceId);
            if (!resource || resource->GetText() != change.originalText) {
                return BookOpResult::error(
                    QStringLiteral("BOOK_REVISION_CONFLICT"),
                    QStringLiteral("A staged text resource changed after it was read"),
                    QJsonObject {
                        { QStringLiteral("resource_id"), change.resourceId },
                        { QStringLiteral("reason"), QStringLiteral("resource_source_changed") }
                    });
            }
        }
        for (const PluginApi::StagedResourceRemoval &removal : m_transaction->Removals()) {
            Resource *resource = findResource(removal.resourceId);
            if (!resource || trackedRevision(resource) != removal.baseRevision) {
                return BookOpResult::error(
                    QStringLiteral("BOOK_REVISION_CONFLICT"),
                    QStringLiteral("A resource staged for removal changed"),
                    QJsonObject {
                        { QStringLiteral("resource_id"), removal.resourceId },
                        { QStringLiteral("reason"), QStringLiteral("removed_resource_changed") }
                    });
            }
        }
        for (const PluginApi::StagedResourceRelocation &reloc : m_transaction->Relocations()) {
            Resource *resource = findResource(reloc.resourceId);
            if (!resource || resource->GetRelativePath() != reloc.originalBookPath
                || trackedRevision(resource) != reloc.baseRevision) {
                return BookOpResult::error(
                    QStringLiteral("BOOK_REVISION_CONFLICT"),
                    QStringLiteral("A resource staged for relocation changed"),
                    QJsonObject {
                        { QStringLiteral("resource_id"), reloc.resourceId },
                        { QStringLiteral("reason"), QStringLiteral("relocated_resource_changed") }
                    });
            }
        }
        if (m_hasStagedToc) {
            NCXResource *live_ncx = m_book->GetNCX();
            if (!live_ncx || live_ncx->GetIdentifier() != m_transactionTocResourceId
                || live_ncx->GetText() != m_transactionTocSource) {
                return BookOpResult::error(
                    QStringLiteral("BOOK_REVISION_CONFLICT"),
                    QStringLiteral("The table of contents source changed after the transaction began"),
                    QJsonObject {
                        { QStringLiteral("resource_id"), live_ncx ? live_ncx->GetIdentifier() : QString() },
                        { QStringLiteral("reason"), QStringLiteral("toc_source_changed") }
                    });
            }
        }
        QHash<QString, QString> originals;
        int applied = 0;
        QString fail;
        for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
            const QString kind = kindFromPathOrType(addition.bookPath, QString());
            const QString text = addition.isText ? QString::fromUtf8(addition.data) : QString();
            const QString folder = Utility::startingDir(addition.bookPath);
            Resource *resource = nullptr;
            try {
                if (kind == QLatin1String("xhtml") && addition.bookPath.isEmpty()) {
                    HTMLResource *html = m_book->CreateEmptyHTMLFile(folder);
                    if (html && !text.isEmpty()) html->SetTextAsUndoableEdit(text);
                    resource = html;
                } else if (kind == QLatin1String("css") && addition.bookPath.isEmpty()) {
                    CSSResource *css = m_book->CreateEmptyCSSFile(folder);
                    if (css && !text.isEmpty()) css->SetText(text);
                    resource = css;
                } else {
                    QTemporaryFile staged_file;
                    if (!staged_file.open()
                        || staged_file.write(addition.data) != addition.data.size()
                        || !staged_file.flush()) {
                        fail = QStringLiteral("Could not materialize added resource");
                        break;
                    }
                    staged_file.close();
                    resource = m_book->GetFolderKeeper()->AddContentFileToFolder(
                        staged_file.fileName(), true, addition.mediaType, addition.bookPath);
                    if (auto *text_resource = qobject_cast<TextResource *>(resource)) {
                        text_resource->InitialLoad();
                        if (!text.isEmpty()) text_resource->SetTextAsUndoableEdit(text);
                    }
                }
            } catch (...) {
                fail = QStringLiteral("Could not add resource %1").arg(addition.bookPath);
                break;
            }
            if (!resource) {
                fail = QStringLiteral("Could not add resource %1").arg(addition.bookPath);
                break;
            }
            if (addition.addToSpine) {
                if (auto *html = qobject_cast<HTMLResource *>(resource)) {
                    const QString after_id = m_stagedAfterIds.value(addition.stagingId);
                    if (auto *after = qobject_cast<HTMLResource *>(findResource(after_id))) {
                        m_book->MoveResourceAfter(html, after);
                    }
                }
            }
            ++applied;
        }
        if (!fail.isEmpty()) {
            m_transaction.reset();
            m_hasStagedMetadata = false;
            m_stagedAfterIds.clear();
            return BookOpResult::error(QStringLiteral("TRANSACTION_ROLLED_BACK"), fail);
        }
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
            const QStringList fields = {
                QStringLiteral("title"), QStringLiteral("language"), QStringLiteral("creator"),
                QStringLiteral("contributor"), QStringLiteral("publisher"), QStringLiteral("description"),
                QStringLiteral("subject"), QStringLiteral("date"), QStringLiteral("identifier"),
                QStringLiteral("rights"), QStringLiteral("source"), QStringLiteral("coverage"),
                QStringLiteral("type"), QStringLiteral("format"), QStringLiteral("relation")
            };
            for (const QString &field : fields) {
                if (m_stagedMetadata.contains(field) && field != QLatin1String("_remove")) {
                    upsert(field, m_stagedMetadata.value(field).toString());
                }
            }
            for (const QString &key : m_stagedMetadataRemove) {
                for (int i = entries.size() - 1; i >= 0; --i) {
                    if (entries.at(i).m_name.endsWith(key)) entries.removeAt(i);
                }
            }
            m_book->SetMetadata(entries);
        }
        if (m_hasStagedSpine) {
            OPFResource *opf = m_book->GetOPF();
            QStringList wanted;
            QList<HTMLResource *> htmls;
            for (const QString &id : m_stagedSpine) {
                HTMLResource *html = qobject_cast<HTMLResource *>(findResource(id));
                if (!html) continue;
                wanted.append(html->GetIdentifier());
                htmls.append(html);
            }
            const QStringList current = opf->GetSpineOrderBookPaths();
            for (const QString &path : current) {
                Resource *resource = m_book->GetFolderKeeper()->GetResourceByBookPathNoThrow(path);
                if (!resource) continue;
                if (!wanted.contains(resource->GetIdentifier())) {
                    opf->RemoveResourceFromSpine(resource);
                }
            }
            for (HTMLResource *html : htmls) {
                if (opf->GetReadingOrder(html) < 0) opf->AppendResourceToSpine(html, false);
            }
            for (int i = 0; i + 1 < htmls.size(); ++i) {
                m_book->MoveResourceAfter(htmls.at(i + 1), htmls.at(i));
            }
        }
        QList<Resource *> reloc_resources;
        QStringList reloc_targets;
        QHash<QString, QString> path_updates;
        for (const PluginApi::StagedResourceRelocation &reloc : m_transaction->Relocations()) {
            Resource *resource = findResource(reloc.resourceId);
            if (!resource) {
                fail = QStringLiteral("Missing resource %1").arg(reloc.resourceId);
                break;
            }
            reloc_resources.append(resource);
            reloc_targets.append(reloc.targetBookPath);
            path_updates.insert(reloc.originalBookPath, reloc.targetBookPath);
        }
        if (fail.isEmpty() && !reloc_resources.isEmpty()) {
            m_book->GetFolderKeeper()->BulkMoveResources(reloc_resources, reloc_targets, true);
            QList<Resource *> update_resources = m_book->GetFolderKeeper()->GetResourceList();
            update_resources.removeOne(m_book->GetOPF());
            const QStringList update_errors = UniversalUpdates::PerformUniversalUpdates(
                true, update_resources, path_updates);
            if (!update_errors.isEmpty()) fail = update_errors.join(QLatin1Char('\n'));
            else applied += reloc_resources.size();
        }
        if (fail.isEmpty() && m_hasStagedToc) {
            if (NCXResource *ncx = m_book->GetNCX()) {
                const QString title = m_book->GetMetadataValues(QStringLiteral("title")).isEmpty()
                    ? QStringLiteral("Untitled")
                    : m_book->GetMetadataValues(QStringLiteral("title")).first();
                ncx->SetTextAsUndoableEdit(ncxFromEntries(m_stagedToc, title));
            }
        }
        if (fail.isEmpty()) {
            for (const QString &id : m_stagedRemovals) {
                if (Resource *resource = findResource(id)) resource->Delete();
            }
        }
        if (!fail.isEmpty()) {
            m_transaction.reset();
            m_hasStagedMetadata = false;
            m_stagedMetadataRemove.clear();
            m_stagedRemovals.clear();
            m_hasStagedSpine = false;
            m_stagedSpine.clear();
            m_hasStagedToc = false;
            m_stagedToc = QJsonArray();
            m_stagedAfterIds.clear();
            return BookOpResult::error(QStringLiteral("TRANSACTION_ROLLED_BACK"), fail);
        }
        const QString txid = m_transaction->Id();
        m_transaction.reset();
        m_hasStagedMetadata = false;
        m_stagedMetadataRemove.clear();
        m_stagedRemovals.clear();
        m_hasStagedSpine = false;
        m_stagedSpine.clear();
        m_hasStagedToc = false;
        m_stagedToc = QJsonArray();
        m_stagedAfterIds.clear();
        m_transactionPackageResourceId.clear();
        m_transactionPackageSource.clear();
        m_transactionTocResourceId.clear();
        m_transactionTocSource.clear();
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
    m_stagedMetadataRemove.clear();
    m_stagedRemovals.clear();
    m_hasStagedSpine = false;
    m_stagedSpine.clear();
    m_hasStagedToc = false;
    m_stagedToc = QJsonArray();
    m_stagedAfterIds.clear();
    m_transactionPackageResourceId.clear();
    m_transactionPackageSource.clear();
    m_transactionTocResourceId.clear();
    m_transactionTocSource.clear();
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("transaction_id"), txid },
        { QStringLiteral("rolled_back"), true }
    });
}

BookOpResult SigilBookWorkspace::patchFragment(const QString &resource_id,
                                               int start,
                                               int end,
                                               const QString &text,
                                               quint64 expected_resource_revision,
                                               const QString &expected_text,
                                               int start_line)
{
    return invokeOp([this, resource_id, start, end, text, expected_resource_revision, expected_text, start_line]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        TextResource *resource = textResource(resource_id);
        QString added_source;
        quint64 added_revision = 0;
        const bool staged_new = !resource && m_transaction
            && m_transaction->ReadAddedText(resource_id, &added_source, &added_revision);
        if (!resource && !staged_new) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        const QString source = resource ? currentText(resource) : added_source;
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
            : m_transaction->ApplyEdits(resource->GetIdentifier(), resource->GetText(),
                                        trackedRevision(resource), expected_resource_revision,
                                        edits, &error);
        if (!applied_edits) {
            const QString code = error.contains(QLatin1String("Revision"))
                ? QStringLiteral("BOOK_REVISION_CONFLICT") : QStringLiteral("PATCH_FAILED");
            return BookOpResult::error(code, error);
        }
        QJsonObject data = resolved.data;
        data.insert(QStringLiteral("resource_id"), resource ? resource->GetIdentifier() : resource_id);
        data.insert(QStringLiteral("staged"), true);
        data.insert(QStringLiteral("live_unchanged"), true);
        data.insert(QStringLiteral("replacement_length"), text.size());
        return BookOpResult::success(data, false, true);
    });
}

BookOpResult SigilBookWorkspace::replaceText(const QString &resource_id,
                                             const QString &text,
                                             quint64 expected_resource_revision)
{
    return invokeOp([this, resource_id, text, expected_resource_revision]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        TextResource *resource = textResource(resource_id);
        QString added_source;
        quint64 added_revision = 0;
        const bool staged_new = !resource && m_transaction
            && m_transaction->ReadAddedText(resource_id, &added_source, &added_revision);
        if (!resource && !staged_new) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        if (resource && (kindOf(resource) == QLatin1String("font")
                         || kindOf(resource) == QLatin1String("image"))) {
            return BookOpResult::error(QStringLiteral("BINARY_NOT_IN_CONTEXT"),
                                       QStringLiteral("Font and image binaries cannot be replaced as text"));
        }
        QString error;
        const bool replaced = staged_new
            ? m_transaction->ReplaceAddedText(resource_id, expected_resource_revision, text, &error)
            : m_transaction->ReplaceText(resource->GetIdentifier(), resource->GetText(),
                                         trackedRevision(resource), expected_resource_revision,
                                         text, &error);
        if (!replaced) {
            const QString code = error.contains(QLatin1String("Revision"))
                ? QStringLiteral("BOOK_REVISION_CONFLICT") : QStringLiteral("REPLACE_FAILED");
            return BookOpResult::error(code, error);
        }
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), resource ? resource->GetIdentifier() : resource_id },
            { QStringLiteral("staged"), true },
            { QStringLiteral("live_unchanged"), true },
            { QStringLiteral("replacement_length"), text.size() }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::updateCss(const QString &resource_id,
                                           const QString &text,
                                           quint64 expected_resource_revision)
{
    return replaceText(resource_id, text, expected_resource_revision);
}

BookOpResult SigilBookWorkspace::updateMetadata(const QJsonObject &patch)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    QJsonObject next = m_hasStagedMetadata ? m_stagedMetadata : QJsonObject();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == QLatin1String("_remove")) continue;
        next.insert(it.key(), it.value());
    }
    if (patch.contains(QStringLiteral("_remove"))) {
        for (const QJsonValue &value : patch.value(QStringLiteral("_remove")).toArray()) {
            const QString key = value.toString();
            next.remove(key);
            if (!m_stagedMetadataRemove.contains(key)) m_stagedMetadataRemove.append(key);
        }
    }
    m_stagedMetadata = next;
    m_hasStagedMetadata = true;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("metadata"), next }
    }, false, true);
}

QStringList SigilBookWorkspace::allBookPaths() const
{
    QStringList paths;
    if (!m_book) return paths;
    QHash<QString, QString> relocated;
    if (m_transaction) {
        for (const PluginApi::StagedResourceRelocation &reloc : m_transaction->Relocations()) {
            relocated.insert(reloc.resourceId, reloc.targetBookPath);
        }
    }
    for (Resource *resource : m_book->GetFolderKeeper()->GetResourceList()) {
        if (!resource) continue;
        paths.append(relocated.value(resource->GetIdentifier(), resource->GetRelativePath()));
    }
    if (m_transaction) {
        for (const PluginApi::StagedResourceAddition &addition : m_transaction->Additions()) {
            paths.append(addition.bookPath);
        }
    }
    return paths;
}

BookOpResult SigilBookWorkspace::stageAddition(const QString &book_path,
                                               const QString &kind,
                                               const QString &text,
                                               bool add_to_spine,
                                               const QString &after_resource_id)
{
    return invokeOp([this, book_path, kind, text, add_to_spine, after_resource_id]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        const QString resolved_kind = kindFromPathOrType(book_path, kind);
        if (!kindIsCreatable(resolved_kind)) {
            return BookOpResult::error(QStringLiteral("UNSUPPORTED_KIND"),
                                       QStringLiteral("Only xhtml, css, svg, js, and text resources can be created"));
        }
        QString path = book_path.trimmed();
        if (path.isEmpty()) {
            return BookOpResult::error(QStringLiteral("BOOK_PATH_REQUIRED"),
                                       QStringLiteral("book_path is required"));
        }
        if (bookPathTaken(path, allBookPaths())) {
            return BookOpResult::error(QStringLiteral("BOOK_PATH_EXISTS"),
                                       QStringLiteral("A resource already uses %1").arg(path));
        }
        PluginApi::StagedResourceAddition addition;
        addition.stagingId = QStringLiteral("new:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        addition.bookPath = path;
        addition.mediaType = mediaTypeForKind(resolved_kind);
        addition.manifestId = QFileInfo(path).completeBaseName();
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
    });
}

BookOpResult SigilBookWorkspace::createResource(const QString &book_path,
                                                const QString &kind,
                                                const QString &text,
                                                bool add_to_spine,
                                                const QString &after_resource_id)
{
    const QString resolved_kind = kindFromPathOrType(book_path, kind);
    QString body = text;
    if (body.isEmpty() && resolved_kind == QLatin1String("xhtml")) body = defaultXhtmlTemplate();
    if (body.isEmpty() && resolved_kind == QLatin1String("svg")) body = defaultSvgTemplate();
    return stageAddition(book_path, resolved_kind, body, add_to_spine, after_resource_id);
}

BookOpResult SigilBookWorkspace::copyResource(const QString &source_id,
                                              const QString &book_path,
                                              bool add_to_spine)
{
    return invokeOp([this, source_id, book_path, add_to_spine]() {
        TextResource *source = textResource(source_id);
        QString staged_text;
        quint64 staged_rev = 0;
        QString source_path;
        QString source_kind;
        QString after_id = source_id;
        if (source) {
            source_path = source->GetRelativePath();
            source_kind = kindOf(source);
            staged_text = currentText(source);
            after_id = source->GetIdentifier();
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
        return stageAddition(target, source_kind, staged_text, add_to_spine, after_id);
    });
}

BookOpResult SigilBookWorkspace::deleteResource(const QString &resource_id)
{
    return invokeOp([this, resource_id]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        Resource *resource = findResource(resource_id);
        if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        const QString kind = kindOf(resource);
        if (kind == QLatin1String("opf") || kind == QLatin1String("ncx")) {
            return BookOpResult::error(QStringLiteral("PROTECTED_RESOURCE"),
                                       QStringLiteral("OPF and NCX cannot be deleted"));
        }
        if (resource == m_book->GetOPF()->GetNavResource()) {
            return BookOpResult::error(QStringLiteral("PROTECTED_RESOURCE"),
                                       QStringLiteral("The Nav document cannot be deleted"));
        }
        if (kind == QLatin1String("xhtml")) {
            int xhtml = 0;
            for (HTMLResource *html : m_book->GetFolderKeeper()->GetResourceTypeList<HTMLResource>(true)) {
                if (!m_stagedRemovals.contains(html->GetIdentifier())) ++xhtml;
            }
            if (xhtml <= 1) {
                return BookOpResult::error(QStringLiteral("LAST_XHTML"),
                                           QStringLiteral("Cannot delete the last XHTML resource"));
            }
        }
        const QString id = resource->GetIdentifier();
        QString error;
        if (!m_transaction->RemoveResource(id, trackedRevision(resource), &error)) {
            return BookOpResult::error(QStringLiteral("REMOVE_RESOURCE_FAILED"), error);
        }
        if (!m_stagedRemovals.contains(id)) m_stagedRemovals.append(id);
        if (m_hasStagedSpine) m_stagedSpine.removeAll(id);
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), id },
            { QStringLiteral("book_path"), resource->GetRelativePath() },
            { QStringLiteral("staged"), true },
            { QStringLiteral("removed"), true }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::renameResource(const QString &resource_id, const QString &book_path)
{
    return invokeOp([this, resource_id, book_path]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        Resource *resource = findResource(resource_id);
        if (!resource) {
            return BookOpResult::error(QStringLiteral("RESOURCE_NOT_FOUND"), QStringLiteral("Unknown resource"));
        }
        const QString kind = kindOf(resource);
        if (kind == QLatin1String("opf") || kind == QLatin1String("ncx")) {
            return BookOpResult::error(QStringLiteral("PROTECTED_RESOURCE"),
                                       QStringLiteral("OPF and NCX cannot be renamed"));
        }
        if (resource == m_book->GetOPF()->GetNavResource()) {
            return BookOpResult::error(QStringLiteral("PROTECTED_RESOURCE"),
                                       QStringLiteral("The Nav document cannot be renamed"));
        }
        const QString old_path = resource->GetRelativePath();
        QString target = resolveRenameTarget(old_path, book_path);
        if (target.isEmpty()) {
            return BookOpResult::error(QStringLiteral("BOOK_PATH_REQUIRED"),
                                       QStringLiteral("book_path is required"));
        }
        if (bookPathTaken(target, allBookPaths()) && target != old_path) {
            return BookOpResult::error(QStringLiteral("BOOK_PATH_EXISTS"),
                                       QStringLiteral("A resource already uses %1").arg(target));
        }
        if (old_path == target) {
            return BookOpResult::success(QJsonObject {
                { QStringLiteral("resource_id"), resource->GetIdentifier() },
                { QStringLiteral("book_path"), target },
                { QStringLiteral("unchanged"), true }
            }, false, true);
        }
        QString error;
        if (!m_transaction->RelocateResource(resource->GetIdentifier(), old_path, target,
                                             trackedRevision(resource), &error)) {
            return BookOpResult::error(QStringLiteral("RENAME_FAILED"), error);
        }
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("from"), old_path },
            { QStringLiteral("book_path"), target },
            { QStringLiteral("staged"), true }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::runLivePython(const QString &script, int timeout_ms)
{
    return invokeOp([this, script, timeout_ms]() {
        if (!m_pluginSessions) {
            return BookOpResult::error(QStringLiteral("LIVE_PYTHON_UNAVAILABLE"),
                                       QStringLiteral("Live Python v2 is only available in the Sigil GUI."));
        }
        if (hasOpenTransaction()) {
            return BookOpResult::error(QStringLiteral("TRANSACTION_OPEN"),
                                       QStringLiteral("Commit or rollback the Agent transaction first. Live Python talks to the in-memory Book, not staged Agent edits."));
        }
        if (script.trimmed().isEmpty()) {
            return BookOpResult::error(QStringLiteral("SCRIPT_REQUIRED"),
                                       QStringLiteral("script is required"));
        }
        if (script.size() > 65536) {
            return BookOpResult::error(QStringLiteral("SCRIPT_TOO_LARGE"),
                                       QStringLiteral("Live Python scripts are capped at 65536 characters"));
        }
        QString status;
        QString error;
        QString output;
        const bool ok = m_pluginSessions->RunSnippetAndWait(
            script, &status, &error, qMax(1000, timeout_ms), &output);
        if (!ok) {
            return BookOpResult::error(QStringLiteral("LIVE_PYTHON_FAILED"),
                                       error.isEmpty() ? status : error,
                                       QJsonObject {
                                           { QStringLiteral("status"), status },
                                           { QStringLiteral("output"), output.left(8000) }
                                       });
        }
        m_tracked.clear();
        ++m_revision;
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("status"), status.isEmpty() ? QStringLiteral("success") : status },
            { QStringLiteral("output"), output.left(8000) },
            { QStringLiteral("applied"), true },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_revision) }
        }, true);
    });
}

BookOpResult SigilBookWorkspace::updateSpine(const QStringList &resource_ids)
{
    return invokeOp([this, resource_ids]() {
        BookOpResult ensured = ensureTransaction();
        if (!ensured.ok) return ensured;
        QStringList next;
        for (const QString &id : resource_ids) {
            HTMLResource *html = qobject_cast<HTMLResource *>(findResource(id));
            if (!html) {
                return BookOpResult::error(QStringLiteral("NOT_XHTML"),
                                           QStringLiteral("Spine entries must be XHTML: %1").arg(id));
            }
            next.append(html->GetIdentifier());
        }
        m_stagedSpine = next;
        m_hasStagedSpine = true;
        return BookOpResult::success(QJsonObject {
            { QStringLiteral("staged"), true },
            { QStringLiteral("spine"), QJsonArray::fromStringList(next) }
        }, false, true);
    });
}

BookOpResult SigilBookWorkspace::updateToc(const QJsonArray &entries)
{
    BookOpResult ensured = ensureTransaction();
    if (!ensured.ok) return ensured;
    m_stagedToc = entries;
    m_hasStagedToc = true;
    return BookOpResult::success(QJsonObject {
        { QStringLiteral("staged"), true },
        { QStringLiteral("toc"), entries },
        { QStringLiteral("entry_count"), entries.size() }
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

QString SigilBookWorkspace::workingText(const QString &resource_id) const
{
    return invokeString([this, resource_id]() {
        QString added;
        quint64 revision = 0;
        if (m_transaction && m_transaction->ReadAddedText(resource_id, &added, &revision)) return added;
        TextResource *resource = textResource(resource_id);
        return resource ? currentText(resource) : QString();
    });
}

quint64 SigilBookWorkspace::resourceRevision(const QString &resource_id) const
{
    Resource *resource = findResource(resource_id);
    return trackedRevision(resource);
}

} // namespace SigilAgent
