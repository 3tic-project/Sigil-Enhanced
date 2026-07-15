/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QPalette>
#include <QRandomGenerator>
#include <QReadLocker>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QWriteLocker>
#include <QTimer>
#include <QXmlStreamReader>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "MainUI/BookBrowser.h"
#include "Misc/PluginDB.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "Misc/ValidationResult.h"
#include "Parsers/OPFParser.h"
#include "PluginAPI/PluginSessionConsole.h"
#include "PluginAPI/PluginInputValidator.h"
#include "PluginAPI/PluginSessionManager.h"
#include "PluginAPI/PluginTextEdit.h"
#include "PluginAPI/PluginTextTransaction.h"
#include "ResourceObjects/FontResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "SourceUpdates/UniversalUpdates.h"
#include "Tabs/ContentTab.h"
#include "Tabs/FlowTab.h"
#include "Tabs/TabManager.h"
#include "ViewEditors/Searchable.h"

namespace
{

// Base64 plus the JSON envelope must still fit the 8 MiB framed-message limit.
constexpr qsizetype MAX_INLINE_BINARY_SIZE = 5 * 1024 * 1024;
constexpr qsizetype DEFAULT_BINARY_CHUNK_SIZE = 1024 * 1024;
constexpr qsizetype MAX_BINARY_CHUNK_SIZE = 2 * 1024 * 1024;
constexpr qint64 MAX_INPUT_EPUB_SIZE = 2LL * 1024 * 1024 * 1024;
constexpr qint64 MAX_TRANSACTION_BINARY_SIZE = 256LL * 1024 * 1024;
constexpr qsizetype MAX_READ_MANY_RESPONSE_SIZE = 6 * 1024 * 1024;

const QSet<QString> SUPPORTED_EVENTS {
    QStringLiteral("editor.activeChanged"),
    QStringLiteral("editor.selectionChanged"),
    QStringLiteral("editor.cursorChanged"),
    QStringLiteral("editor.contentChanged"),
    QStringLiteral("book.resourceChanged"),
    QStringLiteral("book.resourceAdded"),
    QStringLiteral("book.resourceRemoved")
};

bool IsCanonicalBookPath(const QString &book_path);

QJsonObject AttributesToJson(const TagAtts &attributes)
{
    QJsonObject result;
    for (const auto &attribute : attributes.pairs()) {
        result.insert(attribute.first, attribute.second);
    }
    return result;
}

QJsonObject PackageSections(Book *book)
{
    OPFResource *opf = book->GetOPF();
    opf->InitialLoad();
    OPFParser parser;
    parser.parse(opf->GetText());
    FolderKeeper *folder_keeper = book->GetFolderKeeper();

    QJsonArray metadata;
    for (const MetaEntry &entry : parser.m_metadata) {
        metadata.append(QJsonObject {
            { QStringLiteral("name"), entry.m_name },
            { QStringLiteral("content"), entry.m_content },
            { QStringLiteral("attributes"), AttributesToJson(entry.m_atts) }
        });
    }

    QHash<QString, QString> manifest_paths;
    QJsonArray manifest;
    for (const ManifestEntry &entry : parser.m_manifest) {
        const QString decoded_href = Utility::URLDecodePath(entry.m_href);
        const QString book_path = Utility::buildBookPath(decoded_href, opf->GetFolder());
        manifest_paths.insert(entry.m_id, book_path);
        Resource *resource = folder_keeper->GetResourceByBookPathNoThrow(book_path);
        manifest.append(QJsonObject {
            { QStringLiteral("id"), entry.m_id },
            { QStringLiteral("href"), entry.m_href },
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("media_type"), entry.m_mtype },
            { QStringLiteral("properties"), entry.m_atts.value(QStringLiteral("properties")) },
            { QStringLiteral("fallback"), entry.m_atts.value(QStringLiteral("fallback")) },
            { QStringLiteral("media_overlay"), entry.m_atts.value(QStringLiteral("media-overlay")) },
            { QStringLiteral("resource_id"), resource
                ? QJsonValue(resource->GetIdentifier()) : QJsonValue() }
        });
    }

    QJsonArray spine;
    for (const SpineEntry &entry : parser.m_spine) {
        const QString book_path = manifest_paths.value(entry.m_idref);
        Resource *resource = folder_keeper->GetResourceByBookPathNoThrow(book_path);
        spine.append(QJsonObject {
            { QStringLiteral("idref"), entry.m_idref },
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("linear"), entry.m_atts.value(QStringLiteral("linear")) },
            { QStringLiteral("properties"), entry.m_atts.value(QStringLiteral("properties")) },
            { QStringLiteral("resource_id"), resource
                ? QJsonValue(resource->GetIdentifier()) : QJsonValue() }
        });
    }

    QJsonArray guide;
    for (const GuideEntry &entry : parser.m_guide) {
        const QStringList href_parts = entry.m_href.split(QLatin1Char('#'), Qt::KeepEmptyParts);
        const QString book_path = Utility::buildBookPath(
            Utility::URLDecodePath(href_parts.first()), opf->GetFolder());
        guide.append(QJsonObject {
            { QStringLiteral("type"), entry.m_type },
            { QStringLiteral("title"), entry.m_title },
            { QStringLiteral("href"), entry.m_href },
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("fragment"), href_parts.size() > 1 ? href_parts.at(1) : QString() }
        });
    }

    QJsonArray bindings;
    for (const BindingsEntry &entry : parser.m_bindings) {
        bindings.append(QJsonObject {
            { QStringLiteral("media_type"), entry.m_mtype },
            { QStringLiteral("handler"), entry.m_handler }
        });
    }

    return QJsonObject {
        { QStringLiteral("package"), QJsonObject {
            { QStringLiteral("version"), parser.m_package.m_version },
            { QStringLiteral("unique_identifier"), parser.m_package.m_uniqueid },
            { QStringLiteral("attributes"), AttributesToJson(parser.m_package.m_atts) },
            { QStringLiteral("book_path"), opf->GetRelativePath() }
        } },
        { QStringLiteral("metadata"), metadata },
        { QStringLiteral("metadata_xml"), opf->GetMetadataXML() },
        { QStringLiteral("manifest"), manifest },
        { QStringLiteral("spine"), spine },
        { QStringLiteral("spine_attributes"), AttributesToJson(parser.m_spineattr.m_atts) },
        { QStringLiteral("guide"), guide },
        { QStringLiteral("bindings"), bindings }
    };
}

QString ResourceTypeName(Resource::ResourceType type)
{
    switch (type) {
        case Resource::HTMLResourceType: return QStringLiteral("html");
        case Resource::CSSResourceType: return QStringLiteral("css");
        case Resource::ImageResourceType: return QStringLiteral("image");
        case Resource::SVGResourceType: return QStringLiteral("svg");
        case Resource::FontResourceType: return QStringLiteral("font");
        case Resource::OPFResourceType: return QStringLiteral("opf");
        case Resource::NCXResourceType: return QStringLiteral("ncx");
        case Resource::AudioResourceType: return QStringLiteral("audio");
        case Resource::VideoResourceType: return QStringLiteral("video");
        case Resource::XMLResourceType: return QStringLiteral("xml");
        case Resource::TextResourceType:
        case Resource::MiscTextResourceType: return QStringLiteral("text");
        default: return QStringLiteral("binary");
    }
}

QJsonValue RequestId(const QJsonObject &request)
{
    return request.contains(QStringLiteral("id"))
        ? request.value(QStringLiteral("id")) : QJsonValue();
}

bool ReadPosition(const QJsonObject &params, const QString &name, int *position)
{
    const QJsonValue value = params.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > std::numeric_limits<int>::max()) {
        return false;
    }
    *position = static_cast<int>(number);
    return true;
}

bool ReadRevision(const QJsonObject &params, const QString &name, quint64 *revision)
{
    const QJsonValue value = params.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    *revision = static_cast<quint64>(number);
    return true;
}

bool ReadResultPosition(const QJsonValue &value, int *position)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < -1 || number > std::numeric_limits<int>::max()) {
        return false;
    }
    *position = static_cast<int>(number);
    return true;
}

bool ReadBinaryFile(Resource *resource, QByteArray *data, QString *error,
                    qint64 max_size = MAX_INLINE_BINARY_SIZE)
{
    if (!resource || qobject_cast<TextResource *>(resource)) {
        if (error) *error = QStringLiteral("Resource is not binary");
        return false;
    }
    QReadLocker locker(&resource->GetLock());
    QFile file(resource->GetFullPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.size() > max_size) {
        if (error) *error = QStringLiteral("Binary resource exceeds the size limit");
        return false;
    }
    *data = file.readAll();
    return true;
}

QDomElement DirectChildElement(const QDomElement &parent, const QString &local_name)
{
    for (QDomNode child = parent.firstChild(); !child.isNull(); child = child.nextSibling()) {
        const QDomElement element = child.toElement();
        if (!element.isNull() && (element.localName() == local_name
                                  || element.tagName() == local_name)) {
            return element;
        }
    }
    return QDomElement();
}

bool ParsePackageDom(const QString &source, QDomDocument *document, QString *error)
{
    QString message;
    int line = 0;
    int column = 0;
    if (!document->setContent(source, true, &message, &line, &column)) {
        if (error) {
            *error = QStringLiteral("Package XML is not well formed at %1:%2: %3")
                .arg(line).arg(column).arg(message);
        }
        return false;
    }
    return true;
}

bool ApplyMetadataUpdate(const QString &source, const QJsonArray &entries,
                         QString *updated, QString *error)
{
    QDomDocument document;
    if (!ParsePackageDom(source, &document, error)) return false;
    QDomElement metadata = DirectChildElement(document.documentElement(),
                                              QStringLiteral("metadata"));
    if (metadata.isNull()) {
        if (error) *error = QStringLiteral("Package metadata element is missing");
        return false;
    }
    while (!metadata.firstChild().isNull()) metadata.removeChild(metadata.firstChild());
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || !value.isObject()
            || !entry.value(QStringLiteral("content")).isString()) {
            if (error) *error = QStringLiteral("Metadata entries require name and content strings");
            return false;
        }
        QDomElement element = document.createElement(name);
        const QJsonObject attributes = entry.value(QStringLiteral("attributes")).toObject();
        for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
            if (!it.value().isString()) {
                if (error) *error = QStringLiteral("Metadata attributes must be strings");
                return false;
            }
            element.setAttribute(it.key(), it.value().toString());
        }
        element.appendChild(document.createTextNode(entry.value(QStringLiteral("content")).toString()));
        metadata.appendChild(element);
    }
    *updated = document.toString(-1);
    return true;
}

bool ApplySpineUpdate(const QString &source, const QJsonArray &items,
                      const QJsonObject &attributes, QString *updated, QString *error)
{
    QDomDocument document;
    if (!ParsePackageDom(source, &document, error)) return false;
    QDomElement spine = DirectChildElement(document.documentElement(), QStringLiteral("spine"));
    if (spine.isNull()) {
        if (error) *error = QStringLiteral("Package spine element is missing");
        return false;
    }
    while (!spine.firstChild().isNull()) spine.removeChild(spine.firstChild());
    for (auto it = attributes.constBegin(); it != attributes.constEnd(); ++it) {
        if (!it.value().isString()) {
            if (error) *error = QStringLiteral("Spine attributes must be strings");
            return false;
        }
        spine.setAttribute(it.key(), it.value().toString());
    }
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString idref = item.value(QStringLiteral("idref")).toString();
        if (!value.isObject() || idref.isEmpty()) {
            if (error) *error = QStringLiteral("Spine items require an idref string");
            return false;
        }
        QDomElement element = document.createElement(QStringLiteral("itemref"));
        element.setAttribute(QStringLiteral("idref"), idref);
        for (const QString &name : { QStringLiteral("id"), QStringLiteral("linear"),
                                     QStringLiteral("properties") }) {
            const QJsonValue attribute = item.value(name);
            if (!attribute.isUndefined()) {
                if (!attribute.isString()) {
                    if (error) *error = QStringLiteral("Spine item attributes must be strings");
                    return false;
                }
                element.setAttribute(name, attribute.toString());
            }
        }
        spine.appendChild(element);
    }
    *updated = document.toString(-1);
    return true;
}

QString DataFingerprint(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool WriteAtomicFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size()
        || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool ResolveArchiveFile(FolderKeeper *folder_keeper,
                        const QString &book_path,
                        QString *full_path,
                        QString *error)
{
    if (!IsCanonicalBookPath(book_path)) {
        if (error) *error = QStringLiteral("Archive path is not canonical");
        return false;
    }
    const QString root = QFileInfo(folder_keeper->GetFullPathToMainFolder()).canonicalFilePath();
    const QFileInfo candidate(root + QLatin1Char('/') + book_path);
    const QString canonical = candidate.canonicalFilePath();
    if (root.isEmpty() || canonical.isEmpty()
        || (!canonical.startsWith(root + QLatin1Char('/')) && canonical != root)
        || !candidate.isFile() || candidate.isSymLink()) {
        if (error) *error = QStringLiteral("Archive file does not exist or is unsafe");
        return false;
    }
    *full_path = canonical;
    return true;
}

bool ReadArchiveFile(FolderKeeper *folder_keeper,
                     const QString &book_path,
                     QByteArray *data,
                     QString *fingerprint,
                     QString *error)
{
    QString full_path;
    if (!ResolveArchiveFile(folder_keeper, book_path, &full_path, error)) return false;
    QFile file(full_path);
    if (file.size() > MAX_INLINE_BINARY_SIZE) {
        if (error) *error = QStringLiteral("Archive file exceeds the inline size limit");
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    *data = file.readAll();
    if (fingerprint) *fingerprint = DataFingerprint(*data);
    return true;
}

bool IsProtectedArchivePath(const QString &book_path, OPFResource *opf)
{
    return book_path == QStringLiteral("mimetype")
        || book_path == QStringLiteral("META-INF/container.xml")
        || (opf && book_path == opf->GetRelativePath());
}

bool IsCanonicalBookPath(const QString &book_path)
{
    if (book_path.isEmpty() || book_path.startsWith(QLatin1Char('/'))
        || book_path.contains(QLatin1Char('\\')) || book_path.contains(QLatin1Char(':'))
        || QDir::cleanPath(book_path) != book_path) {
        return false;
    }
    const QStringList segments = book_path.split(QLatin1Char('/'));
    return !segments.contains(QStringLiteral(".")) && !segments.contains(QStringLiteral(".."))
        && !segments.contains(QString());
}

bool DecodeResourcePayload(const QJsonObject &params, QByteArray *data, QString *error)
{
    const bool has_text = params.value(QStringLiteral("text")).isString();
    const bool has_binary = params.value(QStringLiteral("data_base64")).isString();
    if (has_text == has_binary) {
        if (error) *error = QStringLiteral("Provide exactly one of text or data_base64");
        return false;
    }
    if (has_text) {
        *data = params.value(QStringLiteral("text")).toString().toUtf8();
    } else {
        const auto decoded = QByteArray::fromBase64Encoding(
            params.value(QStringLiteral("data_base64")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
            if (error) *error = QStringLiteral("data_base64 is invalid");
            return false;
        }
        *data = decoded.decoded;
    }
    if (data->size() > MAX_INLINE_BINARY_SIZE) {
        if (error) *error = QStringLiteral("Resource data exceeds the inline size limit");
        return false;
    }
    return true;
}

struct PackageDocumentInfo {
    QSet<QString> manifestIds;
    QSet<QString> manifestPaths;
    QSet<QString> spineIds;
};

bool ParsePackageDocument(const QString &source,
                          const QString &book_path,
                          const QString &expected_version,
                          PackageDocumentInfo *info,
                          QString *error)
{
    QXmlStreamReader reader(source);
    bool package_seen = false;
    bool metadata_seen = false;
    bool manifest_seen = false;
    bool spine_seen = false;
    bool in_manifest = false;
    bool in_spine = false;
    const QString package_dir = Utility::startingDir(book_path);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (!package_seen) {
                if (name != QStringLiteral("package")) {
                    if (error) *error = QStringLiteral("The document root must be package");
                    return false;
                }
                package_seen = true;
                const QString version = reader.attributes().value(QStringLiteral("version")).toString();
                if (version != expected_version) {
                    if (error) *error = QStringLiteral("The package version cannot be changed");
                    return false;
                }
            }
            if (name == QStringLiteral("metadata")) metadata_seen = true;
            if (name == QStringLiteral("manifest")) {
                manifest_seen = true;
                in_manifest = true;
            } else if (name == QStringLiteral("spine")) {
                spine_seen = true;
                in_spine = true;
            } else if (in_manifest && name == QStringLiteral("item")) {
                const QString manifest_id = reader.attributes().value(QStringLiteral("id")).toString();
                const QString href = reader.attributes().value(QStringLiteral("href")).toString();
                if (manifest_id.isEmpty() || href.isEmpty()
                    || info->manifestIds.contains(manifest_id)) {
                    if (error) *error = QStringLiteral("Manifest IDs and hrefs must be present and unique");
                    return false;
                }
                QString normalized_path = href;
                if (!href.contains(QLatin1Char(':'))) {
                    const auto path_and_fragment = Utility::parseRelativeHREF(href);
                    if (!path_and_fragment.second.isEmpty()) {
                        if (error) *error = QStringLiteral("Manifest hrefs cannot contain fragments");
                        return false;
                    }
                    normalized_path = Utility::buildBookPath(path_and_fragment.first, package_dir);
                    if (!IsCanonicalBookPath(normalized_path)) {
                        if (error) *error = QStringLiteral("Manifest href is not a canonical Book path");
                        return false;
                    }
                }
                if (info->manifestPaths.contains(normalized_path)) {
                    if (error) *error = QStringLiteral("Manifest hrefs must be unique");
                    return false;
                }
                info->manifestIds.insert(manifest_id);
                info->manifestPaths.insert(normalized_path);
            } else if (in_spine && name == QStringLiteral("itemref")) {
                const QString idref = reader.attributes().value(QStringLiteral("idref")).toString();
                if (idref.isEmpty()) {
                    if (error) *error = QStringLiteral("Spine itemrefs require an idref");
                    return false;
                }
                info->spineIds.insert(idref);
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == QStringLiteral("manifest")) in_manifest = false;
            if (reader.name() == QStringLiteral("spine")) in_spine = false;
        }
    }
    if (reader.hasError()) {
        if (error) *error = QStringLiteral("Package XML is not well formed: %1")
            .arg(reader.errorString());
        return false;
    }
    if (!package_seen || !metadata_seen || !manifest_seen || !spine_seen) {
        if (error) *error = QStringLiteral("Package metadata, manifest, and spine are required");
        return false;
    }
    for (const QString &idref : info->spineIds) {
        if (!info->manifestIds.contains(idref)) {
            if (error) *error = QStringLiteral("Spine idref is missing from the manifest: %1").arg(idref);
            return false;
        }
    }
    return true;
}

bool ValidatePackageTransaction(PluginApi::TextTransaction *transaction,
                                FolderKeeper *folder_keeper,
                                OPFResource *opf,
                                QString *error)
{
    if (!transaction->HasPackageChange()) return true;
    PackageDocumentInfo original;
    PackageDocumentInfo staged;
    if (!ParsePackageDocument(opf->GetText(), opf->GetRelativePath(), opf->GetEpubVersion(),
                              &original, error)
        || !ParsePackageDocument(transaction->PackageChange().stagedText,
                                 opf->GetRelativePath(), opf->GetEpubVersion(),
                                 &staged, error)) {
        return false;
    }
    QSet<QString> expected_paths = original.manifestPaths;
    for (const PluginApi::StagedResourceRemoval &removal : transaction->Removals()) {
        Resource *resource = folder_keeper->GetResourceByIdentifier(removal.resourceId);
        if (resource) expected_paths.remove(resource->GetRelativePath());
    }
    for (const PluginApi::StagedResourceRelocation &relocation : transaction->Relocations()) {
        if (expected_paths.remove(relocation.originalBookPath)) {
            expected_paths.insert(relocation.targetBookPath);
        }
    }
    for (const PluginApi::StagedResourceAddition &addition : transaction->Additions()) {
        if (addition.manifested) expected_paths.insert(addition.bookPath);
    }
    if (staged.manifestPaths != expected_paths) {
        if (error) *error = QStringLiteral("Package manifest does not match the staged resource structure");
        return false;
    }
    return true;
}

}

PluginSession::PluginSession(const Plugin &plugin,
                             MainWindow *main_window,
                             TabManager *tab_manager,
                             QObject *parent) :
    QObject(parent),
    m_Plugin(plugin),
    m_MainWindow(main_window),
    m_TabManager(tab_manager),
    m_SessionId(QUuid::createUuid()),
    m_Server(nullptr),
    m_Socket(nullptr),
    m_Process(new QProcess(this)),
    m_Authenticated(false),
    m_Ending(false),
    m_EndSignalScheduled(false),
    m_ProgressMaximum(0),
    m_ValidationErrorCount(0),
    m_MaterializationRoot(nullptr),
    m_InputEpubFile(nullptr),
    m_InputEpubAccepted(false),
    m_BookRevision(1),
    m_InRequest(false),
    m_HoldsWriter(false)
{
}

PluginSession::~PluginSession()
{
    ReleaseWriter();
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
        m_Process->waitForFinished(1000);
    }
    CleanServer();
}

QUuid PluginSession::SessionId() const
{
    return m_SessionId;
}

QString PluginSession::PluginName() const
{
    return m_Plugin.get_name();
}

QString PluginSession::PluginType() const
{
    return m_Plugin.get_type();
}

QString PluginSession::Status() const
{
    return m_Status;
}

int PluginSession::ValidationErrorCount() const
{
    return m_ValidationErrorCount;
}

bool PluginSession::IsBookSession() const
{
    return m_Plugin.get_lifetime() == QStringLiteral("book-session");
}

QString PluginSession::PendingInputEpubPath() const
{
    return m_InputEpubAccepted && m_InputEpubFile
        ? m_InputEpubFile->fileName() : QString();
}

bool PluginSession::Start(QString *error)
{
    const QString interpreter = ResolveInterpreter();
    if (interpreter.isEmpty()) {
        if (error) {
            *error = tr("No Python 3 interpreter is configured.");
        }
        return false;
    }

    const QString launcher = PluginDB::launcherRoot() + QStringLiteral("/python/live_launcher.py");
    const QString plugin_path = PluginDB::pluginsPath() + QLatin1Char('/')
        + m_Plugin.get_name() + QStringLiteral("/plugin.py");
    if (!QFileInfo::exists(launcher) || !QFileInfo::exists(plugin_path)) {
        if (error) {
            *error = tr("The live plugin launcher or plugin entry point does not exist.");
        }
        return false;
    }

#ifdef Q_OS_WIN
    m_ServerName = QStringLiteral("sigil-plugin-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(m_SessionId.toString(QUuid::WithoutBraces));
#else
    m_ServerName = QDir::temp().absoluteFilePath(
        QStringLiteral("sigil-p-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(m_SessionId.toString(QUuid::WithoutBraces).left(12)));
#endif
    m_Token = QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QString::number(QRandomGenerator::global()->generate64(), 16);

    QLocalServer::removeServer(m_ServerName);
    m_Server = new QLocalServer(this);
    m_Server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_Server->listen(m_ServerName)) {
        if (error) {
            *error = tr("Could not create the local plugin server: %1").arg(m_Server->errorString());
        }
        CleanServer();
        return false;
    }
    connect(m_Server, &QLocalServer::newConnection, this, &PluginSession::AcceptConnection);

    QSharedPointer<Book> book = m_MainWindow->GetCurrentBook();
    for (Resource *resource : book->GetFolderKeeper()->GetResourceList()) {
        TrackResource(resource);
    }
    for (ContentTab *tab : m_TabManager->GetContentTabs()) {
        TrackEditorTab(tab);
    }
    for (const QString &event : m_Plugin.get_events()) {
        if (SUPPORTED_EVENTS.contains(event)) m_Subscriptions.insert(event);
    }
    connect(book->GetFolderKeeper(), &FolderKeeper::ResourceAdded, this,
            [this](const Resource *resource) {
        Resource *tracked = const_cast<Resource *>(resource);
        TrackResource(tracked);
        m_BookRevision += 1;
        if (m_Subscriptions.contains(QStringLiteral("book.resourceAdded"))) {
            Notify(QStringLiteral("book.resourceAdded"), ResourceInfo(tracked));
        }
    });
    connect(book->GetFolderKeeper(), &FolderKeeper::ResourceRemoved, this,
            [this](const Resource *resource) {
        if (m_Subscriptions.contains(QStringLiteral("book.resourceRemoved"))) {
            Notify(QStringLiteral("book.resourceRemoved"),
                   QJsonObject {
                       { QStringLiteral("resource_id"), resource->GetIdentifier() },
                       { QStringLiteral("book_path"), resource->GetRelativePath() }
                   });
        }
    });
    connect(m_TabManager, &TabManager::TabChanged, this,
            [this](ContentTab *old_tab, ContentTab *new_tab) {
        TrackEditorTab(new_tab);
        if (!m_Subscriptions.contains(QStringLiteral("editor.activeChanged"))) return;
        Notify(QStringLiteral("editor.activeChanged"), QJsonObject {
            { QStringLiteral("old_resource_id"), old_tab
                ? QJsonValue(old_tab->GetLoadedResource()->GetIdentifier()) : QJsonValue() },
            { QStringLiteral("new_resource_id"), new_tab
                ? QJsonValue(new_tab->GetLoadedResource()->GetIdentifier()) : QJsonValue() },
            { QStringLiteral("state"), EditorEventState() }
        });
    });

    m_Console = new PluginSessionConsole(m_Plugin.get_name(), m_MainWindow);
    connect(m_Console, &PluginSessionConsole::CancelRequested, this, &PluginSession::Cancel);
    m_Console->show();

    connect(m_Process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_Console) {
            m_Console->AppendOutput(QString::fromUtf8(m_Process->readAllStandardOutput()));
        }
    });
    connect(m_Process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_Console) {
            m_Console->AppendOutput(QString::fromUtf8(m_Process->readAllStandardError()));
        }
    });
    connect(m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        Finish(QStringLiteral("failed"), m_Process->errorString());
    });
    connect(m_Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PluginSession::ProcessFinished);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SIGIL_PLUGIN_SOCKET"), m_ServerName);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_TOKEN"), m_Token);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_API_VERSION"), QStringLiteral("2"));
    m_Process->setProcessEnvironment(environment);
    m_Process->setProgram(interpreter);
    QStringList arguments {
        launcher,
        QStringLiteral("--plugin"), plugin_path,
        QStringLiteral("--plugin-name"), m_Plugin.get_name()
    };
    if (m_Plugin.get_declared_runtime() != Plugin::LiveRuntime) {
        arguments.append(QStringList {
            QStringLiteral("--compat-v1"),
            QStringLiteral("--plugin-type"), m_Plugin.get_type()
        });
    }
    m_Process->setArguments(arguments);
    m_Process->setWorkingDirectory(QFileInfo(plugin_path).absolutePath());
    m_Process->start();

    QTimer::singleShot(10000, this, [this]() {
        if (!m_Authenticated && !m_Ending) {
            Finish(QStringLiteral("failed"), tr("Plugin handshake timed out."));
            Cancel();
        }
    });
    return true;
}

void PluginSession::Cancel()
{
    if (m_Ending) {
        return;
    }
    m_Ending = true;
    if (m_Socket && m_Socket->state() == QLocalSocket::ConnectedState) {
        const QJsonObject notification {
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("method"), QStringLiteral("session.cancelRequested") },
            { QStringLiteral("params"), QJsonObject() }
        };
        m_Socket->write(PluginApi::EncodeFrame(notification));
        m_Socket->flush();
    }
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->terminate();
        if (!m_Process->waitForFinished(2000)) {
            m_Process->kill();
            m_Process->waitForFinished(2000);
        }
    }
    Finish(QStringLiteral("cancelled"));
}

void PluginSession::AcceptConnection()
{
    while (m_Server && m_Server->hasPendingConnections()) {
        QLocalSocket *candidate = m_Server->nextPendingConnection();
        if (m_Socket) {
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }
        m_Socket = candidate;
        connect(m_Socket, &QLocalSocket::readyRead, this, &PluginSession::ReadMessages);
        connect(m_Socket, &QLocalSocket::disconnected, this, [this]() {
            if (!m_Ending && m_Process->state() != QProcess::NotRunning) {
                Finish(QStringLiteral("failed"), tr("Plugin control connection closed unexpectedly."));
            }
        });
    }
}

void PluginSession::ReadMessages()
{
    QList<QJsonObject> messages;
    QString error;
    if (!m_Decoder.Append(m_Socket->readAll(), &messages, &error)) {
        RespondError(QJsonValue(), -32700, error);
        m_Socket->disconnectFromServer();
        return;
    }
    for (const QJsonObject &message : messages) {
        Dispatch(message);
    }
}

void PluginSession::ProcessFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    if (m_Ending) {
        return;
    }
    if (exit_status == QProcess::CrashExit || exit_code != 0) {
        Finish(QStringLiteral("failed"), tr("Plugin process exited with code %1.").arg(exit_code));
    } else {
        Finish(QStringLiteral("success"));
    }
}

void PluginSession::Dispatch(const QJsonObject &request)
{
    QScopedValueRollback<bool> request_scope(m_InRequest, true);
    const QJsonValue id = RequestId(request);
    if (request.value(QStringLiteral("jsonrpc")) != QStringLiteral("2.0")
        || !request.value(QStringLiteral("method")).isString()) {
        RespondError(id, -32600, QStringLiteral("Invalid Request"));
        return;
    }
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (!m_Authenticated) {
        if (method != QStringLiteral("session.hello")
            || params.value(QStringLiteral("token")).toString() != m_Token
            || params.value(QStringLiteral("protocol_version")).toInt() != PluginApi::PROTOCOL_VERSION
            || params.value(QStringLiteral("api_version")).toInt() != PluginApi::API_VERSION
            || params.value(QStringLiteral("plugin_name")).toString() != m_Plugin.get_name()) {
            RespondError(id, -32600, QStringLiteral("Invalid session handshake"));
            if (m_Socket) {
                m_Socket->disconnectFromServer();
            }
            return;
        }
        m_Authenticated = true;
        m_Token.clear();
        m_Server->close();
        SettingsStore settings;
        const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
        Respond(id, QJsonObject {
            { QStringLiteral("session_id"), m_SessionId.toString(QUuid::WithoutBraces) },
            { QStringLiteral("protocol_version"), PluginApi::PROTOCOL_VERSION },
            { QStringLiteral("api_version"), PluginApi::API_VERSION },
            { QStringLiteral("lifetime"), m_Plugin.get_lifetime() },
            { QStringLiteral("position_encoding"), QStringLiteral("utf-16") },
            { QStringLiteral("max_message_size"), static_cast<qint64>(PluginApi::DEFAULT_MAX_MESSAGE_SIZE) },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_BookRevision) },
            { QStringLiteral("ui"), QJsonObject {
                { QStringLiteral("language"), settings.uiLanguage() },
                { QStringLiteral("color_mode"), dark ? QStringLiteral("dark") : QStringLiteral("light") }
            } }
        });
        if (m_Console) {
            m_Console->SetStatus(tr("Connected"));
        }
        return;
    }

    if (method == QStringLiteral("session.ping")) {
        Respond(id, QJsonObject {{ QStringLiteral("pong"), true }});
    } else if (method == QStringLiteral("session.getInfo")) {
        Respond(id, QJsonObject {
            { QStringLiteral("session_id"), m_SessionId.toString(QUuid::WithoutBraces) },
            { QStringLiteral("plugin_name"), m_Plugin.get_name() },
            { QStringLiteral("lifetime"), m_Plugin.get_lifetime() }
        });
    } else if (method == QStringLiteral("session.finish")) {
        Respond(id, QJsonObject {{ QStringLiteral("accepted"), true }});
        Finish(params.value(QStringLiteral("status")).toString(QStringLiteral("success")),
               params.value(QStringLiteral("message")).toString());
    } else if (method == QStringLiteral("events.subscribe")
               || method == QStringLiteral("events.unsubscribe")) {
        const QJsonArray requested = params.value(QStringLiteral("events")).toArray();
        QSet<QString> events;
        for (const QJsonValue &value : requested) {
            if (!value.isString() || !SUPPORTED_EVENTS.contains(value.toString())) {
                RespondError(id, -32602, QStringLiteral("Event name is unsupported"));
                return;
            }
            events.insert(value.toString());
        }
        if (method == QStringLiteral("events.subscribe")) m_Subscriptions.unite(events);
        else m_Subscriptions.subtract(events);
        QStringList subscribed = m_Subscriptions.values();
        std::sort(subscribed.begin(), subscribed.end());
        Respond(id, QJsonObject {
            { QStringLiteral("subscribed"), QJsonArray::fromStringList(subscribed) }
        });
    } else if (method == QStringLiteral("book.getRevision")) {
        Respond(id, QJsonObject {{ QStringLiteral("revision"), static_cast<qint64>(m_BookRevision) }});
    } else if (method == QStringLiteral("book.getInfo")) {
        QSharedPointer<Book> book = m_MainWindow->GetCurrentBook();
        Respond(id, QJsonObject {
            { QStringLiteral("epub_version"), book->GetConstOPF()->GetEpubVersion() },
            { QStringLiteral("modified"), book->IsModified() },
            { QStringLiteral("file_path"), m_MainWindow->GetCurrentFilePath() },
            { QStringLiteral("revision"), static_cast<qint64>(m_BookRevision) }
        });
    } else if (method == QStringLiteral("book.getMetadata")
               || method == QStringLiteral("book.getManifest")
               || method == QStringLiteral("book.getSpine")
               || method == QStringLiteral("book.getGuide")
               || method == QStringLiteral("book.getBindings")) {
        const QJsonObject sections = PackageSections(m_MainWindow->GetCurrentBook().data());
        if (method == QStringLiteral("book.getMetadata")) {
            Respond(id, QJsonObject {
                { QStringLiteral("items"), sections.value(QStringLiteral("metadata")) },
                { QStringLiteral("xml"), sections.value(QStringLiteral("metadata_xml")) },
                { QStringLiteral("package"), sections.value(QStringLiteral("package")) },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(
                    m_MainWindow->GetCurrentBook()->GetOPF())) }
            });
        } else if (method == QStringLiteral("book.getSpine")) {
            Respond(id, QJsonObject {
                { QStringLiteral("items"), sections.value(QStringLiteral("spine")) },
                { QStringLiteral("attributes"), sections.value(QStringLiteral("spine_attributes")) },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(
                    m_MainWindow->GetCurrentBook()->GetOPF())) }
            });
        } else {
            const QHash<QString, QString> section_names {
                { QStringLiteral("book.getManifest"), QStringLiteral("manifest") },
                { QStringLiteral("book.getGuide"), QStringLiteral("guide") },
                { QStringLiteral("book.getBindings"), QStringLiteral("bindings") }
            };
            Respond(id, QJsonObject {
                { QStringLiteral("items"), sections.value(section_names.value(method)) }
            });
        }
    } else if (method == QStringLiteral("book.getSelection")) {
        QJsonArray items;
        for (Resource *resource : m_MainWindow->GetBookBrowserSelectedResources()) {
            items.append(ResourceInfo(resource));
        }
        Respond(id, QJsonObject {{ QStringLiteral("items"), items }});
    } else if (method == QStringLiteral("book.getCompatibilitySnapshot")) {
        QSharedPointer<Book> book = m_MainWindow->GetCurrentBook();
        OPFResource *opf = book->GetOPF();
        opf->InitialLoad();

        QJsonArray resources;
        QJsonObject font_mangling;
        for (Resource *resource : book->GetFolderKeeper()->GetResourceList()) {
            resources.append(ResourceInfo(resource));
            if (FontResource *font = qobject_cast<FontResource *>(resource)) {
                const QString algorithm = font->GetObfuscationAlgorithm();
                if (!algorithm.isEmpty()) {
                    font_mangling.insert(resource->GetRelativePath(), algorithm);
                }
            }
        }

        QJsonArray selected;
        for (Resource *resource : m_MainWindow->GetBookBrowserSelectedResources()) {
            selected.append(resource->GetRelativePath());
        }

        SettingsStore settings;
        const QPalette palette = qApp->palette();
        const bool dark = palette.color(QPalette::Window).lightness() < 128;
        QStringList linux_hunspell_dictionary_dirs;
#if !defined(Q_OS_WIN32) && !defined(Q_OS_MAC)
        linux_hunspell_dictionary_dirs = Utility::LinuxHunspellDictionaryDirs();
#endif
        QJsonObject colors {
            { QStringLiteral("Window"), palette.color(QPalette::Window).name() },
            { QStringLiteral("Base"), palette.color(QPalette::Base).name() },
            { QStringLiteral("Text"), palette.color(QPalette::Text).name() },
            { QStringLiteral("Highlight"), palette.color(QPalette::Highlight).name() },
            { QStringLiteral("HighlightedText"), palette.color(QPalette::HighlightedText).name() }
        };
        Respond(id, QJsonObject {
            { QStringLiteral("package"), QJsonObject {
                { QStringLiteral("resource"), ResourceInfo(opf) },
                { QStringLiteral("text"), opf->GetText() },
                { QStringLiteral("book_path"), opf->GetRelativePath() }
            } },
            { QStringLiteral("resources"), resources },
            { QStringLiteral("selected"), selected },
            { QStringLiteral("font_mangling"), font_mangling },
            { QStringLiteral("configuration"), QJsonObject {
                { QStringLiteral("application_dir"), QCoreApplication::applicationDirPath() },
                { QStringLiteral("preferences_dir"), Utility::DefinePrefsDir() },
                { QStringLiteral("linux_hunspell_dictionary_dirs"),
                    QJsonArray::fromStringList(linux_hunspell_dictionary_dirs) },
                { QStringLiteral("ui_language"), settings.uiLanguage() },
                { QStringLiteral("spellcheck_language"), settings.dictionary() },
                { QStringLiteral("color_mode"), dark ? QStringLiteral("dark")
                                                      : QStringLiteral("light") },
                { QStringLiteral("colors"), colors },
                { QStringLiteral("ui_font"), qApp->font().toString() },
                { QStringLiteral("using_automate"), m_MainWindow->UsingAutomate() },
                { QStringLiteral("automate_parameter"),
                    m_MainWindow->AutomatePluginParameter() }
            } }
        });
    } else if (method == QStringLiteral("validation.publishResults")) {
        if (m_Plugin.get_type() != QStringLiteral("validation")) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Only validation plugins may publish validation results"));
            return;
        }
        const QJsonArray values = params.value(QStringLiteral("results")).toArray();
        if (values.size() > 10000) {
            RespondError(id, PluginApi::PayloadTooLarge,
                         QStringLiteral("At most 10000 validation results may be published"));
            return;
        }
        QList<ValidationResult> results;
        int error_count = 0;
        for (const QJsonValue &value : values) {
            const QJsonObject item = value.toObject();
            const QString type = item.value(QStringLiteral("type")).toString();
            const QString book_path = item.value(QStringLiteral("book_path")).toString();
            const QString message = item.value(QStringLiteral("message")).toString();
            int line = -1;
            int character = -1;
            if ((type != QStringLiteral("info") && type != QStringLiteral("warning")
                 && type != QStringLiteral("error"))
                || (!book_path.isEmpty() && !IsCanonicalBookPath(book_path))
                || message.isEmpty() || message.size() > 65536
                || !ReadResultPosition(item.value(QStringLiteral("line")), &line)
                || !ReadResultPosition(item.value(QStringLiteral("character")), &character)) {
                RespondError(id, -32602, QStringLiteral("Validation result is invalid"));
                return;
            }
            ValidationResult::ResType result_type = ValidationResult::ResType_Info;
            if (type == QStringLiteral("warning")) result_type = ValidationResult::ResType_Warn;
            else if (type == QStringLiteral("error")) {
                result_type = ValidationResult::ResType_Error;
                ++error_count;
            }
            results.append(ValidationResult(result_type, book_path, line, character, message));
        }
        m_MainWindow->SetValidationResults(results);
        m_ValidationErrorCount = error_count;
        Respond(id, QJsonObject {{ QStringLiteral("accepted"), results.size() }});
    } else if (method == QStringLiteral("archive.listFiles")) {
        FolderKeeper *folder_keeper = m_MainWindow->GetCurrentBook()->GetFolderKeeper();
        const QString root_path = QFileInfo(folder_keeper->GetFullPathToMainFolder())
            .canonicalFilePath();
        QDir root(root_path);
        QStringList paths;
        QDirIterator iterator(root_path, QDir::Files | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            iterator.next();
            const QString book_path = QDir::fromNativeSeparators(
                root.relativeFilePath(iterator.filePath()));
            if (IsCanonicalBookPath(book_path)) paths.append(book_path);
        }
        std::sort(paths.begin(), paths.end());
        const int page_size = qBound(1, params.value(QStringLiteral("page_size")).toInt(200), 500);
        const int offset = qMax(0, params.value(QStringLiteral("cursor")).toString().toInt());
        QJsonArray items;
        for (int index = offset; index < paths.size() && items.size() < page_size; ++index) {
            const QString &book_path = paths.at(index);
            Resource *resource = folder_keeper->GetResourceByBookPathNoThrow(book_path);
            items.append(QJsonObject {
                { QStringLiteral("book_path"), book_path },
                { QStringLiteral("size"), QFileInfo(root.filePath(book_path)).size() },
                { QStringLiteral("resource_id"), resource
                    ? QJsonValue(resource->GetIdentifier()) : QJsonValue() },
                { QStringLiteral("protected"), IsProtectedArchivePath(
                    book_path, m_MainWindow->GetCurrentBook()->GetOPF()) }
            });
        }
        const int next_offset = offset + items.size();
        Respond(id, QJsonObject {
            { QStringLiteral("items"), items },
            { QStringLiteral("next_cursor"), next_offset < paths.size()
                ? QJsonValue(QString::number(next_offset)) : QJsonValue() }
        });
    } else if (method == QStringLiteral("archive.readFile")) {
        const QString book_path = params.value(QStringLiteral("book_path")).toString();
        QByteArray data;
        QString fingerprint;
        QString read_error;
        if (!ReadArchiveFile(m_MainWindow->GetCurrentBook()->GetFolderKeeper(), book_path,
                             &data, &fingerprint, &read_error)) {
            const int code = read_error.contains(QStringLiteral("size limit"))
                ? PluginApi::PayloadTooLarge : PluginApi::ResourceNotFound;
            RespondError(id, code, read_error);
        } else {
            Respond(id, QJsonObject {
                { QStringLiteral("book_path"), book_path },
                { QStringLiteral("data_base64"), QString::fromLatin1(data.toBase64()) },
                { QStringLiteral("sha256"), fingerprint },
                { QStringLiteral("protected"), IsProtectedArchivePath(
                    book_path, m_MainWindow->GetCurrentBook()->GetOPF()) }
            });
        }
    } else if (method == QStringLiteral("resource.list")) {
        QStringList types;
        for (const QJsonValue &type : params.value(QStringLiteral("types")).toArray()) {
            if (type.isString()) {
                types.append(type.toString());
            }
        }
        const int page_size = qBound(1, params.value(QStringLiteral("page_size")).toInt(200), 500);
        const int offset = qMax(0, params.value(QStringLiteral("cursor")).toString().toInt());
        QList<Resource *> resources = m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceList();
        std::sort(resources.begin(), resources.end(), [](Resource *left, Resource *right) {
            return left->GetRelativePath() < right->GetRelativePath();
        });
        QJsonArray items;
        int matched = 0;
        int next_offset = -1;
        for (Resource *resource : resources) {
            if (!types.isEmpty() && !types.contains(ResourceTypeName(resource->Type()))) continue;
            if (matched++ < offset) continue;
            if (items.size() >= page_size) {
                next_offset = matched - 1;
                break;
            }
            items.append(ResourceInfo(resource));
        }
        QJsonObject result {{ QStringLiteral("items"), items }};
        result.insert(QStringLiteral("next_cursor"), next_offset < 0
            ? QJsonValue() : QJsonValue(QString::number(next_offset)));
        Respond(id, result);
    } else if (method == QStringLiteral("resource.resolvePath")) {
        Resource *resource = m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(
            params.value(QStringLiteral("book_path")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
        } else {
            Respond(id, ResourceInfo(resource));
        }
    } else if (method == QStringLiteral("resource.getInfo")) {
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
        } else {
            Respond(id, ResourceInfo(resource));
        }
    } else if (method == QStringLiteral("resource.readText")) {
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
        } else {
            resource->InitialLoad();
            Respond(id, QJsonObject {
                { QStringLiteral("text"), resource->GetText() },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
    } else if (method == QStringLiteral("resource.readBinary")) {
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        QByteArray data;
        QString read_error;
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
        } else if (!ReadBinaryFile(resource, &data, &read_error)) {
            const int code = read_error.contains(QStringLiteral("size limit"))
                ? PluginApi::PayloadTooLarge : PluginApi::UnsupportedOperation;
            RespondError(id, code, read_error);
        } else {
            Respond(id, QJsonObject {
                { QStringLiteral("resource_id"), resource->GetIdentifier() },
                { QStringLiteral("data_base64"), QString::fromLatin1(data.toBase64()) },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
    } else if (method == QStringLiteral("resource.materializeTemporary")) {
        const QString resource_id = params.value(QStringLiteral("resource_id")).toString();
        const QString requested_path = params.value(QStringLiteral("book_path")).toString();
        if (resource_id.isEmpty() == requested_path.isEmpty()) {
            RespondError(id, -32602,
                         QStringLiteral("Provide exactly one of resource_id or book_path"));
            return;
        }
        Resource *resource = resource_id.isEmpty() ? nullptr : ResolveResource(resource_id);
        QString source_path;
        QString book_path;
        if (!resource_id.isEmpty() && !resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        } else if (resource) {
            source_path = resource->GetFullPath();
            book_path = resource->GetRelativePath();
        } else {
            QString path_error;
            if (!ResolveArchiveFile(m_MainWindow->GetCurrentBook()->GetFolderKeeper(),
                                    requested_path, &source_path, &path_error)) {
                RespondError(id, PluginApi::ResourceNotFound, path_error);
                return;
            }
            book_path = requested_path;
        }
        if (!m_MaterializationRoot) {
            m_MaterializationRoot = new QTemporaryDir(
                QDir::tempPath() + QStringLiteral("/sigil-plugin-session-XXXXXX"));
            if (!m_MaterializationRoot->isValid()) {
                const QString error = m_MaterializationRoot->errorString();
                delete m_MaterializationRoot;
                m_MaterializationRoot = nullptr;
                RespondError(id, PluginApi::UnsupportedOperation, error);
                return;
            }
        }
        const QString suffix = QFileInfo(book_path).suffix();
        const QString pattern = m_MaterializationRoot->path()
            + QStringLiteral("/materialized-XXXXXX")
            + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
        auto *temporary = new QTemporaryFile(pattern, this);
        if (!temporary->open()) {
            const QString error = temporary->errorString();
            delete temporary;
            RespondError(id, PluginApi::UnsupportedOperation, error);
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 total = 0;
        QString copy_error;
        if (TextResource *text = qobject_cast<TextResource *>(resource)) {
            text->InitialLoad();
            const QByteArray data = text->GetText().toUtf8();
            if (temporary->write(data) != data.size()) copy_error = temporary->errorString();
            hash.addData(data);
            total = data.size();
        } else {
            std::unique_ptr<QReadLocker> locker;
            if (resource) locker = std::make_unique<QReadLocker>(&resource->GetLock());
            QFile source(source_path);
            if (!source.open(QIODevice::ReadOnly)) copy_error = source.errorString();
            while (copy_error.isEmpty() && !source.atEnd()) {
                const QByteArray chunk = source.read(DEFAULT_BINARY_CHUNK_SIZE);
                if (chunk.isEmpty() && source.error() != QFile::NoError) {
                    copy_error = source.errorString();
                } else if (temporary->write(chunk) != chunk.size()) {
                    copy_error = temporary->errorString();
                } else {
                    hash.addData(chunk);
                    total += chunk.size();
                }
            }
        }
        if (!copy_error.isEmpty() || !temporary->flush()) {
            if (copy_error.isEmpty()) copy_error = temporary->errorString();
            delete temporary;
            RespondError(id, PluginApi::UnsupportedOperation, copy_error);
            return;
        }
        temporary->close();
        m_MaterializedFiles.append(temporary);
        Respond(id, QJsonObject {
            { QStringLiteral("path"), temporary->fileName() },
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("resource_id"), resource ? QJsonValue(resource->GetIdentifier())
                                                        : QJsonValue() },
            { QStringLiteral("revision"), resource
                ? QJsonValue(static_cast<qint64>(Revision(resource))) : QJsonValue() },
            { QStringLiteral("size"), total },
            { QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex()) }
        });
    } else if (method == QStringLiteral("binary.openRead")) {
        const QString resource_id = params.value(QStringLiteral("resource_id")).toString();
        const QString requested_path = params.value(QStringLiteral("book_path")).toString();
        if (resource_id.isEmpty() == requested_path.isEmpty()) {
            RespondError(id, -32602,
                         QStringLiteral("Provide exactly one of resource_id or book_path"));
            return;
        }
        Resource *resource = resource_id.isEmpty() ? nullptr : ResolveResource(resource_id);
        QString source_path;
        QString book_path;
        if (!resource_id.isEmpty() && !resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        } else if (resource && qobject_cast<TextResource *>(resource)) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Text resources use resource.readText"));
            return;
        } else if (resource) {
            source_path = resource->GetFullPath();
            book_path = resource->GetRelativePath();
        } else {
            QString path_error;
            if (!ResolveArchiveFile(m_MainWindow->GetCurrentBook()->GetFolderKeeper(),
                                    requested_path, &source_path, &path_error)) {
                RespondError(id, PluginApi::ResourceNotFound, path_error);
                return;
            }
            book_path = requested_path;
        }

        auto *snapshot = new QTemporaryFile(this);
        if (!snapshot->open()) {
            const QString error = snapshot->errorString();
            delete snapshot;
            RespondError(id, PluginApi::UnsupportedOperation, error);
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 total = 0;
        QString copy_error;
        {
            std::unique_ptr<QReadLocker> locker;
            if (resource) locker = std::make_unique<QReadLocker>(&resource->GetLock());
            QFile source(source_path);
            if (!source.open(QIODevice::ReadOnly)) {
                copy_error = source.errorString();
            } else {
                while (!source.atEnd()) {
                    const QByteArray chunk = source.read(DEFAULT_BINARY_CHUNK_SIZE);
                    if (chunk.isEmpty() && source.error() != QFile::NoError) {
                        copy_error = source.errorString();
                        break;
                    }
                    if (snapshot->write(chunk) != chunk.size()) {
                        copy_error = snapshot->errorString();
                        break;
                    }
                    hash.addData(chunk);
                    total += chunk.size();
                }
            }
        }
        if (!copy_error.isEmpty() || !snapshot->flush() || !snapshot->seek(0)) {
            if (copy_error.isEmpty()) copy_error = snapshot->errorString();
            delete snapshot;
            RespondError(id, PluginApi::UnsupportedOperation, copy_error);
            return;
        }
        const QString stream_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        BinaryReadStream stream;
        stream.file = snapshot;
        stream.resourceId = resource ? resource->GetIdentifier() : QString();
        stream.bookPath = book_path;
        stream.revision = resource ? Revision(resource) : 0;
        stream.size = total;
        stream.sha256 = QString::fromLatin1(hash.result().toHex());
        m_BinaryReadStreams.insert(stream_id, stream);
        Respond(id, QJsonObject {
            { QStringLiteral("stream_id"), stream_id },
            { QStringLiteral("resource_id"), stream.resourceId.isEmpty()
                ? QJsonValue() : QJsonValue(stream.resourceId) },
            { QStringLiteral("book_path"), stream.bookPath },
            { QStringLiteral("revision"), stream.resourceId.isEmpty()
                ? QJsonValue() : QJsonValue(static_cast<qint64>(stream.revision)) },
            { QStringLiteral("size"), stream.size },
            { QStringLiteral("sha256"), stream.sha256 },
            { QStringLiteral("chunk_size"), static_cast<qint64>(DEFAULT_BINARY_CHUNK_SIZE) }
        });
    } else if (method == QStringLiteral("binary.readChunk")) {
        const QString stream_id = params.value(QStringLiteral("stream_id")).toString();
        auto stream = m_BinaryReadStreams.find(stream_id);
        if (stream == m_BinaryReadStreams.end()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Binary stream not found"));
            return;
        }
        qint64 requested = params.value(QStringLiteral("max_bytes"))
            .toInteger(DEFAULT_BINARY_CHUNK_SIZE);
        if (requested < 1 || requested > MAX_BINARY_CHUNK_SIZE) {
            RespondError(id, -32602, QStringLiteral("max_bytes is outside the allowed range"));
            return;
        }
        const QByteArray chunk = stream->file->read(requested);
        if (chunk.isEmpty() && stream->file->error() != QFile::NoError) {
            RespondError(id, PluginApi::UnsupportedOperation, stream->file->errorString());
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("stream_id"), stream_id },
            { QStringLiteral("data_base64"), QString::fromLatin1(chunk.toBase64()) },
            { QStringLiteral("offset"), stream->file->pos() },
            { QStringLiteral("eof"), stream->file->atEnd() }
        });
    } else if (method == QStringLiteral("binary.close")) {
        const QString stream_id = params.value(QStringLiteral("stream_id")).toString();
        auto stream = m_BinaryReadStreams.find(stream_id);
        if (stream == m_BinaryReadStreams.end()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Binary stream not found"));
            return;
        }
        delete stream->file;
        m_BinaryReadStreams.erase(stream);
        Respond(id, QJsonObject {{ QStringLiteral("closed"), true }});
    } else if (method == QStringLiteral("input.beginEpub")) {
        if (m_Plugin.get_type() != QStringLiteral("input")) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Only input plugins may submit an EPUB"));
            return;
        }
        const QString filename = params.value(QStringLiteral("filename")).toString();
        const QFileInfo filename_info(filename);
        const QJsonValue expected_value = params.value(QStringLiteral("size"));
        const qint64 expected_size = expected_value.isUndefined()
            ? -1 : expected_value.toInteger(-1);
        if (filename.isEmpty() || filename != filename_info.fileName()
            || filename_info.suffix().compare(QStringLiteral("epub"), Qt::CaseInsensitive) != 0
            || expected_size < -1 || expected_size > MAX_INPUT_EPUB_SIZE) {
            RespondError(id, -32602,
                         QStringLiteral("A basename ending in .epub and a valid size are required"));
            return;
        }
        auto *file = new QTemporaryFile(
            QDir::tempPath() + QStringLiteral("/sigil-plugin-input-XXXXXX.epub"), this);
        if (!file->open()) {
            const QString error = file->errorString();
            delete file;
            RespondError(id, PluginApi::UnsupportedOperation, error);
            return;
        }
        const QString upload_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        InputUpload upload;
        upload.file = file;
        upload.filename = filename;
        upload.expectedSize = expected_size;
        m_InputUploads.insert(upload_id, upload);
        Respond(id, QJsonObject {
            { QStringLiteral("upload_id"), upload_id },
            { QStringLiteral("chunk_size"), static_cast<qint64>(DEFAULT_BINARY_CHUNK_SIZE) },
            { QStringLiteral("max_size"), MAX_INPUT_EPUB_SIZE }
        });
    } else if (method == QStringLiteral("input.writeChunk")) {
        const QString upload_id = params.value(QStringLiteral("upload_id")).toString();
        auto upload = m_InputUploads.find(upload_id);
        if (upload == m_InputUploads.end()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Input upload not found"));
            return;
        }
        const auto decoded = QByteArray::fromBase64Encoding(
            params.value(QStringLiteral("data_base64")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok
            || decoded.decoded.isEmpty() || decoded.decoded.size() > MAX_BINARY_CHUNK_SIZE) {
            RespondError(id, -32602, QStringLiteral("Input chunk is empty, invalid, or too large"));
            return;
        }
        if (upload->received + decoded.decoded.size() > MAX_INPUT_EPUB_SIZE
            || (upload->expectedSize >= 0
                && upload->received + decoded.decoded.size() > upload->expectedSize)) {
            RespondError(id, PluginApi::PayloadTooLarge,
                         QStringLiteral("Input EPUB exceeds its declared or maximum size"));
            return;
        }
        if (upload->file->write(decoded.decoded) != decoded.decoded.size()) {
            RespondError(id, PluginApi::UnsupportedOperation, upload->file->errorString());
            return;
        }
        upload->received += decoded.decoded.size();
        Respond(id, QJsonObject {
            { QStringLiteral("upload_id"), upload_id },
            { QStringLiteral("received"), upload->received }
        });
    } else if (method == QStringLiteral("input.finishEpub")) {
        const QString upload_id = params.value(QStringLiteral("upload_id")).toString();
        const QString expected_sha256 = params.value(QStringLiteral("sha256")).toString().toLower();
        auto upload = m_InputUploads.find(upload_id);
        if (upload == m_InputUploads.end()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Input upload not found"));
            return;
        }
        if (expected_sha256.size() != 64
            || std::any_of(expected_sha256.cbegin(), expected_sha256.cend(),
                           [](QChar value) { return !value.isDigit()
                               && (value < QLatin1Char('a') || value > QLatin1Char('f')); })) {
            RespondError(id, -32602,
                         QStringLiteral("sha256 must contain 64 hexadecimal characters"));
            return;
        }
        if ((upload->expectedSize >= 0 && upload->received != upload->expectedSize)
            || upload->received < 4 || !upload->file->flush() || !upload->file->seek(0)) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Input EPUB length does not match or could not be finalized"));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray signature;
        while (!upload->file->atEnd()) {
            const QByteArray chunk = upload->file->read(DEFAULT_BINARY_CHUNK_SIZE);
            if (signature.size() < 4) signature.append(chunk.left(4 - signature.size()));
            if (chunk.isEmpty() && upload->file->error() != QFile::NoError) {
                RespondError(id, PluginApi::UnsupportedOperation, upload->file->errorString());
                return;
            }
            hash.addData(chunk);
        }
        const QString actual_sha256 = QString::fromLatin1(hash.result().toHex());
        if (actual_sha256 != expected_sha256 || !signature.startsWith("PK")) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Input EPUB hash or ZIP signature is invalid"));
            return;
        }
        upload->file->close();
        QString validation_error;
        if (!PluginApi::ValidateInputEpub(upload->file->fileName(), &validation_error)) {
            QTemporaryFile *rejected = upload->file;
            m_InputUploads.erase(upload);
            delete rejected;
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Input EPUB validation failed: %1").arg(validation_error));
            return;
        }
        if (m_InputEpubFile) delete m_InputEpubFile;
        m_InputEpubFile = upload->file;
        const qint64 size = upload->received;
        const QString original_filename = upload->filename;
        m_InputUploads.erase(upload);
        Respond(id, QJsonObject {
            { QStringLiteral("accepted"), true },
            { QStringLiteral("filename"), original_filename },
            { QStringLiteral("size"), size },
            { QStringLiteral("sha256"), actual_sha256 }
        });
    } else if (method == QStringLiteral("output.exportEpub")) {
        if (m_Plugin.get_type() != QStringLiteral("output")) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Only output plugins may export an EPUB"));
            return;
        }
        QString path = params.value(QStringLiteral("path")).toString();
        const QString current_file = m_MainWindow->GetCurrentFilePath();
        if (path.isEmpty()) path = current_file;
        const QFileInfo target(path);
        const QFileInfo current(current_file);
        const QString target_path = target.absoluteFilePath();
        const QString current_path = current.absoluteFilePath();
        const QString target_identity = target.exists() ? target.canonicalFilePath() : target_path;
        const QString current_identity = current.exists() ? current.canonicalFilePath() : current_path;
        if (path.isEmpty() || !target.isAbsolute()
            || target.suffix().compare(QStringLiteral("epub"), Qt::CaseInsensitive) != 0) {
            RespondError(id, -32602,
                         QStringLiteral("An absolute .epub path or a saved source Book is required"));
            return;
        }
        QScopedValueRollback<bool> modal_scope(m_InRequest, false);
        const bool saving_source = !current_file.isEmpty() && target_identity == current_identity;
        const bool exported = saving_source
            ? m_MainWindow->SaveCurrentBook() : m_MainWindow->ExportCurrentBookCopy(target_path);
        Respond(id, QJsonObject {
            { QStringLiteral("exported"), exported },
            { QStringLiteral("path"), target_path },
            { QStringLiteral("mode"), saving_source ? QStringLiteral("source")
                                                     : QStringLiteral("copy") }
        });
    } else if (method == QStringLiteral("resource.readMany")) {
        const QJsonArray ids = params.value(QStringLiteral("resource_ids")).toArray();
        if (ids.size() > 100) {
            RespondError(id, PluginApi::PayloadTooLarge, QStringLiteral("At most 100 resources may be read"));
            return;
        }
        bool cursor_ok = false;
        const int offset = params.value(QStringLiteral("cursor")).isUndefined()
            ? 0 : params.value(QStringLiteral("cursor")).toString().toInt(&cursor_ok);
        if (offset < 0 || offset > ids.size()
            || (!params.value(QStringLiteral("cursor")).isUndefined() && !cursor_ok)) {
            RespondError(id, -32602, QStringLiteral("readMany cursor is invalid"));
            return;
        }
        QJsonArray items;
        qsizetype response_size = 128;
        int next_offset = ids.size();
        for (int index = offset; index < ids.size(); ++index) {
            const QJsonValue value = ids.at(index);
            TextResource *resource = ResolveTextResource(value.toString());
            if (!resource) continue;
            resource->InitialLoad();
            const QJsonObject item {
                { QStringLiteral("resource_id"), resource->GetIdentifier() },
                { QStringLiteral("text"), resource->GetText() },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            };
            const qsizetype item_size = QJsonDocument(item).toJson(QJsonDocument::Compact).size();
            if (response_size + item_size > MAX_READ_MANY_RESPONSE_SIZE) {
                if (items.isEmpty()) {
                    RespondError(id, PluginApi::PayloadTooLarge,
                                 QStringLiteral("A text resource exceeds the readMany response limit"));
                    return;
                }
                next_offset = index;
                break;
            }
            items.append(item);
            response_size += item_size;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("items"), items },
            { QStringLiteral("next_cursor"), next_offset < ids.size()
                ? QJsonValue(QString::number(next_offset)) : QJsonValue() }
        });
    } else if (method == QStringLiteral("transaction.begin")) {
        if (m_Transaction) {
            RespondError(id, PluginApi::Busy, QStringLiteral("A transaction is already active"));
            return;
        }
        if (!AcquireWriter()) {
            RespondError(id, PluginApi::Busy,
                         QStringLiteral("Another live plugin has an active write transaction"));
            return;
        }
        const QString visibility = params.value(QStringLiteral("visibility"))
            .toString(QStringLiteral("staged"));
        const QString checkpoint = params.value(QStringLiteral("checkpoint"))
            .toString(QStringLiteral("auto"));
        if (visibility != QStringLiteral("staged")
            || (checkpoint != QStringLiteral("none")
                && checkpoint != QStringLiteral("auto")
                && checkpoint != QStringLiteral("required"))) {
            ReleaseWriter();
            RespondError(id, -32602, QStringLiteral("Invalid transaction policy"));
            return;
        }
        const QString transaction_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_Transaction = std::make_unique<PluginApi::TextTransaction>(
            transaction_id,
            params.value(QStringLiteral("label")).toString(QStringLiteral("Plugin changes")),
            checkpoint, m_BookRevision);
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("base_book_revision"), static_cast<qint64>(m_BookRevision) },
            { QStringLiteral("visibility"), QStringLiteral("staged") },
            { QStringLiteral("checkpoint"), checkpoint }
        });
    } else if (method == QStringLiteral("transaction.readText")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
            return;
        }
        resource->InitialLoad();
        quint64 revision = 0;
        const QString text = transaction->ReadText(resource->GetIdentifier(), resource->GetText(),
                                                   Revision(resource), &revision);
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("text"), text },
            { QStringLiteral("revision"), static_cast<qint64>(revision) },
            { QStringLiteral("staged"), transaction->HasChange(resource->GetIdentifier()) }
        });
    } else if (method == QStringLiteral("transaction.readBinary")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        QByteArray current_data;
        QString read_error;
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        }
        if (!ReadBinaryFile(resource, &current_data, &read_error)) {
            const int code = read_error.contains(QStringLiteral("size limit"))
                ? PluginApi::PayloadTooLarge : PluginApi::UnsupportedOperation;
            RespondError(id, code, read_error);
            return;
        }
        quint64 revision = 0;
        const QByteArray data = transaction->ReadBinary(resource->GetIdentifier(), current_data,
                                                        Revision(resource), &revision);
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("data_base64"), QString::fromLatin1(data.toBase64()) },
            { QStringLiteral("revision"), static_cast<qint64>(revision) },
            { QStringLiteral("staged"), transaction->HasBinaryChange(resource->GetIdentifier()) }
        });
    } else if (method == QStringLiteral("transaction.writeBinary")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        }
        quint64 expected_revision = 0;
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || !params.value(QStringLiteral("data_base64")).isString()) {
            RespondError(id, -32602,
                         QStringLiteral("Expected revision and base64 data are required"));
            return;
        }
        const QByteArray encoded = params.value(QStringLiteral("data_base64")).toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(
            encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
            RespondError(id, -32602, QStringLiteral("data_base64 is invalid"));
            return;
        }
        if (decoded.decoded.size() > MAX_INLINE_BINARY_SIZE) {
            RespondError(id, PluginApi::PayloadTooLarge,
                         QStringLiteral("Binary resource exceeds the inline size limit"));
            return;
        }
        QByteArray current_data;
        QString read_error;
        if (!ReadBinaryFile(resource, &current_data, &read_error)) {
            RespondError(id, PluginApi::UnsupportedOperation, read_error);
            return;
        }
        quint64 required_revision = 0;
        transaction->ReadBinary(resource->GetIdentifier(), current_data, Revision(resource),
                                &required_revision);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), static_cast<qint64>(expected_revision) },
                             { QStringLiteral("actual"), static_cast<qint64>(required_revision) },
                             { QStringLiteral("resource_id"), resource->GetIdentifier() }
                         });
            return;
        }
        QString stage_error;
        if (!transaction->ReplaceBinary(resource->GetIdentifier(), current_data, Revision(resource),
                                        expected_revision, decoded.decoded, &stage_error)) {
            RespondError(id, PluginApi::RevisionConflict, stage_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("base_revision"), static_cast<qint64>(required_revision) },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.writeBinaryBegin")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        quint64 expected_revision = 0;
        const qint64 expected_size = params.value(QStringLiteral("size")).toInteger(-1);
        if (!resource || qobject_cast<TextResource *>(resource)) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Binary resource not found"));
            return;
        }
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || expected_size < 0 || expected_size > MAX_TRANSACTION_BINARY_SIZE) {
            RespondError(id, -32602,
                         QStringLiteral("A valid expected_revision and size are required"));
            return;
        }
        QByteArray current_data;
        QString read_error;
        if (!ReadBinaryFile(resource, &current_data, &read_error,
                            MAX_TRANSACTION_BINARY_SIZE)) {
            RespondError(id, PluginApi::PayloadTooLarge, read_error);
            return;
        }
        quint64 required_revision = 0;
        transaction->ReadBinary(resource->GetIdentifier(), current_data, Revision(resource),
                                &required_revision);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"));
            return;
        }
        auto *file = new QTemporaryFile(this);
        if (!file->open()) {
            const QString error = file->errorString();
            delete file;
            RespondError(id, PluginApi::UnsupportedOperation, error);
            return;
        }
        const QString upload_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        BinaryWriteUpload upload;
        upload.file = file;
        upload.transactionId = transaction->Id();
        upload.resourceId = resource->GetIdentifier();
        upload.expectedRevision = expected_revision;
        upload.expectedSize = expected_size;
        m_BinaryWriteUploads.insert(upload_id, upload);
        Respond(id, QJsonObject {
            { QStringLiteral("upload_id"), upload_id },
            { QStringLiteral("chunk_size"), static_cast<qint64>(DEFAULT_BINARY_CHUNK_SIZE) },
            { QStringLiteral("max_size"), MAX_TRANSACTION_BINARY_SIZE }
        });
    } else if (method == QStringLiteral("transaction.writeBinaryChunk")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString upload_id = params.value(QStringLiteral("upload_id")).toString();
        auto upload = m_BinaryWriteUploads.find(upload_id);
        if (upload == m_BinaryWriteUploads.end()
            || upload->transactionId != transaction->Id()) {
            RespondError(id, PluginApi::ResourceNotFound,
                         QStringLiteral("Binary write upload not found"));
            return;
        }
        const auto decoded = QByteArray::fromBase64Encoding(
            params.value(QStringLiteral("data_base64")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok
            || decoded.decoded.isEmpty() || decoded.decoded.size() > MAX_BINARY_CHUNK_SIZE) {
            RespondError(id, -32602, QStringLiteral("Binary chunk is empty, invalid, or too large"));
            return;
        }
        if (upload->received + decoded.decoded.size() > upload->expectedSize
            || upload->file->write(decoded.decoded) != decoded.decoded.size()) {
            RespondError(id, PluginApi::PayloadTooLarge,
                         QStringLiteral("Binary write exceeds its declared size"));
            return;
        }
        upload->received += decoded.decoded.size();
        Respond(id, QJsonObject {
            { QStringLiteral("upload_id"), upload_id },
            { QStringLiteral("received"), upload->received }
        });
    } else if (method == QStringLiteral("transaction.writeBinaryEnd")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString upload_id = params.value(QStringLiteral("upload_id")).toString();
        const QString expected_sha256 = params.value(QStringLiteral("sha256")).toString().toLower();
        auto upload = m_BinaryWriteUploads.find(upload_id);
        if (upload == m_BinaryWriteUploads.end()
            || upload->transactionId != transaction->Id()) {
            RespondError(id, PluginApi::ResourceNotFound,
                         QStringLiteral("Binary write upload not found"));
            return;
        }
        if (expected_sha256.size() != 64 || upload->received != upload->expectedSize
            || !upload->file->flush() || !upload->file->seek(0)) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Binary write length or hash is invalid"));
            return;
        }
        const QByteArray replacement = upload->file->readAll();
        const QString actual_sha256 = DataFingerprint(replacement);
        if (replacement.size() != upload->expectedSize || actual_sha256 != expected_sha256) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Binary write length or hash does not match"));
            return;
        }
        Resource *resource = ResolveResource(upload->resourceId);
        QByteArray current_data;
        QString read_error;
        if (!resource || !ReadBinaryFile(resource, &current_data, &read_error,
                                         MAX_TRANSACTION_BINARY_SIZE)) {
            RespondError(id, PluginApi::ResourceNotFound,
                         resource ? read_error : QStringLiteral("Resource not found"));
            return;
        }
        quint64 required_revision = 0;
        transaction->ReadBinary(resource->GetIdentifier(), current_data, Revision(resource),
                                &required_revision);
        if (required_revision != upload->expectedRevision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"));
            return;
        }
        QString stage_error;
        if (!transaction->ReplaceBinary(resource->GetIdentifier(), current_data, Revision(resource),
                                        upload->expectedRevision, replacement, &stage_error)) {
            RespondError(id, PluginApi::RevisionConflict, stage_error);
            return;
        }
        QTemporaryFile *completed = upload->file;
        m_BinaryWriteUploads.erase(upload);
        delete completed;
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("size"), static_cast<qint64>(replacement.size()) },
            { QStringLiteral("sha256"), actual_sha256 },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.replaceArchiveFile")
               || method == QStringLiteral("transaction.removeArchiveFile")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString book_path = params.value(QStringLiteral("book_path")).toString();
        const QString expected_fingerprint = params.value(QStringLiteral("expected_sha256"))
            .toString();
        FolderKeeper *folder_keeper = m_MainWindow->GetCurrentBook()->GetFolderKeeper();
        if (expected_fingerprint.isEmpty() || !IsCanonicalBookPath(book_path)) {
            RespondError(id, -32602, QStringLiteral("Canonical path and expected_sha256 are required"));
            return;
        }
        if (folder_keeper->GetResourceByBookPathNoThrow(book_path)) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Managed resources require the resource transaction API"));
            return;
        }
        if (IsProtectedArchivePath(book_path, m_MainWindow->GetCurrentBook()->GetOPF())) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Protected EPUB infrastructure cannot be modified"));
            return;
        }
        QByteArray current_data;
        QString current_fingerprint;
        QString archive_error;
        if (!ReadArchiveFile(folder_keeper, book_path, &current_data, &current_fingerprint,
                             &archive_error)) {
            RespondError(id, PluginApi::ResourceNotFound, archive_error);
            return;
        }
        bool staged = false;
        if (method == QStringLiteral("transaction.removeArchiveFile")) {
            staged = transaction->RemoveArchiveFile(
                book_path, current_data, current_fingerprint, expected_fingerprint,
                &archive_error);
        } else {
            QByteArray replacement;
            if (!DecodeResourcePayload(params, &replacement, &archive_error)) {
                const int code = archive_error.contains(QStringLiteral("size limit"))
                    ? PluginApi::PayloadTooLarge : -32602;
                RespondError(id, code, archive_error);
                return;
            }
            staged = transaction->ReplaceArchiveFile(
                book_path, current_data, current_fingerprint, expected_fingerprint,
                replacement, &archive_error);
        }
        if (!staged) {
            RespondError(id, PluginApi::RevisionConflict, archive_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("book_path"), book_path },
            { QStringLiteral("base_sha256"), current_fingerprint },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.addResource")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString book_path = params.value(QStringLiteral("book_path")).toString();
        const QString media_type = params.value(QStringLiteral("media_type")).toString();
        const bool manifested = params.value(QStringLiteral("manifested")).toBool(true);
        const QString manifest_id = params.value(QStringLiteral("manifest_id")).toString();
        if (!IsCanonicalBookPath(book_path) || media_type.isEmpty()
            || (manifested && manifest_id.isEmpty())
            || m_MainWindow->GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow(book_path)) {
            RespondError(id, -32602, QStringLiteral("Resource path, media type, or manifest ID is invalid"));
            return;
        }
        QByteArray data;
        QString payload_error;
        if (!DecodeResourcePayload(params, &data, &payload_error)) {
            const int code = payload_error.contains(QStringLiteral("size limit"))
                ? PluginApi::PayloadTooLarge : -32602;
            RespondError(id, code, payload_error);
            return;
        }
        PluginApi::StagedResourceAddition addition;
        addition.stagingId = QStringLiteral("new:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        addition.bookPath = book_path;
        addition.mediaType = media_type;
        addition.manifestId = manifest_id;
        addition.properties = params.value(QStringLiteral("properties")).toString();
        addition.fallback = params.value(QStringLiteral("fallback")).toString();
        addition.overlay = params.value(QStringLiteral("overlay")).toString();
        addition.data = data;
        addition.manifested = manifested;
        addition.addToSpine = params.value(QStringLiteral("add_to_spine")).toBool(true);
        QString stage_error;
        if (!transaction->AddResource(addition, &stage_error)) {
            RespondError(id, PluginApi::ValidationFailed, stage_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("staging_id"), addition.stagingId },
            { QStringLiteral("book_path"), addition.bookPath },
            { QStringLiteral("manifest_id"), addition.manifestId },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.removeResource")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        quint64 expected_revision = 0;
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        }
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || expected_revision != Revision(resource)) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), params.value(QStringLiteral("expected_revision")) },
                             { QStringLiteral("actual"), static_cast<qint64>(Revision(resource)) },
                             { QStringLiteral("resource_id"), resource->GetIdentifier() }
                         });
            return;
        }
        if (resource->Type() == Resource::OPFResourceType) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("The package document cannot be removed"));
            return;
        }
        QString stage_error;
        if (!transaction->RemoveResource(resource->GetIdentifier(), expected_revision, &stage_error)) {
            RespondError(id, PluginApi::ValidationFailed, stage_error);
            return;
        }
        Respond(id, QJsonObject {{ QStringLiteral("staged"), true }});
    } else if (method == QStringLiteral("transaction.moveResource")
               || method == QStringLiteral("transaction.renameResource")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        quint64 expected_revision = 0;
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        }
        if (resource->Type() == Resource::OPFResourceType) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("The package document cannot be relocated"));
            return;
        }
        QString target_path;
        if (method == QStringLiteral("transaction.renameResource")) {
            const QString filename = params.value(QStringLiteral("filename")).toString();
            if (filename.isEmpty() || filename.contains(QLatin1Char('/'))
                || filename.contains(QLatin1Char('\\'))) {
                RespondError(id, -32602, QStringLiteral("Filename is invalid"));
                return;
            }
            const QString folder = Utility::startingDir(resource->GetRelativePath());
            target_path = folder.isEmpty() ? filename : folder + QLatin1Char('/') + filename;
        } else {
            target_path = params.value(QStringLiteral("book_path")).toString();
        }
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || expected_revision != Revision(resource)) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"));
            return;
        }
        if (!IsCanonicalBookPath(target_path)
            || m_MainWindow->GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow(target_path)) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Relocation target is invalid or occupied"));
            return;
        }
        QString stage_error;
        if (!transaction->RelocateResource(resource->GetIdentifier(), resource->GetRelativePath(),
                                           target_path, expected_revision, &stage_error)) {
            RespondError(id, PluginApi::ValidationFailed, stage_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("book_path"), target_path },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.updateMetadata")
               || method == QStringLiteral("transaction.updateSpine")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        quint64 expected_revision = 0;
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || !params.value(QStringLiteral("items")).isArray()) {
            RespondError(id, -32602,
                         QStringLiteral("Expected revision and an items array are required"));
            return;
        }
        OPFResource *opf = m_MainWindow->GetCurrentBook()->GetOPF();
        opf->InitialLoad();
        const quint64 required_revision = transaction->HasPackageChange()
            ? transaction->PackageChange().baseRevision : Revision(opf);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"));
            return;
        }
        const QString source = transaction->HasPackageChange()
            ? transaction->PackageChange().stagedText : opf->GetText();
        QString replacement;
        QString package_error;
        const bool updated = method == QStringLiteral("transaction.updateMetadata")
            ? ApplyMetadataUpdate(source, params.value(QStringLiteral("items")).toArray(),
                                  &replacement, &package_error)
            : ApplySpineUpdate(source, params.value(QStringLiteral("items")).toArray(),
                               params.value(QStringLiteral("attributes")).toObject(),
                               &replacement, &package_error);
        PackageDocumentInfo package_info;
        if (!updated
            || !ParsePackageDocument(replacement, opf->GetRelativePath(), opf->GetEpubVersion(),
                                     &package_info, &package_error)) {
            RespondError(id, PluginApi::ValidationFailed, package_error);
            return;
        }
        if (!transaction->ReplacePackage(opf->GetIdentifier(), opf->GetText(), Revision(opf),
                                         expected_revision, replacement, &package_error)) {
            RespondError(id, PluginApi::RevisionConflict, package_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), opf->GetIdentifier() },
            { QStringLiteral("base_revision"), static_cast<qint64>(required_revision) },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.replacePackage")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        quint64 expected_revision = 0;
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)
            || !params.value(QStringLiteral("text")).isString()) {
            RespondError(id, -32602, QStringLiteral("Expected revision and package text are required"));
            return;
        }
        const QString replacement = params.value(QStringLiteral("text")).toString();
        if (replacement.toUtf8().size() > MAX_INLINE_BINARY_SIZE) {
            RespondError(id, PluginApi::PayloadTooLarge,
                         QStringLiteral("Package document exceeds the inline size limit"));
            return;
        }
        OPFResource *opf = m_MainWindow->GetCurrentBook()->GetOPF();
        opf->InitialLoad();
        const quint64 required_revision = transaction->HasPackageChange()
            ? transaction->PackageChange().baseRevision : Revision(opf);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), static_cast<qint64>(expected_revision) },
                             { QStringLiteral("actual"), static_cast<qint64>(required_revision) },
                             { QStringLiteral("resource_id"), opf->GetIdentifier() }
                         });
            return;
        }
        PackageDocumentInfo package_info;
        QString package_error;
        if (!ParsePackageDocument(replacement, opf->GetRelativePath(), opf->GetEpubVersion(),
                                  &package_info, &package_error)) {
            RespondError(id, PluginApi::ValidationFailed, package_error);
            return;
        }
        if (!transaction->ReplacePackage(opf->GetIdentifier(), opf->GetText(), Revision(opf),
                                         expected_revision, replacement, &package_error)) {
            RespondError(id, PluginApi::RevisionConflict, package_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), opf->GetIdentifier() },
            { QStringLiteral("base_revision"), static_cast<qint64>(required_revision) },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.replaceText")
               || method == QStringLiteral("transaction.applyTextEdits")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
            return;
        }
        if (resource->Type() == Resource::OPFResourceType) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("OPF changes require the structure transaction API"));
            return;
        }
        quint64 expected_revision = 0;
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)) {
            RespondError(id, -32602, QStringLiteral("expected_revision must be a non-negative integer"));
            return;
        }
        if (method == QStringLiteral("transaction.replaceText")
            && !params.value(QStringLiteral("text")).isString()) {
            RespondError(id, -32602, QStringLiteral("Text must be a string"));
            return;
        }
        resource->InitialLoad();
        quint64 required_revision = 0;
        transaction->ReadText(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                              &required_revision);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), static_cast<qint64>(expected_revision) },
                             { QStringLiteral("actual"), static_cast<qint64>(required_revision) },
                             { QStringLiteral("resource_id"), resource->GetIdentifier() }
                         });
            return;
        }
        QString stage_error;
        const bool staged = method == QStringLiteral("transaction.replaceText")
            ? transaction->ReplaceText(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                                       expected_revision, params.value(QStringLiteral("text")).toString(),
                                       &stage_error)
            : transaction->ApplyEdits(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                                      expected_revision, params.value(QStringLiteral("edits")).toArray(),
                                      &stage_error);
        if (!staged) {
            RespondError(id, PluginApi::InvalidPatch, stage_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("base_revision"), static_cast<qint64>(required_revision) },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.validate")
               || method == QStringLiteral("transaction.preview")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        QJsonArray conflicts;
        QJsonArray text_changes;
        QJsonArray binary_changes;
        QJsonArray structure_changes;
        QJsonArray opf_changes;
        int modified = 0;
        for (const PluginApi::StagedTextChange &change : transaction->Changes()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalText != change.stagedText) {
                ++modified;
                text_changes.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("before_length"), change.originalText.size() },
                    { QStringLiteral("after_length"), change.stagedText.size() }
                });
            }
        }
        for (const PluginApi::StagedBinaryChange &change : transaction->BinaryChanges()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalData != change.stagedData) {
                ++modified;
                binary_changes.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("before_size"), change.originalData.size() },
                    { QStringLiteral("after_size"), change.stagedData.size() }
                });
            }
        }
        for (const PluginApi::StagedResourceRemoval &removal : transaction->Removals()) {
            Resource *resource = ResolveResource(removal.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != removal.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), removal.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(removal.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            structure_changes.append(QJsonObject {
                { QStringLiteral("operation"), QStringLiteral("remove") },
                { QStringLiteral("resource_id"), removal.resourceId }
            });
        }
        for (const PluginApi::StagedResourceRelocation &relocation : transaction->Relocations()) {
            Resource *resource = ResolveResource(relocation.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != relocation.baseRevision
                || (m_MainWindow->GetCurrentBook()->GetFolderKeeper()
                    ->GetResourceByBookPathNoThrow(relocation.targetBookPath))) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), relocation.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(relocation.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            structure_changes.append(QJsonObject {
                { QStringLiteral("operation"), QStringLiteral("relocate") },
                { QStringLiteral("resource_id"), relocation.resourceId },
                { QStringLiteral("from"), relocation.originalBookPath },
                { QStringLiteral("to"), relocation.targetBookPath }
            });
        }
        for (const PluginApi::StagedResourceAddition &addition : transaction->Additions()) {
            if (m_MainWindow->GetCurrentBook()->GetFolderKeeper()
                ->GetResourceByBookPathNoThrow(addition.bookPath)) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("staging_id"), addition.stagingId },
                    { QStringLiteral("book_path"), addition.bookPath }
                });
            }
            structure_changes.append(QJsonObject {
                { QStringLiteral("operation"), QStringLiteral("add") },
                { QStringLiteral("staging_id"), addition.stagingId },
                { QStringLiteral("book_path"), addition.bookPath }
            });
        }
        int archive_deleted = 0;
        for (const PluginApi::StagedArchiveChange &change : transaction->ArchiveChanges()) {
            QByteArray current_data;
            QString actual_fingerprint;
            QString archive_error;
            if (!ReadArchiveFile(m_MainWindow->GetCurrentBook()->GetFolderKeeper(),
                                 change.bookPath, &current_data, &actual_fingerprint,
                                 &archive_error)
                || actual_fingerprint != change.baseFingerprint) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("book_path"), change.bookPath },
                    { QStringLiteral("expected_sha256"), change.baseFingerprint },
                    { QStringLiteral("actual_sha256"), actual_fingerprint },
                    { QStringLiteral("message"), archive_error }
                });
            }
            if (change.remove) ++archive_deleted;
            else if (change.originalData != change.stagedData) ++modified;
            structure_changes.append(QJsonObject {
                { QStringLiteral("operation"), change.remove
                    ? QStringLiteral("remove-archive-file")
                    : QStringLiteral("replace-archive-file") },
                { QStringLiteral("book_path"), change.bookPath },
                { QStringLiteral("before_size"), change.originalData.size() },
                { QStringLiteral("after_size"), change.remove ? 0 : change.stagedData.size() }
            });
        }
        if (transaction->HasPackageChange()) {
            const PluginApi::StagedPackageChange package = transaction->PackageChange();
            Resource *resource = ResolveResource(package.resourceId);
            QString package_error;
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != package.baseRevision
                || !ValidatePackageTransaction(
                    transaction, m_MainWindow->GetCurrentBook()->GetFolderKeeper(),
                    m_MainWindow->GetCurrentBook()->GetOPF(), &package_error)) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), package.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(package.baseRevision) },
                    { QStringLiteral("actual"), resource
                        ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() },
                    { QStringLiteral("message"), package_error }
                });
            }
            if (package.originalText != package.stagedText) {
                ++modified;
                opf_changes.append(QJsonObject {
                    { QStringLiteral("operation"), QStringLiteral("replace-package") },
                    { QStringLiteral("before_length"), package.originalText.size() },
                    { QStringLiteral("after_length"), package.stagedText.size() }
                });
            }
        }
        QJsonObject result {
            { QStringLiteral("transaction_id"), transaction->Id() },
            { QStringLiteral("valid"), conflicts.isEmpty() },
            { QStringLiteral("conflicts"), conflicts },
            { QStringLiteral("summary"), QJsonObject {
                { QStringLiteral("modified"), modified },
                { QStringLiteral("added"), transaction->Additions().size() },
                { QStringLiteral("deleted"), transaction->Removals().size() + archive_deleted },
                { QStringLiteral("renamed"), transaction->Relocations().size() }
            } }
        };
        if (method == QStringLiteral("transaction.preview")) {
            result.insert(QStringLiteral("text_changes"), text_changes);
            result.insert(QStringLiteral("binary_changes"), binary_changes);
            result.insert(QStringLiteral("structure_changes"), structure_changes);
            result.insert(QStringLiteral("opf_changes"), opf_changes);
            result.insert(QStringLiteral("warnings"), QJsonArray());
        }
        Respond(id, result);
    } else if (method == QStringLiteral("transaction.commit")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        QJsonArray conflicts;
        QList<PluginApi::StagedTextChange> dirty_changes;
        QList<PluginApi::StagedBinaryChange> dirty_binary_changes;
        QList<PluginApi::StagedResourceAddition> additions = transaction->Additions();
        QList<PluginApi::StagedResourceRemoval> removals = transaction->Removals();
        QList<PluginApi::StagedResourceRelocation> relocations = transaction->Relocations();
        QList<PluginApi::StagedArchiveChange> archive_changes = transaction->ArchiveChanges();
        const bool has_package_change = transaction->HasPackageChange();
        PluginApi::StagedPackageChange package_change;
        if (has_package_change) package_change = transaction->PackageChange();
        for (const PluginApi::StagedTextChange &change : transaction->Changes()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalText != change.stagedText) dirty_changes.append(change);
        }
        for (const PluginApi::StagedBinaryChange &change : transaction->BinaryChanges()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalData != change.stagedData) dirty_binary_changes.append(change);
        }
        FolderKeeper *folder_keeper = m_MainWindow->GetCurrentBook()->GetFolderKeeper();
        OPFResource *opf = m_MainWindow->GetCurrentBook()->GetOPF();
        QString package_error;
        if (has_package_change) {
            Resource *resource = ResolveResource(package_change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != package_change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), package_change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(package_change.baseRevision) },
                    { QStringLiteral("actual"), resource
                        ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            } else if (!ValidatePackageTransaction(transaction, folder_keeper, opf, &package_error)) {
                RespondError(id, PluginApi::ValidationFailed, package_error);
                return;
            }
        }
        QList<PluginApi::StagedArchiveChange> dirty_archive_changes;
        for (const PluginApi::StagedArchiveChange &change : archive_changes) {
            QByteArray current_data;
            QString actual_fingerprint;
            QString archive_error;
            if (!ReadArchiveFile(folder_keeper, change.bookPath, &current_data,
                                 &actual_fingerprint, &archive_error)
                || actual_fingerprint != change.baseFingerprint) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("book_path"), change.bookPath },
                    { QStringLiteral("expected_sha256"), change.baseFingerprint },
                    { QStringLiteral("actual_sha256"), actual_fingerprint }
                });
            }
            if (change.remove || change.originalData != change.stagedData) {
                dirty_archive_changes.append(change);
            }
        }
        QList<Resource *> removal_resources;
        int removed_html = 0;
        for (const PluginApi::StagedResourceRemoval &removal : removals) {
            Resource *resource = ResolveResource(removal.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != removal.baseRevision
                || transaction->HasChange(removal.resourceId)
                || transaction->HasBinaryChange(removal.resourceId)) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), removal.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(removal.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
                continue;
            }
            if (resource == m_MainWindow->GetCurrentBook()->GetOPF()->GetNavResource()) {
                RespondError(id, PluginApi::ValidationFailed,
                             QStringLiteral("The EPUB navigation document cannot be removed"));
                return;
            }
            if (resource->Type() == Resource::HTMLResourceType) ++removed_html;
            removal_resources.append(resource);
        }
        QList<Resource *> relocation_resources;
        QStringList relocation_targets;
        QHash<QString, Resource *> relocation_map;
        for (const PluginApi::StagedResourceRelocation &relocation : relocations) {
            Resource *resource = ResolveResource(relocation.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != relocation.baseRevision
                || folder_keeper->GetResourceByBookPathNoThrow(relocation.targetBookPath)
                || QFileInfo::exists(folder_keeper->GetFullPathToMainFolder()
                                     + QLatin1Char('/') + relocation.targetBookPath)) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), relocation.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(relocation.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
                continue;
            }
            relocation_resources.append(resource);
            relocation_targets.append(relocation.targetBookPath);
            relocation_map.insert(relocation.originalBookPath, resource);
        }
        int added_html = 0;
        for (const PluginApi::StagedResourceAddition &addition : additions) {
            if (folder_keeper->GetResourceByBookPathNoThrow(addition.bookPath)
                || QFileInfo::exists(folder_keeper->GetFullPathToMainFolder()
                                     + QLatin1Char('/') + addition.bookPath)) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("staging_id"), addition.stagingId },
                    { QStringLiteral("book_path"), addition.bookPath }
                });
            }
            if (addition.mediaType == QStringLiteral("application/xhtml+xml")
                || addition.mediaType == QStringLiteral("text/html")) {
                ++added_html;
            }
        }
        const int current_html = folder_keeper
            ->GetResourceListByType(Resource::HTMLResourceType).size();
        if (current_html - removed_html + added_html < 1) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("A Book must contain at least one XHTML resource"));
            return;
        }
        if (!relocations.isEmpty() && !dirty_changes.isEmpty()) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Relocation cannot be combined with staged text writes"));
            return;
        }
        if (!conflicts.isEmpty()) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Transaction has conflicts"),
                         QJsonObject {{ QStringLiteral("conflicts"), conflicts }});
            return;
        }
        const bool has_structure_changes = !additions.isEmpty() || !removals.isEmpty()
            || !relocations.isEmpty();
        bool safety_checkpoint_required = dirty_changes.size() + dirty_binary_changes.size() > 1
            || !dirty_binary_changes.isEmpty() || !dirty_archive_changes.isEmpty()
            || has_structure_changes || has_package_change;
        for (const PluginApi::StagedTextChange &change : dirty_changes) {
            Resource *resource = ResolveResource(change.resourceId);
            safety_checkpoint_required = safety_checkpoint_required || (resource &&
                (resource->Type() == Resource::OPFResourceType
                 || resource->Type() == Resource::NCXResourceType));
        }
        if (transaction->CheckpointPolicy() == QStringLiteral("none")
            && safety_checkpoint_required) {
            RespondError(id, PluginApi::TransactionRequired,
                         QStringLiteral("This transaction requires a checkpoint"));
            return;
        }
        const bool checkpoint_required = transaction->CheckpointPolicy() == QStringLiteral("required")
            || (transaction->CheckpointPolicy() == QStringLiteral("auto")
                && safety_checkpoint_required);
        if (checkpoint_required && !m_MainWindow->RepoCommit()) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Could not create the required checkpoint"));
            return;
        }
        const bool book_was_modified = m_MainWindow->GetCurrentBook()->IsModified();
        QHash<QString, QString> text_snapshots;
        QSet<QString> snapshot_ids;
        for (const PluginApi::StagedTextChange &change : dirty_changes) {
            snapshot_ids.insert(change.resourceId);
        }
        if (has_structure_changes || has_package_change) {
            snapshot_ids.insert(opf->GetIdentifier());
        }
        for (Resource *resource : folder_keeper->GetResourceList()) {
            if (auto *text = qobject_cast<TextResource *>(resource)) {
                if (relocations.isEmpty() && !snapshot_ids.contains(resource->GetIdentifier())) {
                    continue;
                }
                text->InitialLoad();
                text_snapshots.insert(resource->GetIdentifier(), text->GetText());
            }
        }

        QJsonArray committed;
        QList<Resource *> added_resources;
        QList<PluginApi::StagedBinaryChange> applied_binary_changes;
        QList<PluginApi::StagedArchiveChange> applied_archive_changes;
        QList<ManifestResourceAddition> manifest_additions;
        auto rollback_applied_changes = [&]() {
            QStringList recovery_errors;
            folder_keeper->SuspendWatchingResources();

            for (auto it = applied_archive_changes.crbegin();
                 it != applied_archive_changes.crend(); ++it) {
                const QString path = folder_keeper->GetFullPathToMainFolder()
                    + QLatin1Char('/') + it->bookPath;
                QString restore_error;
                if (!WriteAtomicFile(path, it->originalData, &restore_error)) {
                    recovery_errors.append(QStringLiteral("archive %1: %2")
                                               .arg(it->bookPath, restore_error));
                }
            }
            for (auto it = applied_binary_changes.crbegin();
                 it != applied_binary_changes.crend(); ++it) {
                Resource *resource = ResolveResource(it->resourceId);
                QString restore_error;
                if (!resource || !WriteAtomicFile(resource->GetFullPath(), it->originalData,
                                                  &restore_error)) {
                    recovery_errors.append(QStringLiteral("binary %1: %2")
                                               .arg(it->resourceId, restore_error));
                } else {
                    resource->Modified();
                }
            }
            for (auto it = text_snapshots.cbegin(); it != text_snapshots.cend(); ++it) {
                TextResource *resource = ResolveTextResource(it.key());
                if (resource && resource->GetText() != it.value()) resource->SetText(it.value());
            }

            QList<Resource *> moved_resources;
            QStringList original_paths;
            for (const PluginApi::StagedResourceRelocation &relocation : relocations) {
                Resource *resource = ResolveResource(relocation.resourceId);
                if (resource && resource->GetRelativePath() != relocation.originalBookPath) {
                    moved_resources.append(resource);
                    original_paths.append(relocation.originalBookPath);
                }
            }
            if (!moved_resources.isEmpty()) {
                folder_keeper->BulkMoveResources(moved_resources, original_paths, false);
                for (Resource *resource : moved_resources) resource->SetCurrentBookRelPath(QString());
            }
            for (Resource *resource : std::as_const(added_resources)) {
                if (ResolveResource(resource->GetIdentifier())) {
                    folder_keeper->RemoveWithoutUpdatingOPF(resource);
                }
            }
            folder_keeper->ResumeWatchingResources();
            m_MainWindow->GetCurrentBook()->SetModified(book_was_modified);
            m_MainWindow->GetBookBrowser()->Refresh();
            return recovery_errors;
        };
        if (has_structure_changes) {
            folder_keeper->SuspendWatchingResources();
            QString structure_error;
            for (const PluginApi::StagedResourceAddition &addition : additions) {
                QTemporaryFile staged_file;
                if (!staged_file.open()
                    || staged_file.write(addition.data) != addition.data.size()
                    || !staged_file.flush()) {
                    structure_error = QStringLiteral("Could not materialize an added resource");
                    break;
                }
                staged_file.close();
                try {
                    Resource *resource = folder_keeper->AddContentFileToFolder(
                        staged_file.fileName(), false, addition.mediaType, addition.bookPath);
                    added_resources.append(resource);
                    TrackResource(resource);
                    if (addition.manifested) {
                        ManifestResourceAddition manifest_addition;
                        manifest_addition.resource = resource;
                        manifest_addition.manifestId = addition.manifestId;
                        manifest_addition.properties = addition.properties;
                        manifest_addition.fallback = addition.fallback;
                        manifest_addition.overlay = addition.overlay;
                        manifest_addition.addToSpine = addition.addToSpine;
                        manifest_additions.append(manifest_addition);
                    }
                } catch (...) {
                    structure_error = QStringLiteral("Could not add resource %1").arg(addition.bookPath);
                    break;
                }
            }
            if (structure_error.isEmpty() && !relocation_resources.isEmpty()) {
                folder_keeper->BulkMoveResources(relocation_resources, relocation_targets, false);
                for (int index = 0; index < relocation_resources.size(); ++index) {
                    Resource *resource = relocation_resources.at(index);
                    if (resource->GetRelativePath() != relocation_targets.at(index)) {
                        structure_error = QStringLiteral("Could not relocate resource %1")
                            .arg(resource->GetIdentifier());
                        break;
                    }
                    resource->SetCurrentBookRelPath(relocations.at(index).originalBookPath);
                    m_ResourceRevisions[resource->GetIdentifier()] += 1;
                    m_BookRevision += 1;
                }
            }
            if (structure_error.isEmpty() && !has_package_change
                && !m_MainWindow->GetCurrentBook()->GetOPF()->ApplyResourceBatch(
                    manifest_additions, removal_resources, relocation_map, &structure_error)) {
                if (structure_error.isEmpty()) {
                    structure_error = QStringLiteral("Could not update the package document");
                }
            }
            if (structure_error.isEmpty() && !relocations.isEmpty()) {
                QHash<QString, QString> path_updates;
                for (const PluginApi::StagedResourceRelocation &relocation : relocations) {
                    path_updates.insert(relocation.originalBookPath, relocation.targetBookPath);
                }
                QList<Resource *> update_resources = folder_keeper->GetResourceList();
                update_resources.removeOne(m_MainWindow->GetCurrentBook()->GetOPF());
                for (Resource *resource : removal_resources) update_resources.removeOne(resource);
                const QStringList update_errors = UniversalUpdates::PerformUniversalUpdates(
                    true, update_resources, path_updates);
                if (!update_errors.isEmpty()) structure_error = update_errors.join(QLatin1Char('\n'));
            }
            folder_keeper->ResumeWatchingResources();
            if (!structure_error.isEmpty()) {
                const QStringList recovery_errors = rollback_applied_changes();
                ClearBinaryWriteUploads();
                m_Transaction.reset();
                ReleaseWriter();
                QString message = structure_error;
                message += recovery_errors.isEmpty()
                    ? QStringLiteral("; all applied changes were rolled back")
                    : QStringLiteral("; rollback errors: ")
                        + recovery_errors.join(QStringLiteral("; "));
                RespondError(id, PluginApi::ValidationFailed, message);
                return;
            }
            for (Resource *resource : added_resources) {
                m_BookRevision += 1;
                committed.append(QJsonObject {
                    { QStringLiteral("resource_id"), resource->GetIdentifier() },
                    { QStringLiteral("book_path"), resource->GetRelativePath() },
                    { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
                });
            }
        }
        if (has_package_change && package_change.originalText != package_change.stagedText) {
            opf->SetText(package_change.stagedText);
            committed.append(QJsonObject {
                { QStringLiteral("resource_id"), package_change.resourceId },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(opf)) }
            });
        }
        for (const PluginApi::StagedTextChange &change : dirty_changes) {
            TextResource *resource = ResolveTextResource(change.resourceId);
            resource->SetText(change.stagedText);
            committed.append(QJsonObject {
                { QStringLiteral("resource_id"), change.resourceId },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
        if (!dirty_binary_changes.isEmpty()) {
            folder_keeper->SuspendWatchingResources();
            QString write_error;
            for (const PluginApi::StagedBinaryChange &change : dirty_binary_changes) {
                Resource *resource = ResolveResource(change.resourceId);
                {
                    QWriteLocker locker(&resource->GetLock());
                    if (!WriteAtomicFile(resource->GetFullPath(), change.stagedData, &write_error)) {
                        break;
                    }
                }
                applied_binary_changes.append(change);
                resource->Modified();
                committed.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
                });
            }
            folder_keeper->ResumeWatchingResources();
            if (!write_error.isEmpty()) {
                const QStringList recovery_errors = rollback_applied_changes();
                ClearBinaryWriteUploads();
                m_Transaction.reset();
                ReleaseWriter();
                RespondError(id, PluginApi::ValidationFailed,
                             QStringLiteral("Binary commit failed: %1; %2")
                                 .arg(write_error, recovery_errors.isEmpty()
                                     ? QStringLiteral("all applied changes were rolled back")
                                     : QStringLiteral("rollback errors: ")
                                         + recovery_errors.join(QStringLiteral("; "))));
                return;
            }
        }
        if (!dirty_archive_changes.isEmpty()) {
            folder_keeper->SuspendWatchingResources();
            QString archive_error;
            for (const PluginApi::StagedArchiveChange &change : dirty_archive_changes) {
                QString full_path;
                if (!ResolveArchiveFile(folder_keeper, change.bookPath, &full_path,
                                        &archive_error)) {
                    break;
                }
                if (change.remove) {
                    if (!QFile::remove(full_path)) {
                        archive_error = QStringLiteral("Could not remove archive file %1")
                            .arg(change.bookPath);
                        break;
                    }
                } else {
                    if (!WriteAtomicFile(full_path, change.stagedData, &archive_error)) break;
                }
                applied_archive_changes.append(change);
                committed.append(QJsonObject {
                    { QStringLiteral("book_path"), change.bookPath },
                    { QStringLiteral("removed"), change.remove },
                    { QStringLiteral("sha256"), change.remove
                        ? QJsonValue() : QJsonValue(DataFingerprint(change.stagedData)) }
                });
                m_BookRevision += 1;
            }
            folder_keeper->ResumeWatchingResources();
            if (!archive_error.isEmpty()) {
                const QStringList recovery_errors = rollback_applied_changes();
                ClearBinaryWriteUploads();
                m_Transaction.reset();
                ReleaseWriter();
                RespondError(id, PluginApi::ValidationFailed,
                             QStringLiteral("Archive commit failed: %1; %2")
                                 .arg(archive_error, recovery_errors.isEmpty()
                                     ? QStringLiteral("all applied changes were rolled back")
                                     : QStringLiteral("rollback errors: ")
                                         + recovery_errors.join(QStringLiteral("; "))));
                return;
            }
        }
        if (!removal_resources.isEmpty()) {
            folder_keeper->SuspendWatchingResources();
            for (Resource *resource : removal_resources) {
                m_TabManager->CloseTabForResource(resource, true);
                const QJsonObject removed_info {
                    { QStringLiteral("resource_id"), resource->GetIdentifier() },
                    { QStringLiteral("book_path"), resource->GetRelativePath() }
                };
                folder_keeper->RemoveWithoutUpdatingOPF(resource);
                if (m_Subscriptions.contains(QStringLiteral("book.resourceRemoved"))) {
                    Notify(QStringLiteral("book.resourceRemoved"), removed_info);
                }
            }
            folder_keeper->ResumeWatchingResources();
        }
        for (Resource *resource : std::as_const(added_resources)) {
            if (m_Subscriptions.contains(QStringLiteral("book.resourceAdded"))) {
                Notify(QStringLiteral("book.resourceAdded"), ResourceInfo(resource));
            }
        }
        if (has_structure_changes) m_MainWindow->GetBookBrowser()->Refresh();
        if (!dirty_changes.isEmpty() || !dirty_binary_changes.isEmpty() || has_structure_changes
            || !dirty_archive_changes.isEmpty()
            || (has_package_change && package_change.originalText != package_change.stagedText)) {
            m_MainWindow->GetCurrentBook()->SetModified();
        }
        const QString transaction_id = transaction->Id();
        const int archive_removed = static_cast<int>(std::count_if(
            dirty_archive_changes.cbegin(), dirty_archive_changes.cend(),
            [](const PluginApi::StagedArchiveChange &change) { return change.remove; }));
        ClearBinaryWriteUploads();
        m_Transaction.reset();
        ReleaseWriter();
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("committed"), committed },
            { QStringLiteral("modified"), dirty_changes.size() + dirty_binary_changes.size()
                + ((has_package_change && package_change.originalText != package_change.stagedText)
                    ? 1 : 0) + dirty_archive_changes.size() - archive_removed },
            { QStringLiteral("added"), additions.size() },
            { QStringLiteral("removed"), removals.size() + archive_removed },
            { QStringLiteral("relocated"), relocations.size() },
            { QStringLiteral("archive_files"), dirty_archive_changes.size() },
            { QStringLiteral("checkpoint_created"), checkpoint_required }
        });
    } else if (method == QStringLiteral("transaction.rollback")) {
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString transaction_id = transaction->Id();
        const int discarded = transaction->Size();
        ClearBinaryWriteUploads();
        m_Transaction.reset();
        ReleaseWriter();
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("rolled_back"), true },
            { QStringLiteral("discarded"), discarded }
        });
    } else if (method == QStringLiteral("editor.getState")
               || method == QStringLiteral("editor.getSelection")) {
        Respond(id, EditorState());
    } else if (method == QStringLiteral("editor.getOpenTabs")) {
        QJsonArray tabs;
        for (ContentTab *tab : m_TabManager->GetContentTabs()) {
            tabs.append(ResourceInfo(tab->GetLoadedResource()));
        }
        Respond(id, QJsonObject {{ QStringLiteral("items"), tabs }});
    } else if (method == QStringLiteral("editor.applyEdits")
               || method == QStringLiteral("editor.replaceSelection")
               || method == QStringLiteral("editor.insertText")) {
        ContentTab *tab = m_TabManager->GetCurrentContentTab();
        Resource *active_resource = tab ? tab->GetLoadedResource() : nullptr;
        const QString requested_id = params.value(QStringLiteral("resource_id")).toString();
        if (!active_resource || (!requested_id.isEmpty()
                                 && requested_id != active_resource->GetIdentifier())) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Editor writes require the active resource"));
            return;
        }
        auto *text_resource = qobject_cast<TextResource *>(active_resource);
        if (!text_resource) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("The active resource is not editable text"));
            return;
        }
        const QJsonValue expected_value = params.value(QStringLiteral("expected_revision"));
        const double expected_number = expected_value.toDouble(-1);
        const quint64 actual_revision = Revision(text_resource);
        if (!expected_value.isDouble() || expected_number < 0
            || std::floor(expected_number) != expected_number
            || expected_number > static_cast<double>(std::numeric_limits<qint64>::max())
            || static_cast<quint64>(expected_number) != actual_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), expected_value },
                             { QStringLiteral("actual"), static_cast<qint64>(actual_revision) },
                             { QStringLiteral("resource_id"), active_resource->GetIdentifier() }
                         });
            return;
        }

        QJsonArray edit_values;
        if (method == QStringLiteral("editor.applyEdits")) {
            edit_values = params.value(QStringLiteral("edits")).toArray();
        } else {
            if (!params.value(QStringLiteral("text")).isString()) {
                RespondError(id, -32602, QStringLiteral("Text must be a string"));
                return;
            }
            const int position = method == QStringLiteral("editor.insertText")
                ? tab->GetCursorPosition() : tab->GetSelectionStart();
            const int end = method == QStringLiteral("editor.insertText")
                ? position : tab->GetSelectionEnd();
            edit_values.append(QJsonObject {
                { QStringLiteral("start"), position },
                { QStringLiteral("end"), end },
                { QStringLiteral("text"), params.value(QStringLiteral("text")) }
            });
        }

        text_resource->InitialLoad();
        const QString current_text = text_resource->GetText();
        QList<PluginApi::TextEdit> edits;
        QString patch_error;
        if (!PluginApi::ParseTextEdits(edit_values, current_text, &edits, &patch_error)) {
            RespondError(id, PluginApi::InvalidPatch, patch_error);
            return;
        }
        {
            QWriteLocker locker(&text_resource->GetLock());
            PluginApi::ApplyTextEdits(&text_resource->GetTextDocumentForWriting(), edits);
        }
        m_MainWindow->GetCurrentBook()->SetModified();
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), active_resource->GetIdentifier() },
            { QStringLiteral("revision"), static_cast<qint64>(Revision(active_resource)) },
            { QStringLiteral("applied_edits"), edits.size() }
        });
    } else if (method == QStringLiteral("editor.openResource")
               || method == QStringLiteral("editor.revealRange")) {
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
            return;
        }
        int start = 0;
        int end = 0;
        if (method == QStringLiteral("editor.revealRange")
            && (!ReadPosition(params, QStringLiteral("start"), &start)
                || !ReadPosition(params, QStringLiteral("end"), &end))) {
            RespondError(id, -32602, QStringLiteral("A valid UTF-16 range is required"));
            return;
        }
        int position = -1;
        if (method == QStringLiteral("editor.openResource")
            && params.contains(QStringLiteral("position"))
            && !ReadPosition(params, QStringLiteral("position"), &position)) {
            RespondError(id, -32602, QStringLiteral("Position is invalid"));
            return;
        }
        m_MainWindow->OpenResourceAndWaitUntilLoaded(resource, -1, position);
        if (method == QStringLiteral("editor.revealRange")) {
            ContentTab *tab = m_TabManager->GetCurrentContentTab();
            if (!tab || tab->GetLoadedResource() != resource
                || !tab->SetSelectionRange(start, end)) {
                RespondError(id, PluginApi::InvalidPatch,
                             QStringLiteral("Selection range is invalid for the opened resource"));
                return;
            }
        }
        Respond(id, EditorState());
    } else if (method == QStringLiteral("ui.showStatus")) {
        const QString message = params.value(QStringLiteral("message")).toString();
        const int duration = params.value(QStringLiteral("duration_ms")).toInt(5000);
        if (message.isEmpty() || message.size() > 4096 || duration < 0 || duration > 60000) {
            RespondError(id, -32602, QStringLiteral("Status message or duration is invalid"));
            return;
        }
        m_MainWindow->statusBar()->showMessage(message, duration);
        Respond(id, QJsonObject {{ QStringLiteral("shown"), true }});
    } else if (method == QStringLiteral("ui.showMessage")) {
        const QString title = params.value(QStringLiteral("title"))
            .toString(m_Plugin.get_name());
        const QString message = params.value(QStringLiteral("message")).toString();
        const QString level = params.value(QStringLiteral("level")).toString(QStringLiteral("info"));
        if (title.size() > 256 || message.isEmpty() || message.size() > 65536
            || (level != QStringLiteral("info") && level != QStringLiteral("warning")
                && level != QStringLiteral("error"))) {
            RespondError(id, -32602, QStringLiteral("Message parameters are invalid"));
            return;
        }
        QScopedValueRollback<bool> modal_scope(m_InRequest, false);
        if (level == QStringLiteral("warning")) {
            QMessageBox::warning(m_MainWindow, title, message);
        } else if (level == QStringLiteral("error")) {
            QMessageBox::critical(m_MainWindow, title, message);
        } else {
            QMessageBox::information(m_MainWindow, title, message);
        }
        Respond(id, QJsonObject {{ QStringLiteral("shown"), true }});
    } else if (method == QStringLiteral("ui.confirm")) {
        const QString title = params.value(QStringLiteral("title"))
            .toString(m_Plugin.get_name());
        const QString message = params.value(QStringLiteral("message")).toString();
        if (title.size() > 256 || message.isEmpty() || message.size() > 65536) {
            RespondError(id, -32602, QStringLiteral("Confirmation parameters are invalid"));
            return;
        }
        QScopedValueRollback<bool> modal_scope(m_InRequest, false);
        const bool confirmed = QMessageBox::question(
            m_MainWindow, title, message, QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) == QMessageBox::Yes;
        Respond(id, QJsonObject {{ QStringLiteral("confirmed"), confirmed }});
    } else if (method == QStringLiteral("ui.chooseOpenFile")
               || method == QStringLiteral("ui.chooseSaveFile")) {
        const QString title = params.value(QStringLiteral("title"))
            .toString(m_Plugin.get_name());
        const QString filter = params.value(QStringLiteral("filter"))
            .toString(tr("All Files (*)"));
        const QString suggested_name = params.value(QStringLiteral("suggested_name")).toString();
        if (title.size() > 256 || filter.size() > 2048
            || suggested_name.size() > 255
            || (!suggested_name.isEmpty()
                && QFileInfo(suggested_name).fileName() != suggested_name)) {
            RespondError(id, -32602, QStringLiteral("File dialog parameters are invalid"));
            return;
        }
        const QString current_path = m_MainWindow->GetCurrentFilePath();
        const QString directory = current_path.isEmpty()
            ? QDir::homePath() : QFileInfo(current_path).absolutePath();
        const QString initial_path = suggested_name.isEmpty()
            ? directory : QDir(directory).filePath(suggested_name);
        QScopedValueRollback<bool> modal_scope(m_InRequest, false);
        const QString path = method == QStringLiteral("ui.chooseOpenFile")
            ? QFileDialog::getOpenFileName(m_MainWindow, title, initial_path, filter)
            : QFileDialog::getSaveFileName(m_MainWindow, title, initial_path, filter);
        Respond(id, QJsonObject {
            { QStringLiteral("path"), path.isEmpty() ? QJsonValue() : QJsonValue(path) }
        });
    } else if (method == QStringLiteral("ui.progressBegin")) {
        if (!m_ProgressId.isEmpty()) {
            RespondError(id, PluginApi::Busy, QStringLiteral("A progress operation is already active"));
            return;
        }
        const QString label = params.value(QStringLiteral("label")).toString();
        const int maximum = params.value(QStringLiteral("total")).toInt(0);
        if (label.isEmpty() || label.size() > 1024 || maximum < 0 || maximum > 1000000000) {
            RespondError(id, -32602, QStringLiteral("Progress label or total is invalid"));
            return;
        }
        m_ProgressId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_ProgressLabel = label;
        m_ProgressMaximum = maximum;
        if (m_Console) m_Console->SetProgress(label, 0, maximum);
        Respond(id, QJsonObject {
            { QStringLiteral("progress_id"), m_ProgressId },
            { QStringLiteral("total"), maximum }
        });
    } else if (method == QStringLiteral("ui.progressUpdate")) {
        if (params.value(QStringLiteral("progress_id")).toString() != m_ProgressId
            || m_ProgressId.isEmpty()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Progress operation not found"));
            return;
        }
        const int value = params.value(QStringLiteral("value")).toInt(-1);
        const QString label = params.value(QStringLiteral("label")).toString();
        if (value < 0 || (m_ProgressMaximum > 0 && value > m_ProgressMaximum)
            || label.size() > 1024) {
            RespondError(id, -32602, QStringLiteral("Progress value or label is invalid"));
            return;
        }
        if (m_Console) {
            if (!label.isEmpty()) m_ProgressLabel = label;
            m_Console->SetProgress(
                m_ProgressLabel, value, m_ProgressMaximum);
        }
        Respond(id, QJsonObject {{ QStringLiteral("updated"), true }});
    } else if (method == QStringLiteral("ui.progressEnd")) {
        if (params.value(QStringLiteral("progress_id")).toString() != m_ProgressId
            || m_ProgressId.isEmpty()) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Progress operation not found"));
            return;
        }
        m_ProgressId.clear();
        m_ProgressLabel.clear();
        m_ProgressMaximum = 0;
        if (m_Console) m_Console->ClearProgress();
        Respond(id, QJsonObject {{ QStringLiteral("ended"), true }});
    } else if (method == QStringLiteral("editor.setCursor")
               || method == QStringLiteral("editor.setSelection")) {
        ContentTab *tab = m_TabManager->GetCurrentContentTab();
        Resource *active_resource = tab ? tab->GetLoadedResource() : nullptr;
        const QString requested_id = params.value(QStringLiteral("resource_id")).toString();
        if (!active_resource || (!requested_id.isEmpty()
                                 && requested_id != active_resource->GetIdentifier())) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Editor navigation requires the active resource"));
            return;
        }
        int start = 0;
        int end = 0;
        const bool positions_valid = method == QStringLiteral("editor.setCursor")
            ? ReadPosition(params, QStringLiteral("position"), &start)
            : ReadPosition(params, QStringLiteral("start"), &start)
                && ReadPosition(params, QStringLiteral("end"), &end);
        if (method == QStringLiteral("editor.setCursor")) end = start;
        if (!positions_valid || !tab->SetSelectionRange(start, end)) {
            RespondError(id, PluginApi::InvalidPatch, QStringLiteral("Selection range is invalid"));
            return;
        }
        Respond(id, EditorState());
    } else {
        RespondError(id, -32601, QStringLiteral("Method not found"));
    }
}

void PluginSession::Respond(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isUndefined() || !m_Socket) return;
    QByteArray frame = PluginApi::EncodeFrame(PluginApi::MakeResult(id, result));
    if (frame.size() - static_cast<qsizetype>(sizeof(quint32))
        > PluginApi::DEFAULT_MAX_MESSAGE_SIZE) {
        frame = PluginApi::EncodeFrame(PluginApi::MakeError(
            id, PluginApi::PayloadTooLarge, QStringLiteral("Response exceeds the message limit")));
    }
    m_Socket->write(frame);
    m_Socket->flush();
}

void PluginSession::RespondError(const QJsonValue &id, int code, const QString &message,
                                 const QJsonValue &data)
{
    if (!m_Socket) return;
    m_Socket->write(PluginApi::EncodeFrame(PluginApi::MakeError(id, code, message, data)));
    m_Socket->flush();
}

void PluginSession::Notify(const QString &method, QJsonObject params)
{
    if (!m_Authenticated || !m_Socket || m_Ending) return;
    params.insert(QStringLiteral("origin_session_id"), m_InRequest
        ? QJsonValue(m_SessionId.toString(QUuid::WithoutBraces)) : QJsonValue());
    params.insert(QStringLiteral("book_revision"), static_cast<qint64>(m_BookRevision));
    m_Socket->write(PluginApi::EncodeFrame(QJsonObject {
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), method },
        { QStringLiteral("params"), params }
    }));
    m_Socket->flush();
}

QJsonObject PluginSession::ResourceInfo(Resource *resource) const
{
    auto *text = qobject_cast<TextResource *>(resource);
    return QJsonObject {
        { QStringLiteral("resource_id"), resource->GetIdentifier() },
        { QStringLiteral("book_path"), resource->GetRelativePath() },
        { QStringLiteral("media_type"), resource->GetMediaType() },
        { QStringLiteral("resource_type"), ResourceTypeName(resource->Type()) },
        { QStringLiteral("content_revision"), static_cast<qint64>(Revision(resource)) },
        { QStringLiteral("loaded"), text ? text->IsLoaded() : true }
    };
}

Resource *PluginSession::ResolveResource(const QString &resource_id) const
{
    return m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceByIdentifier(resource_id);
}

TextResource *PluginSession::ResolveTextResource(const QString &resource_id) const
{
    return qobject_cast<TextResource *>(ResolveResource(resource_id));
}

void PluginSession::TrackResource(Resource *resource)
{
    const QString resource_id = resource->GetIdentifier();
    m_ResourceRevisions.insert(resource_id, 1);
    connect(resource, &Resource::Modified, this, [this, resource_id]() {
        m_ResourceRevisions[resource_id] += 1;
        m_BookRevision += 1;
        if (m_Subscriptions.contains(QStringLiteral("book.resourceChanged"))) {
            Resource *changed = ResolveResource(resource_id);
            if (changed) Notify(QStringLiteral("book.resourceChanged"), ResourceInfo(changed));
        }
        ContentTab *tab = m_TabManager->GetCurrentContentTab();
        if (tab && tab->GetLoadedResource()->GetIdentifier() == resource_id
            && m_Subscriptions.contains(QStringLiteral("editor.contentChanged"))) {
            Notify(QStringLiteral("editor.contentChanged"), EditorEventState());
        }
    });
    connect(resource, &Resource::Renamed, this,
            [this, resource_id](const Resource *, const QString &) {
        m_ResourceRevisions[resource_id] += 1;
        m_BookRevision += 1;
    });
    connect(resource, &Resource::Moved, this,
            [this, resource_id](const Resource *, const QString &) {
        m_ResourceRevisions[resource_id] += 1;
        m_BookRevision += 1;
    });
    connect(resource, &Resource::Deleted, this,
            [this, resource_id](const Resource *) {
        m_ResourceRevisions.remove(resource_id);
        m_BookRevision += 1;
    });
}

void PluginSession::TrackEditorTab(ContentTab *tab)
{
    if (!tab || m_TrackedEditorTabs.contains(tab)) return;
    m_TrackedEditorTabs.insert(tab);
    connect(tab, &QObject::destroyed, this, [this, tab]() {
        m_TrackedEditorTabs.remove(tab);
    });
    connect(tab, &ContentTab::UpdateCursorPosition, this,
            [this, tab](int, int, int) {
        if (tab == m_TabManager->GetCurrentContentTab()
            && m_Subscriptions.contains(QStringLiteral("editor.cursorChanged"))) {
            Notify(QStringLiteral("editor.cursorChanged"), EditorEventState());
        }
    });
    if (FlowTab *flow = qobject_cast<FlowTab *>(tab)) {
        connect(flow, &FlowTab::SelectionChanged, this, [this, flow]() {
            if (flow == m_TabManager->GetCurrentContentTab()
                && m_Subscriptions.contains(QStringLiteral("editor.selectionChanged"))) {
                Notify(QStringLiteral("editor.selectionChanged"), EditorEventState());
            }
        });
    }
}

PluginApi::TextTransaction *PluginSession::RequireTransaction(const QJsonObject &params,
                                                              const QJsonValue &request_id)
{
    const QString transaction_id = params.value(QStringLiteral("transaction_id")).toString();
    if (!m_Transaction || transaction_id.isEmpty()
        || transaction_id != m_Transaction->Id()) {
        RespondError(request_id, PluginApi::TransactionNotFound,
                     QStringLiteral("Transaction not found"));
        return nullptr;
    }
    return m_Transaction.get();
}

bool PluginSession::AcquireWriter()
{
    auto *manager = qobject_cast<PluginSessionManager *>(parent());
    if (!manager || !manager->AcquireWriter(m_SessionId)) return false;
    m_HoldsWriter = true;
    return true;
}

void PluginSession::ReleaseWriter()
{
    if (!m_HoldsWriter) return;
    if (auto *manager = qobject_cast<PluginSessionManager *>(parent())) {
        manager->ReleaseWriter(m_SessionId);
    }
    m_HoldsWriter = false;
}

void PluginSession::ClearBinaryWriteUploads()
{
    for (const BinaryWriteUpload &upload : std::as_const(m_BinaryWriteUploads)) {
        delete upload.file;
    }
    m_BinaryWriteUploads.clear();
}

quint64 PluginSession::Revision(Resource *resource) const
{
    return m_ResourceRevisions.value(resource->GetIdentifier(), 1);
}

QJsonObject PluginSession::EditorState() const
{
    ContentTab *tab = m_TabManager->GetCurrentContentTab();
    if (!tab || !tab->GetLoadedResource()) {
        return QJsonObject {{ QStringLiteral("active"), false }};
    }
    Searchable *searchable = tab->GetSearchableContent();
    const int cursor = tab->GetCursorPosition();
    const int selection_start = tab->GetSelectionStart();
    const int selection_end = tab->GetSelectionEnd();
    return QJsonObject {
        { QStringLiteral("active"), true },
        { QStringLiteral("resource_id"), tab->GetLoadedResource()->GetIdentifier() },
        { QStringLiteral("book_path"), tab->GetLoadedResource()->GetRelativePath() },
        { QStringLiteral("revision"), static_cast<qint64>(Revision(tab->GetLoadedResource())) },
        { QStringLiteral("cursor"), cursor },
        { QStringLiteral("selection"), QJsonObject {
            { QStringLiteral("start"), selection_start },
            { QStringLiteral("end"), selection_end },
            { QStringLiteral("text"), searchable ? searchable->GetSelectedText() : QString() }
        } },
        { QStringLiteral("position_encoding"), QStringLiteral("utf-16") }
    };
}

QJsonObject PluginSession::EditorEventState() const
{
    QJsonObject state = EditorState();
    QJsonObject selection = state.value(QStringLiteral("selection")).toObject();
    selection.remove(QStringLiteral("text"));
    if (!selection.isEmpty()) state.insert(QStringLiteral("selection"), selection);
    return state;
}

QString PluginSession::ResolveInterpreter() const
{
    SettingsStore settings;
    const QString bundled = PluginDB::buildBundledInterpPath();
    if (settings.useBundledInterp() && !bundled.isEmpty()) {
        return bundled;
    }
    return PluginDB::instance()->get_engine_path(QStringLiteral("python3.4"));
}

void PluginSession::Finish(const QString &status, const QString &message)
{
    ReleaseWriter();
    m_Status = status;
    m_InputEpubAccepted = status == QStringLiteral("success") && m_InputEpubFile;
    m_Ending = true;
    if (!message.isEmpty() && m_Console) {
        m_Console->AppendOutput(message);
    }
    if (m_Console) {
        m_Console->SetStatus(tr("Status: %1").arg(status));
        m_Console->SetFinished();
    }
    CleanServer();
    if (!m_EndSignalScheduled) {
        m_EndSignalScheduled = true;
        QTimer::singleShot(0, this, &PluginSession::Ended);
    }
}

void PluginSession::CleanServer()
{
    ClearBinaryWriteUploads();
    for (const BinaryReadStream &stream : std::as_const(m_BinaryReadStreams)) {
        delete stream.file;
    }
    m_BinaryReadStreams.clear();
    for (const InputUpload &upload : std::as_const(m_InputUploads)) {
        delete upload.file;
    }
    m_InputUploads.clear();
    qDeleteAll(m_MaterializedFiles);
    m_MaterializedFiles.clear();
    delete m_MaterializationRoot;
    m_MaterializationRoot = nullptr;
    if (m_Socket) {
        m_Socket->disconnect(this);
        m_Socket->disconnectFromServer();
        m_Socket->deleteLater();
        m_Socket = nullptr;
    }
    if (m_Server) {
        m_Server->close();
        m_Server->deleteLater();
        m_Server = nullptr;
    }
    if (!m_ServerName.isEmpty()) {
        QLocalServer::removeServer(m_ServerName);
    }
}
