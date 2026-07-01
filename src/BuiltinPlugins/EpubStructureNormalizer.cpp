/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/EpubStructureNormalizer.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QPair>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Misc/HTMLEncodingResolver.h"
#include "Misc/MediaTypes.h"
#include "Misc/Utility.h"
#include "Parsers/OPFParser.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/SVGResource.h"
#include "ResourceObjects/XMLResource.h"
#include "SourceUpdates/UniversalUpdates.h"

namespace BuiltinPlugins
{

namespace
{

struct BookPathIndex {
    QHash<QString, QStringList> lowerToBookPaths;
    QSet<QString> exactBookPaths;
};

struct SourceLocation {
    int line = -1;
    int offset = -1;
};

struct IdScanResult {
    QSet<QString> ids;
    QList<QPair<QString, SourceLocation>> duplicateIds;
};

struct LinkScanContext {
    BookPathIndex index;
    QHash<QString, QString> updates;
    QHash<QString, QSet<QString>> idsByBookPath;
    QSet<QString> reported;
};

struct ManifestHrefInfo {
    QString rawHref;
    QString bookPath;
    QString key;
    bool isLocal = true;
    bool hasQuery = false;
    bool hasFragment = false;
    bool isDataUrl = false;
    bool isFileUrl = false;
};

static SourceLocation sourceLocationForOffset(const QString& source, int offset)
{
    SourceLocation location;
    if (offset < 0) {
        return location;
    }

    location.offset = offset;
    location.line = 1;
    const int bounded_offset = qMin(offset, source.size());
    for (int i = 0; i < bounded_offset; i++) {
        if (source.at(i) == QLatin1Char('\n')) {
            location.line++;
        }
    }
    return location;
}

static bool bookPathEscapesRoot(const QString& href_path, const QString& source_bookpath)
{
    QStringList stack = QFileInfo(source_bookpath).dir().path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList path_segments = href_path.split(QLatin1Char('/'), Qt::KeepEmptyParts);

    foreach(QString segment, path_segments) {
        if (segment.isEmpty() || segment == QLatin1String(".")) {
            continue;
        }
        if (segment == QLatin1String("..")) {
            if (stack.isEmpty()) {
                return true;
            }
            stack.removeLast();
        } else {
            stack << segment;
        }
    }

    return false;
}

static IdScanResult scanIds(const QString& source)
{
    IdScanResult result;
    QSet<QString> seen;
    QRegularExpression id_re(QStringLiteral("(^|[^A-Za-z0-9_:-])((?:xml:)?id)\\s*=\\s*([\"'])(.*?)\\3"),
                             QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = id_re.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        const QString id = match.captured(4);
        if (id.isEmpty()) {
            continue;
        }
        const SourceLocation location = sourceLocationForOffset(source, match.capturedStart(4));
        if (seen.contains(id)) {
            result.duplicateIds << qMakePair(id, location);
        } else {
            seen.insert(id);
            result.ids.insert(id);
        }
    }
    return result;
}

static bool shouldSkipFragmentIdCheck(const QString& fragment)
{
    const QString trimmed = fragment.trimmed();
    return trimmed.isEmpty() ||
           trimmed.startsWith(QStringLiteral("epubcfi("), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("svgView("), Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("xpointer("), Qt::CaseInsensitive);
}

static void addUniqueResult(EpubStructureNormalizer::Result& result,
                            LinkScanContext& context,
                            ValidationResult::ResType type,
                            const QString& source_bookpath,
                            const SourceLocation& location,
                            const QString& message)
{
    const QString key = QString::number(type) + "|" + source_bookpath + "|" +
                        QString::number(location.line) + "|" +
                        QString::number(location.offset) + "|" + message;
    if (context.reported.contains(key)) {
        return;
    }
    context.reported.insert(key);
    result.validationResults << ValidationResult(type, source_bookpath, location.line, location.offset, message);
}

static QString resolvedBookPath(const QString& raw_link, const QString& source_bookpath)
{
    QUrl url(raw_link.trimmed());
    QString href_path = Utility::URLDecodePath(url.path());
    if (href_path.isEmpty()) {
        return QString();
    }

    return Utility::buildBookPath(href_path, QFileInfo(source_bookpath).dir().path());
}

static void inspectLink(EpubStructureNormalizer* normalizer,
                        EpubStructureNormalizer::Result& result,
                        LinkScanContext& context,
                        const QString& source_bookpath,
                        const QString& raw_link,
                        const SourceLocation& location,
                        bool report_missing)
{
    Q_UNUSED(normalizer);

    const QString link = raw_link.trimmed();
    if (link.isEmpty() || link.startsWith("//")) {
        return;
    }

    QUrl url(link);
    if (link.startsWith("#")) {
        const QString fragment = url.fragment(QUrl::FullyDecoded);
        if (!shouldSkipFragmentIdCheck(fragment) &&
            context.idsByBookPath.contains(source_bookpath) &&
            !context.idsByBookPath.value(source_bookpath).contains(fragment)) {
            addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                            QStringLiteral("链接检查：fragment【#%1】在当前文件中找不到对应 id。").arg(fragment));
        }
        return;
    }

    if (link.startsWith("file:", Qt::CaseInsensitive)) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】使用 file: URL，建议改为 EPUB 内部相对路径。").arg(raw_link));
        return;
    }
    if (!url.isRelative()) {
        return;
    }
    if (link.contains(QLatin1Char('\\'))) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】包含反斜杠，建议使用 URL 规范的 / 路径分隔符。").arg(raw_link));
    }
    if (link.contains(QLatin1Char(' '))) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】包含未编码空格，建议使用 %20。").arg(raw_link));
    }
    QUrl strict_url(link, QUrl::StrictMode);
    if (!strict_url.isValid()) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】不是严格合法的 URL：%2。")
                            .arg(raw_link, strict_url.errorString()));
    }
    if (url.hasQuery()) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内相对链接【%1】包含 query，已跳过自动修正。").arg(raw_link));
        return;
    }

    const QString href_path = Utility::URLDecodePath(url.path());
    if (href_path.startsWith(QLatin1Char('/'))) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】使用根路径形式，EPUB 中建议改为包内相对路径。").arg(raw_link));
        return;
    }
    if (bookPathEscapesRoot(href_path, source_bookpath)) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("EPUBCheck：书内链接【%1】解析后会逃出 EPUB 根目录，已跳过自动修正。").arg(raw_link));
        return;
    }

    const QString target_bookpath = resolvedBookPath(raw_link, source_bookpath);
    if (target_bookpath.isEmpty()) {
        return;
    }

    const QString lower_target = target_bookpath.toLower();
    QString actual_bookpath = target_bookpath;
    if (!context.index.exactBookPaths.contains(target_bookpath)) {
        if (!context.index.lowerToBookPaths.contains(lower_target)) {
            if (report_missing) {
                addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                                QStringLiteral("链接检查：发现无法定位的书内链接【%1】。").arg(raw_link));
            }
            return;
        }

        const QStringList matches = context.index.lowerToBookPaths.value(lower_target);
        if (matches.count() != 1) {
            addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                            QStringLiteral("链接检查：链接【%1】存在大小写歧义，未自动修改。").arg(raw_link));
            return;
        }

        actual_bookpath = matches.first();
        context.updates[target_bookpath] = actual_bookpath;
        addUniqueResult(result, context, ValidationResult::ResType_Info, source_bookpath, location,
                        QStringLiteral("链接检查：链接【%1】与实际文件路径大小写不一致，已计划修正为【%2】。")
                            .arg(raw_link, actual_bookpath));
    }

    const QString fragment = url.fragment(QUrl::FullyDecoded);
    if (!shouldSkipFragmentIdCheck(fragment) &&
        context.idsByBookPath.contains(actual_bookpath) &&
        !context.idsByBookPath.value(actual_bookpath).contains(fragment)) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("链接检查：链接【%1】指向的 fragment【#%2】在目标文件【%3】中找不到对应 id。")
                            .arg(raw_link, fragment, actual_bookpath));
    }
}

static void scanElementAttributes(EpubStructureNormalizer* normalizer,
                                  EpubStructureNormalizer::Result& result,
                                  LinkScanContext& context,
                                  const QString& source_bookpath,
                                  const QString& source)
{
    QRegularExpression attr_re(QStringLiteral("\\b(?:href|src|poster|data|altimg)\\s*=\\s*([\"'])(.*?)\\1"),
                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = attr_re.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        inspectLink(normalizer, result, context, source_bookpath, match.captured(2),
                    sourceLocationForOffset(source, match.capturedStart(2)), true);
    }
}

static void scanCssUrls(EpubStructureNormalizer* normalizer,
                        EpubStructureNormalizer::Result& result,
                        LinkScanContext& context,
                        const QString& source_bookpath,
                        const QString& source,
                        const QString& location_source,
                        int base_offset,
                        bool report_missing)
{
    QRegularExpression url_re(QStringLiteral("url\\(\\s*[\"']?([^\\(\\)\"']*)[\"']?\\s*\\)"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator url_it = url_re.globalMatch(source);
    while (url_it.hasNext()) {
        QRegularExpressionMatch match = url_it.next();
        inspectLink(normalizer, result, context, source_bookpath, match.captured(1),
                    sourceLocationForOffset(location_source, base_offset + match.capturedStart(1)), report_missing);
    }

    QRegularExpression import_re(QStringLiteral("@import\\s+(?:url\\(\\s*[\"']?([^\\(\\)\"']*)[\"']?\\s*\\)|[\"']([^\"']*)[\"'])"),
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator import_it = import_re.globalMatch(source);
    while (import_it.hasNext()) {
        QRegularExpressionMatch match = import_it.next();
        int capture_index = 1;
        QString href = match.captured(capture_index);
        if (href.isEmpty()) {
            capture_index = 2;
            href = match.captured(capture_index);
        }
        inspectLink(normalizer, result, context, source_bookpath, href,
                    sourceLocationForOffset(location_source, base_offset + match.capturedStart(capture_index)), report_missing);
    }
}

static void scanStyleAttributes(EpubStructureNormalizer* normalizer,
                                EpubStructureNormalizer::Result& result,
                                LinkScanContext& context,
                                const QString& source_bookpath,
                                const QString& source)
{
    QRegularExpression style_re(QStringLiteral("\\bstyle\\s*=\\s*([\"'])(.*?)\\1"),
                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = style_re.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        scanCssUrls(normalizer, result, context, source_bookpath, match.captured(2), source,
                    match.capturedStart(2), true);
    }
}

static void scanNcxContentSources(EpubStructureNormalizer* normalizer,
                                  EpubStructureNormalizer::Result& result,
                                  LinkScanContext& context,
                                  const QString& source_bookpath,
                                  const QString& source)
{
    QRegularExpression src_re(QStringLiteral("\\bsrc\\s*=\\s*([\"'])(.*?)\\1"),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = src_re.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        inspectLink(normalizer, result, context, source_bookpath, match.captured(2),
                    sourceLocationForOffset(source, match.capturedStart(2)), true);
    }
}

static ManifestHrefInfo manifestHrefInfo(const QString& href, const QString& opf_folder)
{
    ManifestHrefInfo info;
    info.rawHref = href.trimmed();

    QUrl url(info.rawHref);
    info.hasQuery = url.hasQuery();
    info.hasFragment = url.hasFragment();

    const QString scheme = url.scheme().toLower();
    info.isDataUrl = scheme == "data";
    info.isFileUrl = scheme == "file";
    info.isLocal = scheme.isEmpty() && !info.rawHref.startsWith("//");

    if (!info.isLocal) {
        info.key = "remote:" + info.rawHref.toLower();
        return info;
    }

    QString href_path = Utility::URLDecodePath(url.path());
    if (href_path.isEmpty() && !info.rawHref.isEmpty()) {
        href_path = Utility::URLDecodePath(info.rawHref.split('#').first().split('?').first());
    }
    info.bookPath = Utility::buildBookPath(href_path, opf_folder);
    info.key = "local:" + info.bookPath.toLower();
    return info;
}

static QString manifestHrefForBookPath(const QString& bookpath, const QString& opf_folder)
{
    return Utility::URLEncodePath(Utility::relativePath(bookpath, opf_folder));
}

static QString mediaTypeForBookPath(const QString& bookpath, const QString& full_book_folder)
{
    const QFileInfo info(bookpath);
    const QString ext = info.suffix().toLower();
    QString media_type = MediaTypes::instance()->GetMediaTypeFromExtension(ext, "");
    if (ext == "xml") {
        media_type = MediaTypes::instance()->GetMediaTypeFromXML(full_book_folder + "/" + bookpath, media_type);
    }
    return media_type;
}

static QStringList propertyTokens(const ManifestEntry& entry)
{
    return entry.m_atts.value("properties", "").split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

static bool hasProperty(const ManifestEntry& entry, const QString& property)
{
    return propertyTokens(entry).contains(property);
}

static void addProperty(ManifestEntry& entry, const QString& property)
{
    QStringList properties = propertyTokens(entry);
    if (!properties.contains(property)) {
        properties << property;
        entry.m_atts["properties"] = properties.join(QStringLiteral(" "));
    }
}

static bool isContentDocumentMediaType(const QString& media_type, const QString& epub_version)
{
    const QString type = media_type.trimmed().toLower();
    if (type == "application/xhtml+xml") {
        return true;
    }
    if (epub_version.startsWith("2")) {
        return type == "application/x-dtbook+xml";
    }
    return type == "image/svg+xml";
}

static bool isRemoteManifestMediaTypeAllowed(const QString& media_type)
{
    const QString type = media_type.trimmed().toLower();
    return type.startsWith("audio/") ||
           type.startsWith("video/") ||
           type.startsWith("font/") ||
           type.startsWith("application/font-") ||
           type.startsWith("application/x-font") ||
           type == "application/vnd.ms-opentype" ||
           type == "application/x-shockwave-flash";
}

static bool isImageMediaType(const QString& media_type)
{
    return media_type.trimmed().toLower().startsWith("image/");
}

static bool hasContentDocumentFallback(const ManifestEntry& entry,
                                       const QHash<QString, ManifestEntry>& manifest_by_id,
                                       const QString& epub_version)
{
    QString fallback_id = entry.m_atts.value("fallback", "").trimmed();
    QSet<QString> seen;

    while (!fallback_id.isEmpty()) {
        if (seen.contains(fallback_id) || !manifest_by_id.contains(fallback_id)) {
            return false;
        }
        seen.insert(fallback_id);

        const ManifestEntry fallback_entry = manifest_by_id.value(fallback_id);
        if (isContentDocumentMediaType(fallback_entry.m_mtype, epub_version)) {
            return true;
        }
        fallback_id = fallback_entry.m_atts.value("fallback", "").trimmed();
    }

    return false;
}

static QString formatFileSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 bytes").arg(bytes);
}

static QByteArray readFileHeader(const QString& path, qint64 length)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.read(length);
}

static bool isJpegResource(const QString& extension, const QString& media_type)
{
    return extension == QLatin1String("jpg") ||
           extension == QLatin1String("jpeg") ||
           extension == QLatin1String("jpe") ||
           media_type.trimmed().toLower() == QLatin1String("image/jpeg");
}

static bool isPngResource(const QString& extension, const QString& media_type)
{
    return extension == QLatin1String("png") ||
           media_type.trimmed().toLower() == QLatin1String("image/png");
}

static bool isGifResource(const QString& extension, const QString& media_type)
{
    return extension == QLatin1String("gif") ||
           media_type.trimmed().toLower() == QLatin1String("image/gif");
}

}

EpubStructureNormalizer::EpubStructureNormalizer(Book* book)
    :
    m_Book(book)
{
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalize()
{
    return normalize(Options());
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalize(const Options& options)
{
    Result result;

    if (options.repairOpfManifest) {
        appendResult(result, normalizeOpfManifest(options));
    }

    if (options.repairLinkCase) {
        appendResult(result, normalizeLinkCase(options));
    }

    appendResult(result, validateResourceDiagnostics());

    return result;
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalizeOpfManifest()
{
    return normalizeOpfManifest(Options());
}

void EpubStructureNormalizer::appendResult(Result& target, const Result& source) const
{
    target.validationResults.append(source.validationResults);
    target.bookBrowserRefreshRequired = target.bookBrowserRefreshRequired || source.bookBrowserRefreshRequired;
    target.modified = target.modified || source.modified;
}

void EpubStructureNormalizer::addResult(Result& result,
                                        ValidationResult::ResType type,
                                        const QString& bookpath,
                                        const QString& message) const
{
    result.validationResults << ValidationResult(type, bookpath, -1, -1, message);
}

void EpubStructureNormalizer::addResult(Result& result,
                                        ValidationResult::ResType type,
                                        const QString& bookpath,
                                        int line,
                                        int charoffset,
                                        const QString& message) const
{
    result.validationResults << ValidationResult(type, bookpath, line, charoffset, message);
}

EpubStructureNormalizer::Result EpubStructureNormalizer::validateResourceDiagnostics() const
{
    Result result;

    if (!m_Book) {
        return result;
    }

    const QList<Resource*> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach(Resource* resource, resources) {
        if (!resource || resource->Type() != Resource::ImageResourceType) {
            continue;
        }

        const QString bookpath = resource->GetRelativePath();
        const QFileInfo file_info(resource->GetFullPath());
        if (!file_info.exists() || !file_info.isFile()) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：图片文件在临时书目录中不存在，建议检查 Manifest 与 Book Browser。"));
            continue;
        }

        if (file_info.size() == 0) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：图片文件大小为 0，阅读器无法正常显示。"));
            continue;
        }

        if (file_info.size() >= 4 * 1024 * 1024) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：图片文件大小为【%1】，超过 EPUBCheck 常见 4 MB 建议值。")
                          .arg(formatFileSize(file_info.size())));
        }

        QImageReader reader(resource->GetFullPath());
        const QSize image_size = reader.size();
        if (!reader.canRead() || !image_size.isValid()) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：无法读取图片尺寸，文件头或图片数据可能损坏。"));
        } else if (image_size.width() >= 3840 || image_size.height() >= 2160) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：图片尺寸为【%1x%2】，超过 EPUBCheck 常见大图提示阈值。")
                          .arg(image_size.width()).arg(image_size.height()));
        }

        const QString extension = file_info.suffix().toLower();
        const QString media_type = resource->GetMediaType();
        const QByteArray header = readFileHeader(resource->GetFullPath(), 8);
        if (isJpegResource(extension, media_type) &&
            !header.startsWith(QByteArray::fromHex("FFD8"))) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：JPEG 图片文件头无效，实际文件内容可能与扩展名或 media-type 不一致。"));
        } else if (isPngResource(extension, media_type) &&
                   !header.startsWith(QByteArray::fromHex("89504E470D0A1A0A"))) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：PNG 图片文件头无效，实际文件内容可能与扩展名或 media-type 不一致。"));
        } else if (isGifResource(extension, media_type) &&
                   !header.startsWith("GIF8")) {
            addResult(result, ValidationResult::ResType_Warn, bookpath,
                      QStringLiteral("图片检查：GIF 图片文件头无效，实际文件内容可能与扩展名或 media-type 不一致。"));
        }
    }

    return result;
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalizeLinkCase()
{
    return normalizeLinkCase(Options());
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalizeLinkCase(const Options& options)
{
    Result result;

    if (!m_Book || !options.repairLinkCase) {
        return result;
    }

    LinkScanContext context;
    const QList<Resource*> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach(Resource* resource, resources) {
        if (!resource) {
            continue;
        }
        const QString bookpath = resource->GetRelativePath();
        context.index.exactBookPaths.insert(bookpath);
        context.index.lowerToBookPaths[bookpath.toLower()].append(bookpath);

        QString id_source;
        bool scan_ids = true;
        switch (resource->Type()) {
        case Resource::HTMLResourceType:
            if (HTMLResource* html_resource = qobject_cast<HTMLResource*>(resource)) {
                id_source = html_resource->GetText();
            }
            break;
        case Resource::SVGResourceType:
            if (SVGResource* svg_resource = qobject_cast<SVGResource*>(resource)) {
                id_source = svg_resource->GetText();
            }
            break;
        case Resource::XMLResourceType:
        case Resource::NCXResourceType:
            if (XMLResource* xml_resource = qobject_cast<XMLResource*>(resource)) {
                id_source = xml_resource->GetText();
            }
            break;
        default:
            scan_ids = false;
            break;
        }

        if (scan_ids) {
            const IdScanResult id_scan = scanIds(id_source);
            context.idsByBookPath[bookpath] = id_scan.ids;
            for (const QPair<QString, SourceLocation>& duplicate : id_scan.duplicateIds) {
                addUniqueResult(result, context, ValidationResult::ResType_Warn, bookpath, duplicate.second,
                                QStringLiteral("EPUBCheck：资源内存在重复 id【%1】，fragment 链接可能无法唯一定位。")
                                    .arg(duplicate.first));
            }
        }
    }

    foreach(Resource* resource, resources) {
        if (!resource) {
            continue;
        }

        const QString bookpath = resource->GetRelativePath();
        if (resource->Type() == Resource::HTMLResourceType) {
            HTMLResource* html_resource = qobject_cast<HTMLResource*>(resource);
            if (!html_resource) {
                continue;
            }
            const QString source = html_resource->GetText();
            scanElementAttributes(this, result, context, bookpath, source);
            scanStyleAttributes(this, result, context, bookpath, source);
        } else if (resource->Type() == Resource::CSSResourceType) {
            CSSResource* css_resource = qobject_cast<CSSResource*>(resource);
            if (!css_resource) {
                continue;
            }
            const QString source = css_resource->GetText();
            scanCssUrls(this, result, context, bookpath, source, source, 0, false);
        } else if (resource->Type() == Resource::NCXResourceType) {
            NCXResource* ncx_resource = qobject_cast<NCXResource*>(resource);
            if (!ncx_resource) {
                continue;
            }
            scanNcxContentSources(this, result, context, bookpath, ncx_resource->GetText());
        }
    }

    if (context.updates.isEmpty()) {
        return result;
    }

    result.modified = true;

    if (options.dryRun) {
        return result;
    }

    foreach(Resource* resource, resources) {
        if (!resource) {
            continue;
        }
        switch (resource->Type()) {
        case Resource::HTMLResourceType:
        case Resource::CSSResourceType:
        case Resource::NCXResourceType:
        case Resource::OPFResourceType:
        case Resource::XMLResourceType:
            resource->SetCurrentBookRelPath(resource->GetRelativePath());
            break;
        default:
            break;
        }
    }

    QStringList update_errors = UniversalUpdates::PerformUniversalUpdates(true, resources, context.updates);
    foreach(QString update_error, update_errors) {
        addResult(result, ValidationResult::ResType_Error, QString(),
                  QStringLiteral("链接检查：自动修正链接大小写时发生错误：%1").arg(update_error));
    }

    return result;
}

EpubStructureNormalizer::Result EpubStructureNormalizer::normalizeOpfManifest(const Options& options)
{
    Result result;

    if (!m_Book || !m_Book->GetOPF()) {
        return result;
    }

    OPFResource* opf = m_Book->GetOPF();
    QString opfpath = opf->GetCurrentBookRelPath();
    if (opfpath.isEmpty()) {
        opfpath = opf->GetRelativePath();
    }

    OPFParser p;
    p.parse(opf->GetText());

    const ValidationResult::ResType error_type = ValidationResult::ResType_Error;
    const ValidationResult::ResType warning_type = ValidationResult::ResType_Warn;
    bool opf_modified = false;

    if (!p.m_package.m_atts.contains("xmlns")) {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：找不到在package节点的xmlns属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：不规范的xmlns属性值【%1】,建议改为\"http://www.idpf.org/2007/opf\"").arg(p.m_package.m_atts["xmlns"]));
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：找不到在metadata节点的xmlns:dc属性，,建议以\"http://purl.org/dc/elements/1.1/\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：不规范的xmlns:dc属性值【%1】，建议改为\"http://purl.org/dc/elements/1.1/\"").arg(p.m_metans.m_atts["xmlns:dc"]));
    }

    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：找不到在metadata节点的xmlns:opf属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        addResult(result, error_type, opfpath,
                  QStringLiteral("OPF规范：不规范的xmlns:opf属性值【%1】，建议改为\"http://www.idpf.org/2007/opf\"").arg(p.m_metans.m_atts["xmlns:opf"]));
    }

    QString tempfolder = m_Book->GetFolderKeeper()->GetFullPathToMainFolder();
    QStringList filepath_list = Utility::walkDirs(tempfolder);
    QHash<QString, QString> lower_bkpath_to_original_bkpath;

    foreach(QString filepath, filepath_list) {
        QString bkpath = filepath.mid(tempfolder.size() + 1);
        lower_bkpath_to_original_bkpath[bkpath.toLower()] = bkpath;
    }

    QStringList spine_idref_list;
    foreach(SpineEntry se, p.m_spine) {
        spine_idref_list << se.m_idref;
    }

    QString cover_idref;
    foreach(MetaEntry meta, p.m_metadata) {
        if (meta.m_name == "meta" && meta.m_atts["name"] == "cover") {
            cover_idref = meta.m_atts["content"];
        }
    }

    QStringList all_idref_list = spine_idref_list;
    if (!cover_idref.isEmpty()) {
        all_idref_list.prepend(cover_idref);
    }

    QHash<QString, QString> manifest_key_to_id;
    QHash<QString, QString> id_to_manifest_key;
    foreach(ManifestEntry me, p.m_manifest) {
        ManifestHrefInfo href_info = manifestHrefInfo(me.m_href, opf->GetFolder());
        QString manifest_key = href_info.key;

        if (manifest_key_to_id.contains(manifest_key)) {
            if (all_idref_list.contains(me.m_id)) {
                manifest_key_to_id[manifest_key] = me.m_id;
            }
        } else {
            manifest_key_to_id[manifest_key] = me.m_id;
        }

        if (id_to_manifest_key.contains(me.m_id)) {
            if (!href_info.isLocal || lower_bkpath_to_original_bkpath.contains(href_info.bookPath.toLower())) {
                id_to_manifest_key[me.m_id] = manifest_key;
            }
        } else {
            id_to_manifest_key[me.m_id] = manifest_key;
        }
    }

    QList<ManifestEntry> new_manifest;
    for (int i = 0; i < p.m_manifest.count(); i++) {
        ManifestEntry me = p.m_manifest.at(i);
        ManifestHrefInfo href_info = manifestHrefInfo(me.m_href, opf->GetFolder());
        QString bkpath = href_info.bookPath;
        QString lower_bkpath = bkpath.toLower();
        QString id = me.m_id;

        if (href_info.key != id_to_manifest_key[id]) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (id != manifest_key_to_id[href_info.key]) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (!href_info.isLocal) {
            if (href_info.isFileUrl) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：manifest href【%1】使用 file: URL，建议改为书内相对路径或移除。").arg(me.m_href));
            } else if (href_info.isDataUrl) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：manifest href【%1】使用 data: URL，EPUB3 中不允许作为 Manifest 资源。").arg(me.m_href.left(64)));
            } else if (p.m_package.m_version.startsWith("3") && !isRemoteManifestMediaTypeAllowed(me.m_mtype)) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：EPUB3 Manifest 远程资源【%1】的 media-type【%2】通常只允许音频、视频或字体。")
                              .arg(me.m_href, me.m_mtype));
            } else if (!p.m_package.m_version.startsWith("3")) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：Manifest href【%1】是远程资源，EPUB2 通常要求 Manifest 资源为包内文件。").arg(me.m_href));
            }
            new_manifest.append(me);
            continue;
        }

        if (href_info.hasQuery || href_info.hasFragment) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：manifest href【%1】包含 query 或 fragment，已按资源路径规范化。").arg(me.m_href));
            opf_modified = true;
        }

        if (!lower_bkpath_to_original_bkpath.contains(lower_bkpath)) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：无效OPF超链接：在manifest项发现无效href【%1】，已进行删除对应项的处理。").arg(me.m_href));
            opf_modified = true;
            continue;
        }

        QString original_bkpath = lower_bkpath_to_original_bkpath[lower_bkpath];
        if (original_bkpath == opf->GetRelativePath()) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("EPUBCheck：manifest 不应登记 OPF package document 自身，已删除 href【%1】。").arg(me.m_href));
            opf_modified = true;
            continue;
        }

        if (bkpath != original_bkpath) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：大小写不一致：发现 OPF 超链接【%1】与实际路径大小写不一致，已自动校正。").arg(me.m_href));
            opf_modified = true;
        }
        me.m_href = manifestHrefForBookPath(original_bkpath, opf->GetFolder());

        QString expected_media_type = mediaTypeForBookPath(original_bkpath, tempfolder);
        if (!expected_media_type.isEmpty() && me.m_mtype != expected_media_type) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：manifest 项【%1】的 media-type【%2】与文件类型不一致，已修正为【%3】。")
                          .arg(me.m_href, me.m_mtype, expected_media_type));
            me.m_mtype = expected_media_type;
            opf_modified = true;
        }

        new_manifest.append(me);
    }

    QStringList all_ids_without_duplication;
    QHash<QString, ManifestEntry> manifest_by_id;
    QHash<QString, ManifestEntry> manifest_by_lower_bkpath;
    QStringList nav_ids;
    foreach(ManifestEntry me, new_manifest) {
        all_ids_without_duplication << me.m_id;
        manifest_by_id[me.m_id] = me;
        ManifestHrefInfo href_info = manifestHrefInfo(me.m_href, opf->GetFolder());
        if (href_info.isLocal) {
            manifest_by_lower_bkpath[href_info.bookPath.toLower()] = me;
        }
        if (hasProperty(me, "nav")) {
            nav_ids << me.m_id;
        }
    }

    QStringList lower_bkpath_on_manifest;
    foreach(ManifestEntry me, new_manifest) {
        ManifestHrefInfo href_info = manifestHrefInfo(me.m_href, opf->GetFolder());
        if (href_info.isLocal) {
            lower_bkpath_on_manifest << href_info.bookPath.toLower();
        }
    }

    foreach(QString filepath, filepath_list) {
        QString bkpath = filepath.mid(tempfolder.size() + 1);
        QString lower_bkpath = bkpath.toLower();

        if (lower_bkpath_on_manifest.contains(lower_bkpath)) {
            continue;
        }

        QFileInfo bkpath_info = QFileInfo(bkpath);
        QString ext = bkpath_info.suffix().toLower();
        QString media_type = mediaTypeForBookPath(bkpath, tempfolder);
        if (ext == "xml" || ext == "opf" || media_type.isEmpty()) {
            continue;
        }

        result.bookBrowserRefreshRequired = true;
        opf_modified = true;

        if (!options.dryRun && !m_Book->GetFolderKeeper()->GetAllBookPaths().contains(bkpath)) {
            Resource* resource = m_Book->GetFolderKeeper()->AddContentFileToFolder(filepath, false, media_type, bkpath);
            if (resource->Type() == Resource::HTMLResourceType) {
                HTMLResource* hresource = qobject_cast<HTMLResource*>(resource);
                if (hresource) {
                    hresource->SetText(HTMLEncodingResolver::ReadHTMLFile(filepath));
                }
            }
        }

        ManifestEntry me;
        me.m_href = Utility::URLEncodePath(Utility::buildRelativePath(opf->GetRelativePath(), bkpath));
        QString unique_id = bkpath_info.baseName();
        if (unique_id.isEmpty()) {
            unique_id = QStringLiteral("resource");
        }
        while (all_ids_without_duplication.contains(unique_id)) {
            unique_id = "_" + unique_id;
        }
        all_ids_without_duplication << unique_id;
        me.m_id = unique_id;
        me.m_mtype = media_type;
        new_manifest << me;
        manifest_by_id[me.m_id] = me;
        manifest_by_lower_bkpath[lower_bkpath] = me;
        lower_bkpath_on_manifest << lower_bkpath;

        if (me.m_mtype == "application/xhtml+xml" && !spine_idref_list.contains(me.m_id)) {
            SpineEntry se;
            se.m_idref = me.m_id;
            p.m_spine << se;
        }

        addResult(result, warning_type, opfpath,
                  QStringLiteral("OPF规范：发现文件【%1】未登记到Manifest，已自动登记。").arg(bkpath));
    }

    if (p.m_package.m_version.startsWith("3")) {
        if (nav_ids.count() > 1) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：EPUB3 Manifest 中发现多个 properties=\"nav\" 项，建议仅保留一个导航文档。"));
        } else if (nav_ids.isEmpty()) {
            QList<int> nav_candidates;
            for (int i = 0; i < new_manifest.count(); i++) {
                const ManifestEntry me = new_manifest.at(i);
                ManifestHrefInfo href_info = manifestHrefInfo(me.m_href, opf->GetFolder());
                QFileInfo info(href_info.bookPath);
                const QString filename = info.fileName().toLower();
                if (me.m_mtype == "application/xhtml+xml" &&
                    (me.m_id.toLower() == "nav" || filename == "nav.xhtml" || filename == "nav.html")) {
                    nav_candidates << i;
                }
            }
            if (nav_candidates.count() == 1) {
                ManifestEntry me = new_manifest.at(nav_candidates.first());
                addProperty(me, "nav");
                new_manifest[nav_candidates.first()] = me;
                manifest_by_id[me.m_id] = me;
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：EPUB3 未标记导航文档，已为 manifest 项【%1】添加 properties=\"nav\"。").arg(me.m_id));
                opf_modified = true;
            } else {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：EPUB3 Manifest 中没有唯一的 nav 导航文档，请手动确认并设置 properties=\"nav\"。"));
            }
        } else {
            ManifestEntry nav_entry = manifest_by_id.value(nav_ids.first());
            if (nav_entry.m_mtype != "application/xhtml+xml") {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：nav 导航文档【%1】的 media-type 应为 application/xhtml+xml。").arg(nav_entry.m_id));
            }
        }
    }

    for (int i = 0; i < new_manifest.count(); i++) {
        ManifestEntry me = new_manifest.at(i);
        if (hasProperty(me, "cover-image") && !isImageMediaType(me.m_mtype)) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：manifest 项【%1】声明 cover-image，但 media-type【%2】不是图片类型。")
                          .arg(me.m_id, me.m_mtype));
        }
        if (hasProperty(me, "nav") && me.m_mtype != "application/xhtml+xml") {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：manifest 项【%1】声明 nav，但 media-type【%2】不是 application/xhtml+xml。")
                          .arg(me.m_id, me.m_mtype));
        }
        if (p.m_package.m_version.startsWith("3") && me.m_id == cover_idref &&
            isImageMediaType(me.m_mtype) && !hasProperty(me, "cover-image")) {
            addProperty(me, "cover-image");
            new_manifest[i] = me;
            manifest_by_id[me.m_id] = me;
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：EPUB3 封面图片【%1】缺少 cover-image 属性，已自动补齐。").arg(me.m_id));
            opf_modified = true;
        }
    }

    if (!cover_idref.isEmpty() && !all_ids_without_duplication.contains(cover_idref)) {
        addResult(result, warning_type, opfpath,
                  QStringLiteral("OPF规范：无效ID引用：在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。").arg(cover_idref));
    }

    bool has_linear_spine_item = false;
    QSet<QString> seen_spine_ids;
    foreach(SpineEntry se, p.m_spine) {
        QString idref = se.m_idref;
        if (!all_ids_without_duplication.contains(idref)) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("OPF规范：无效ID引用：在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。").arg(idref));
            continue;
        }

        ManifestEntry spine_entry = manifest_by_id.value(idref);
        ManifestHrefInfo spine_href_info = manifestHrefInfo(spine_entry.m_href, opf->GetFolder());
        if (!spine_href_info.isLocal) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：spine 项【%1】引用远程 Manifest 资源，阅读器可能无法作为阅读顺序内容处理。").arg(idref));
        }

        if (!isContentDocumentMediaType(spine_entry.m_mtype, p.m_package.m_version)) {
            const QString fallback_id = spine_entry.m_atts.value("fallback", "").trimmed();
            if (fallback_id.isEmpty()) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：spine 项【%1】的 media-type【%2】不是内容文档，且缺少 fallback。")
                              .arg(idref, spine_entry.m_mtype));
            } else if (!hasContentDocumentFallback(spine_entry, manifest_by_id, p.m_package.m_version)) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：spine 项【%1】的 fallback 链未指向可阅读内容文档。").arg(idref));
            }
        }

        if (se.m_atts.value("linear", "yes").toLower() != "no") {
            has_linear_spine_item = true;
        }

        if (p.m_package.m_version.startsWith("2")) {
            if (seen_spine_ids.contains(idref)) {
                addResult(result, warning_type, opfpath,
                          QStringLiteral("EPUBCheck：EPUB2 spine 中重复引用了 manifest 项【%1】。").arg(idref));
            }
            seen_spine_ids.insert(idref);
        }
    }

    if (!p.m_spine.isEmpty() && !has_linear_spine_item) {
        addResult(result, warning_type, opfpath,
                  QStringLiteral("EPUBCheck：spine 中没有 linear=\"yes\" 的阅读顺序项目，阅读器可能无法确定主要内容。"));
    }

    foreach(GuideEntry ge, p.m_guide) {
        QUrl guide_url(ge.m_href);
        if (!guide_url.isRelative() || guide_url.hasQuery()) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：guide 引用【%1】不是规范的书内相对链接。").arg(ge.m_href));
            continue;
        }

        const QString guide_bkpath = Utility::buildBookPath(Utility::URLDecodePath(guide_url.path()), opf->GetFolder());
        if (!manifest_by_lower_bkpath.contains(guide_bkpath.toLower())) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：guide 引用【%1】没有对应的 Manifest 内容项。").arg(ge.m_href));
            continue;
        }

        ManifestEntry guide_entry = manifest_by_lower_bkpath.value(guide_bkpath.toLower());
        if (!isContentDocumentMediaType(guide_entry.m_mtype, p.m_package.m_version)) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("EPUBCheck：guide 引用【%1】指向的 media-type【%2】不是内容文档。")
                          .arg(ge.m_href, guide_entry.m_mtype));
        }
    }

    if (opf_modified) {
        result.modified = true;
        if (!options.dryRun) {
            p.m_manifest = new_manifest;
            opf->SetText(p.convert_to_xml());
        }
    }

    return result;
}

}
