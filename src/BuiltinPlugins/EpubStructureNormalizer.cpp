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

#include <QDir>
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
#include <QXmlStreamReader>

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
#include "ResourceObjects/TextResource.h"
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
    QHash<QString, QString> mediaTypesByBookPath;
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

struct OpfTagLocation {
    SourceLocation tagLocation;
    QHash<QString, SourceLocation> attrLocations;
    QHash<QString, QString> attrs;
};

struct OpfSourceIndex {
    OpfTagLocation packageTag;
    OpfTagLocation metadataTag;
    QList<OpfTagLocation> metadataMetaTags;
    QList<OpfTagLocation> manifestItems;
    QList<OpfTagLocation> spineItemrefs;
    QList<OpfTagLocation> guideReferences;
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

static SourceLocation sourceLocationForLineColumn(const QString& source, int line, int column)
{
    SourceLocation location;
    location.line = line;

    if (line <= 0) {
        return location;
    }

    int current_line = 1;
    int line_start = 0;
    for (int i = 0; i < source.size(); i++) {
        if (current_line == line) {
            break;
        }
        if (source.at(i) == QLatin1Char('\n')) {
            current_line++;
            line_start = i + 1;
        }
    }

    if (current_line != line) {
        return location;
    }

    const int column_offset = qMax(0, column - 1);
    location.offset = qMin(source.size(), line_start + column_offset);
    return location;
}

static void addLocatedResult(EpubStructureNormalizer::Result& result,
                             ValidationResult::ResType type,
                             const QString& bookpath,
                             const SourceLocation& location,
                             const QString& message)
{
    result.validationResults << ValidationResult(type, bookpath, location.line, location.offset, message);
}

static QPair<int, int> tagSectionRange(const QString& source, const QString& tag_name)
{
    QRegularExpression begin_re(QStringLiteral("<\\s*(?:[A-Za-z_][\\w.-]*:)?%1\\b[^>]*>")
                                    .arg(QRegularExpression::escape(tag_name)),
                                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch begin_match = begin_re.match(source);
    if (!begin_match.hasMatch()) {
        return qMakePair(-1, -1);
    }

    QRegularExpression end_re(QStringLiteral("</\\s*(?:[A-Za-z_][\\w.-]*:)?%1\\s*>")
                                  .arg(QRegularExpression::escape(tag_name)),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch end_match = end_re.match(source, begin_match.capturedEnd(0));
    const int end = end_match.hasMatch() ? end_match.capturedEnd(0) : begin_match.capturedEnd(0);
    return qMakePair(begin_match.capturedStart(0), end);
}

static OpfTagLocation makeOpfTagLocation(const QString& source,
                                         const QRegularExpressionMatch& match)
{
    OpfTagLocation location;
    location.tagLocation = sourceLocationForOffset(source, match.capturedStart(0));

    const QString tag_text = match.captured(0);
    const int tag_offset = match.capturedStart(0);
    QRegularExpression attr_re(QStringLiteral("([A-Za-z_:][A-Za-z0-9_.:-]*)\\s*=\\s*([\"'])(.*?)\\2"),
                               QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = attr_re.globalMatch(tag_text);
    while (it.hasNext()) {
        QRegularExpressionMatch attr_match = it.next();
        const QString name = attr_match.captured(1);
        location.attrs[name] = attr_match.captured(3);
        location.attrLocations[name] = sourceLocationForOffset(source, tag_offset + attr_match.capturedStart(3));
    }

    return location;
}

static QList<OpfTagLocation> scanOpfTags(const QString& source,
                                         const QString& tag_name,
                                         int range_start,
                                         int range_end)
{
    QList<OpfTagLocation> tags;
    if (range_start < 0 || range_end <= range_start) {
        return tags;
    }

    QRegularExpression tag_re(QStringLiteral("<\\s*(?:[A-Za-z_][\\w.-]*:)?%1\\b[^>]*>")
                                  .arg(QRegularExpression::escape(tag_name)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = tag_re.globalMatch(source.mid(range_start, range_end - range_start));
    while (it.hasNext()) {
        QRegularExpressionMatch local_match = it.next();
        QRegularExpressionMatch match = tag_re.match(source, range_start + local_match.capturedStart(0));
        if (match.hasMatch() && match.capturedStart(0) < range_end) {
            tags << makeOpfTagLocation(source, match);
        }
    }
    return tags;
}

static OpfTagLocation firstOpfTag(const QString& source, const QString& tag_name)
{
    QRegularExpression tag_re(QStringLiteral("<\\s*(?:[A-Za-z_][\\w.-]*:)?%1\\b[^>]*>")
                                  .arg(QRegularExpression::escape(tag_name)),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = tag_re.match(source);
    if (!match.hasMatch()) {
        return OpfTagLocation();
    }
    return makeOpfTagLocation(source, match);
}

static OpfSourceIndex buildOpfSourceIndex(const QString& source)
{
    OpfSourceIndex index;
    index.packageTag = firstOpfTag(source, QStringLiteral("package"));
    index.metadataTag = firstOpfTag(source, QStringLiteral("metadata"));

    const QPair<int, int> metadata_range = tagSectionRange(source, QStringLiteral("metadata"));
    const QPair<int, int> manifest_range = tagSectionRange(source, QStringLiteral("manifest"));
    const QPair<int, int> spine_range = tagSectionRange(source, QStringLiteral("spine"));
    const QPair<int, int> guide_range = tagSectionRange(source, QStringLiteral("guide"));

    index.metadataMetaTags = scanOpfTags(source, QStringLiteral("meta"), metadata_range.first, metadata_range.second);
    index.manifestItems = scanOpfTags(source, QStringLiteral("item"), manifest_range.first, manifest_range.second);
    index.spineItemrefs = scanOpfTags(source, QStringLiteral("itemref"), spine_range.first, spine_range.second);
    index.guideReferences = scanOpfTags(source, QStringLiteral("reference"), guide_range.first, guide_range.second);
    return index;
}

static SourceLocation opfAttrLocation(const OpfTagLocation& tag, const QString& attr_name)
{
    return tag.attrLocations.value(attr_name, tag.tagLocation);
}

static SourceLocation opfManifestLocation(const OpfSourceIndex& index, int manifest_index, const QString& attr_name)
{
    if (manifest_index < 0 || manifest_index >= index.manifestItems.count()) {
        return SourceLocation();
    }
    return opfAttrLocation(index.manifestItems.at(manifest_index), attr_name);
}

static SourceLocation opfSpineLocation(const OpfSourceIndex& index, int spine_index, const QString& attr_name)
{
    if (spine_index < 0 || spine_index >= index.spineItemrefs.count()) {
        return SourceLocation();
    }
    return opfAttrLocation(index.spineItemrefs.at(spine_index), attr_name);
}

static SourceLocation opfGuideLocation(const OpfSourceIndex& index, int guide_index, const QString& attr_name)
{
    if (guide_index < 0 || guide_index >= index.guideReferences.count()) {
        return SourceLocation();
    }
    return opfAttrLocation(index.guideReferences.at(guide_index), attr_name);
}

static SourceLocation opfManifestLocationById(const OpfSourceIndex& index,
                                              const QString& id,
                                              const QString& attr_name)
{
    for (const OpfTagLocation& item : index.manifestItems) {
        if (item.attrs.value(QStringLiteral("id")) == id) {
            return opfAttrLocation(item, attr_name);
        }
    }
    return SourceLocation();
}

static SourceLocation opfCoverMetaLocation(const OpfSourceIndex& index, const QString& cover_idref)
{
    for (const OpfTagLocation& meta : index.metadataMetaTags) {
        if (meta.attrs.value(QStringLiteral("name")) == QLatin1String("cover") &&
            (cover_idref.isEmpty() || meta.attrs.value(QStringLiteral("content")) == cover_idref)) {
            return opfAttrLocation(meta, QStringLiteral("content"));
        }
    }
    return SourceLocation();
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

static QString actualBookPathForLocalLink(const LinkScanContext& context,
                                          const QString& raw_link,
                                          const QString& source_bookpath)
{
    const QString link = raw_link.trimmed();
    if (link.isEmpty() || link.startsWith("//") || link.startsWith("#") ||
        link.startsWith("file:", Qt::CaseInsensitive)) {
        return QString();
    }

    QUrl url(link);
    if (!url.isRelative() || url.hasQuery()) {
        return QString();
    }

    const QString href_path = Utility::URLDecodePath(url.path());
    if (href_path.isEmpty() || href_path.startsWith(QLatin1Char('/')) ||
        bookPathEscapesRoot(href_path, source_bookpath)) {
        return QString();
    }

    const QString target_bookpath = resolvedBookPath(raw_link, source_bookpath);
    if (context.index.exactBookPaths.contains(target_bookpath)) {
        return target_bookpath;
    }

    const QStringList matches = context.index.lowerToBookPaths.value(target_bookpath.toLower());
    if (matches.count() == 1) {
        return matches.first();
    }

    return QString();
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

enum class ResourceReferenceExpectation {
    None,
    Image,
    Audio,
    Video,
    Style,
    TextTrack,
    ContentDocument,
    EmbeddedContent
};

static QString localName(const QString& name)
{
    return name.section(QLatin1Char(':'), -1).toLower();
}

static QString mediaTypeBase(const QString& media_type)
{
    return media_type.split(QLatin1Char(';')).first().trimmed().toLower();
}

static bool mediaTypeMatchesExpectation(ResourceReferenceExpectation expectation,
                                        const QString& media_type)
{
    const QString type = mediaTypeBase(media_type);
    switch (expectation) {
    case ResourceReferenceExpectation::Image:
        return type.startsWith(QLatin1String("image/"));
    case ResourceReferenceExpectation::Audio:
        return type.startsWith(QLatin1String("audio/"));
    case ResourceReferenceExpectation::Video:
        return type.startsWith(QLatin1String("video/"));
    case ResourceReferenceExpectation::Style:
        return type == QLatin1String("text/css");
    case ResourceReferenceExpectation::TextTrack:
        return type == QLatin1String("text/vtt") ||
               type == QLatin1String("application/ttml+xml");
    case ResourceReferenceExpectation::ContentDocument:
        return type == QLatin1String("application/xhtml+xml") ||
               type == QLatin1String("text/html");
    case ResourceReferenceExpectation::EmbeddedContent:
        return type.startsWith(QLatin1String("image/")) ||
               type.startsWith(QLatin1String("audio/")) ||
               type.startsWith(QLatin1String("video/")) ||
               type == QLatin1String("application/xhtml+xml") ||
               type == QLatin1String("text/html") ||
               type == QLatin1String("application/pdf");
    case ResourceReferenceExpectation::None:
        return true;
    }
    return true;
}

static QString expectationDescription(ResourceReferenceExpectation expectation)
{
    switch (expectation) {
    case ResourceReferenceExpectation::Image:
        return QStringLiteral("图片");
    case ResourceReferenceExpectation::Audio:
        return QStringLiteral("音频");
    case ResourceReferenceExpectation::Video:
        return QStringLiteral("视频");
    case ResourceReferenceExpectation::Style:
        return QStringLiteral("CSS样式");
    case ResourceReferenceExpectation::TextTrack:
        return QStringLiteral("字幕/文本轨道");
    case ResourceReferenceExpectation::ContentDocument:
        return QStringLiteral("内容文档");
    case ResourceReferenceExpectation::EmbeddedContent:
        return QStringLiteral("可嵌入内容");
    case ResourceReferenceExpectation::None:
        return QString();
    }
    return QString();
}

static bool relContainsToken(const QString& rel, const QString& token)
{
    return rel.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
        .contains(token);
}

static ResourceReferenceExpectation sourceExpectationFromDeclaredType(const QString& declared_type)
{
    const QString type = mediaTypeBase(declared_type);
    if (type.startsWith(QLatin1String("image/"))) {
        return ResourceReferenceExpectation::Image;
    }
    if (type.startsWith(QLatin1String("audio/"))) {
        return ResourceReferenceExpectation::Audio;
    }
    if (type.startsWith(QLatin1String("video/"))) {
        return ResourceReferenceExpectation::Video;
    }
    if (type == QLatin1String("text/vtt") ||
        type == QLatin1String("application/ttml+xml")) {
        return ResourceReferenceExpectation::TextTrack;
    }
    return ResourceReferenceExpectation::None;
}

static ResourceReferenceExpectation expectationForElementAttribute(const QString& element_name,
                                                                   const QString& attr_name,
                                                                   const QHash<QString, QString>& attrs)
{
    const QString element = localName(element_name);
    const QString attr = localName(attr_name);

    if (attr == QLatin1String("altimg")) {
        return ResourceReferenceExpectation::Image;
    }
    if ((element == QLatin1String("img") && attr == QLatin1String("src")) ||
        (element == QLatin1String("image") && attr == QLatin1String("href"))) {
        return ResourceReferenceExpectation::Image;
    }
    if (element == QLatin1String("audio") && attr == QLatin1String("src")) {
        return ResourceReferenceExpectation::Audio;
    }
    if (element == QLatin1String("video")) {
        if (attr == QLatin1String("src")) {
            return ResourceReferenceExpectation::Video;
        }
        if (attr == QLatin1String("poster")) {
            return ResourceReferenceExpectation::Image;
        }
    }
    if (element == QLatin1String("source") && attr == QLatin1String("src")) {
        return sourceExpectationFromDeclaredType(attrs.value(QStringLiteral("type")));
    }
    if (element == QLatin1String("track") && attr == QLatin1String("src")) {
        return ResourceReferenceExpectation::TextTrack;
    }
    if (element == QLatin1String("link") && attr == QLatin1String("href")) {
        const QString rel = attrs.value(QStringLiteral("rel"));
        const QString as_value = attrs.value(QStringLiteral("as")).toLower();
        if (relContainsToken(rel, QStringLiteral("stylesheet")) ||
            as_value == QLatin1String("style")) {
            return ResourceReferenceExpectation::Style;
        }
        if (relContainsToken(rel, QStringLiteral("icon")) ||
            as_value == QLatin1String("image")) {
            return ResourceReferenceExpectation::Image;
        }
        if (as_value == QLatin1String("font")) {
            return ResourceReferenceExpectation::None;
        }
        if (as_value == QLatin1String("audio")) {
            return ResourceReferenceExpectation::Audio;
        }
        if (as_value == QLatin1String("video")) {
            return ResourceReferenceExpectation::Video;
        }
    }
    if (element == QLatin1String("iframe") && attr == QLatin1String("src")) {
        return ResourceReferenceExpectation::ContentDocument;
    }
    if ((element == QLatin1String("object") && attr == QLatin1String("data")) ||
        (element == QLatin1String("embed") && attr == QLatin1String("src"))) {
        return ResourceReferenceExpectation::EmbeddedContent;
    }

    return ResourceReferenceExpectation::None;
}

static bool isLinkAttribute(const QString& attr_name)
{
    const QString attr = localName(attr_name);
    return attr == QLatin1String("href") ||
           attr == QLatin1String("src") ||
           attr == QLatin1String("poster") ||
           attr == QLatin1String("data") ||
           attr == QLatin1String("altimg");
}

static void validateElementResourceReference(EpubStructureNormalizer::Result& result,
                                             LinkScanContext& context,
                                             const QString& source_bookpath,
                                             const QString& element_name,
                                             const QString& attr_name,
                                             const QString& raw_link,
                                             const SourceLocation& location,
                                             const QHash<QString, QString>& attrs)
{
    const ResourceReferenceExpectation expectation =
        expectationForElementAttribute(element_name, attr_name, attrs);
    if (expectation == ResourceReferenceExpectation::None) {
        return;
    }

    const QString actual_bookpath = actualBookPathForLocalLink(context, raw_link, source_bookpath);
    if (actual_bookpath.isEmpty() || !context.mediaTypesByBookPath.contains(actual_bookpath)) {
        return;
    }

    const QString media_type = context.mediaTypesByBookPath.value(actual_bookpath);
    if (mediaTypeMatchesExpectation(expectation, media_type)) {
        return;
    }

    addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                    QStringLiteral("内容引用检查：<%1> 的 %2 属性指向【%3】，media-type 为【%4】，但这里应引用%5资源。")
                        .arg(localName(element_name), localName(attr_name), actual_bookpath,
                             media_type, expectationDescription(expectation)));
}

static void validateFragmentReferenceSemantics(EpubStructureNormalizer::Result& result,
                                               LinkScanContext& context,
                                               const QString& source_bookpath,
                                               const QString& element_name,
                                               const QString& attr_name,
                                               const QString& raw_link,
                                               const SourceLocation& location)
{
    const QString element = localName(element_name);
    const QString attr = localName(attr_name);
    const QString link = raw_link.trimmed();
    if (link.isEmpty() || link.startsWith("//") ||
        link.startsWith("file:", Qt::CaseInsensitive)) {
        return;
    }

    QUrl url(link);
    if (!url.isRelative() || url.hasQuery()) {
        return;
    }

    const QString fragment = url.fragment(QUrl::FullyDecoded);
    if (element == QLatin1String("use") && (attr == QLatin1String("href"))) {
        if (fragment.isEmpty()) {
            addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                            QStringLiteral("SVG检查：<use> 的 href【%1】没有指向具体 fragment，建议引用目标 SVG 元素 ID。")
                                .arg(raw_link));
        }
        return;
    }

    if (fragment.isEmpty()) {
        return;
    }

    const bool image_reference =
        (element == QLatin1String("img") && attr == QLatin1String("src")) ||
        (element == QLatin1String("image") && attr == QLatin1String("href")) ||
        attr == QLatin1String("altimg");
    if (!image_reference) {
        return;
    }

    const QString actual_bookpath = actualBookPathForLocalLink(context, raw_link, source_bookpath);
    if (actual_bookpath.isEmpty() || !context.mediaTypesByBookPath.contains(actual_bookpath)) {
        return;
    }

    const QString media_type = mediaTypeBase(context.mediaTypesByBookPath.value(actual_bookpath));
    if (media_type.startsWith(QLatin1String("image/")) &&
        media_type != QLatin1String("image/svg+xml")) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath, location,
                        QStringLiteral("SVG检查：图片引用【%1】带有 fragment，但目标【%2】不是 SVG 图片，阅读器通常无法定位该 fragment。")
                            .arg(raw_link, actual_bookpath));
    }
}

static void scanElementAttributes(EpubStructureNormalizer* normalizer,
                                  EpubStructureNormalizer::Result& result,
                                  LinkScanContext& context,
                                  const QString& source_bookpath,
                                  const QString& source)
{
    QRegularExpression tag_re(QStringLiteral("<\\s*([A-Za-z][A-Za-z0-9:_-]*)\\b([^<>]*)>"),
                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression attr_re(QStringLiteral("([A-Za-z_:][A-Za-z0-9_.:-]*)\\s*=\\s*([\"'])(.*?)\\2"),
                               QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator tag_it = tag_re.globalMatch(source);
    while (tag_it.hasNext()) {
        const QRegularExpressionMatch tag_match = tag_it.next();
        const QString element_name = tag_match.captured(1);
        const QString tag_text = tag_match.captured(0);
        const int tag_offset = tag_match.capturedStart(0);

        QHash<QString, QString> attrs;
        QHash<QString, QPair<QString, SourceLocation>> link_attrs;
        QRegularExpressionMatchIterator attr_it = attr_re.globalMatch(tag_text);
        while (attr_it.hasNext()) {
            const QRegularExpressionMatch attr_match = attr_it.next();
            const QString attr_name = attr_match.captured(1);
            const QString attr_value = attr_match.captured(3);
            const SourceLocation attr_location =
                sourceLocationForOffset(source, tag_offset + attr_match.capturedStart(3));
            attrs[localName(attr_name)] = attr_value;
            if (isLinkAttribute(attr_name)) {
                link_attrs[attr_name] = qMakePair(attr_value, attr_location);
            }
        }

        for (auto it = link_attrs.constBegin(); it != link_attrs.constEnd(); ++it) {
            inspectLink(normalizer, result, context, source_bookpath, it.value().first,
                        it.value().second, true);
            validateElementResourceReference(result, context, source_bookpath, element_name,
                                             it.key(), it.value().first, it.value().second, attrs);
            validateFragmentReferenceSemantics(result, context, source_bookpath, element_name,
                                               it.key(), it.value().first, it.value().second);
        }
    }
}

static bool isCssIdentifierChar(const QChar& ch)
{
    return ch.isLetterOrNumber() ||
           ch == QLatin1Char('_') ||
           ch == QLatin1Char('-') ||
           ch.unicode() >= 0x80;
}

static bool cssIdentifierAt(const QString& source, int offset, const QString& identifier)
{
    if (offset < 0 || offset + identifier.size() > source.size()) {
        return false;
    }
    if (source.mid(offset, identifier.size()).compare(identifier, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (offset > 0 && isCssIdentifierChar(source.at(offset - 1))) {
        return false;
    }
    const int end = offset + identifier.size();
    return end >= source.size() || !isCssIdentifierChar(source.at(end));
}

static int skipCssWhitespaceAndComments(const QString& source, int offset)
{
    int i = offset;
    while (i < source.size()) {
        if (source.at(i).isSpace()) {
            i++;
            continue;
        }
        if (i + 1 < source.size() &&
            source.at(i) == QLatin1Char('/') &&
            source.at(i + 1) == QLatin1Char('*')) {
            const int comment_end = source.indexOf(QStringLiteral("*/"), i + 2);
            if (comment_end < 0) {
                return source.size();
            }
            i = comment_end + 2;
            continue;
        }
        break;
    }
    return i;
}

static bool parseCssQuotedString(const QString& source,
                                 int quote_offset,
                                 QString& value,
                                 int& value_offset,
                                 int& end_offset)
{
    if (quote_offset < 0 || quote_offset >= source.size()) {
        return false;
    }
    const QChar quote = source.at(quote_offset);
    if (quote != QLatin1Char('"') && quote != QLatin1Char('\'')) {
        return false;
    }

    value.clear();
    value_offset = quote_offset + 1;
    int i = value_offset;
    while (i < source.size()) {
        const QChar ch = source.at(i);
        if (ch == QLatin1Char('\\')) {
            value.append(ch);
            i++;
            if (i < source.size()) {
                value.append(source.at(i));
                i++;
            }
            continue;
        }
        if (ch == quote) {
            end_offset = i + 1;
            return true;
        }
        value.append(ch);
        i++;
    }
    end_offset = source.size();
    return false;
}

static bool parseCssUrlFunction(const QString& source,
                                int url_offset,
                                QString& value,
                                int& value_offset,
                                int& end_offset)
{
    int i = skipCssWhitespaceAndComments(source, url_offset + 3);
    if (i >= source.size() || source.at(i) != QLatin1Char('(')) {
        return false;
    }

    i = skipCssWhitespaceAndComments(source, i + 1);
    if (i >= source.size()) {
        return false;
    }

    if (source.at(i) == QLatin1Char('"') || source.at(i) == QLatin1Char('\'')) {
        if (!parseCssQuotedString(source, i, value, value_offset, end_offset)) {
            return false;
        }
        end_offset = skipCssWhitespaceAndComments(source, end_offset);
        if (end_offset < source.size() && source.at(end_offset) == QLatin1Char(')')) {
            end_offset++;
        }
        return true;
    }

    value.clear();
    value_offset = i;
    while (i < source.size()) {
        const QChar ch = source.at(i);
        if (ch == QLatin1Char('\\')) {
            value.append(ch);
            i++;
            if (i < source.size()) {
                value.append(source.at(i));
                i++;
            }
            continue;
        }
        if (ch == QLatin1Char(')')) {
            end_offset = i + 1;
            value = value.trimmed();
            return true;
        }
        if (i + 1 < source.size() &&
            ch == QLatin1Char('/') &&
            source.at(i + 1) == QLatin1Char('*')) {
            const int comment_end = source.indexOf(QStringLiteral("*/"), i + 2);
            if (comment_end < 0) {
                end_offset = source.size();
                value = value.trimmed();
                return false;
            }
            value.append(QLatin1Char(' '));
            i = comment_end + 2;
            continue;
        }
        value.append(ch);
        i++;
    }

    end_offset = source.size();
    value = value.trimmed();
    return false;
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
    int i = 0;
    while (i < source.size()) {
        if (i + 1 < source.size() &&
            source.at(i) == QLatin1Char('/') &&
            source.at(i + 1) == QLatin1Char('*')) {
            const int comment_end = source.indexOf(QStringLiteral("*/"), i + 2);
            i = comment_end < 0 ? source.size() : comment_end + 2;
            continue;
        }

        if (source.at(i) == QLatin1Char('"') || source.at(i) == QLatin1Char('\'')) {
            QString ignored;
            int ignored_offset = -1;
            int string_end = i + 1;
            parseCssQuotedString(source, i, ignored, ignored_offset, string_end);
            i = qMax(i + 1, string_end);
            continue;
        }

        if (source.at(i) == QLatin1Char('@') &&
            cssIdentifierAt(source, i + 1, QStringLiteral("import"))) {
            const int import_value_offset = skipCssWhitespaceAndComments(source, i + 7);
            if (import_value_offset < source.size() &&
                (source.at(import_value_offset) == QLatin1Char('"') ||
                 source.at(import_value_offset) == QLatin1Char('\''))) {
                QString href;
                int href_offset = -1;
                int import_end = import_value_offset + 1;
                if (parseCssQuotedString(source, import_value_offset, href, href_offset, import_end)) {
                    inspectLink(normalizer, result, context, source_bookpath, href,
                                sourceLocationForOffset(location_source, base_offset + href_offset), report_missing);
                    i = import_end;
                    continue;
                }
            }
        }

        if (cssIdentifierAt(source, i, QStringLiteral("url"))) {
            QString href;
            int href_offset = -1;
            int url_end = i + 3;
            if (parseCssUrlFunction(source, i, href, href_offset, url_end)) {
                inspectLink(normalizer, result, context, source_bookpath, href,
                            sourceLocationForOffset(location_source, base_offset + href_offset), report_missing);
                i = qMax(i + 1, url_end);
                continue;
            }
        }
        i++;
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

static bool isStyleMediaType(const QString& media_type)
{
    return media_type.trimmed().toLower() == QLatin1String("text/css");
}

static void validateManifestFallbackDiagnostics(EpubStructureNormalizer::Result& result,
                                                const QString& opfpath,
                                                const OpfSourceIndex& opf_index,
                                                const QList<ManifestEntry>& manifest,
                                                const QHash<QString, ManifestEntry>& manifest_by_id,
                                                const QString& epub_version)
{
    for (const ManifestEntry& me : manifest) {
        if (me.m_atts.keys().contains(QStringLiteral("fallback"))) {
            QString fallback_id = me.m_atts.value(QStringLiteral("fallback")).trimmed();
            if (fallback_id.isEmpty()) {
                addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback")),
                                 QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback 属性为空。").arg(me.m_id));
            } else {
                QSet<QString> seen;
                seen.insert(me.m_id);
                QString current_id = fallback_id;
                while (!current_id.isEmpty()) {
                    if (!manifest_by_id.contains(current_id)) {
                        addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                         opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback")),
                                         QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback 指向不存在的 ID【%2】。")
                                             .arg(me.m_id, current_id));
                        break;
                    }
                    if (seen.contains(current_id)) {
                        addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                         opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback")),
                                         QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback 链形成循环，循环点为【%2】。")
                                             .arg(me.m_id, current_id));
                        break;
                    }
                    seen.insert(current_id);
                    current_id = manifest_by_id.value(current_id).m_atts.value(QStringLiteral("fallback")).trimmed();
                }
            }
        }

        if (me.m_atts.keys().contains(QStringLiteral("fallback-style"))) {
            const QString fallback_style_id = me.m_atts.value(QStringLiteral("fallback-style")).trimmed();
            if (!epub_version.startsWith(QLatin1String("2"))) {
                addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback-style")),
                                 QStringLiteral("EPUBCheck：manifest 项【%1】使用 fallback-style；该属性主要属于 EPUB2 兼容路径，EPUB3 中建议改用标准 CSS 引用。")
                                     .arg(me.m_id));
            }
            if (fallback_style_id.isEmpty()) {
                addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback-style")),
                                 QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback-style 属性为空。").arg(me.m_id));
            } else if (!manifest_by_id.contains(fallback_style_id)) {
                addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback-style")),
                                 QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback-style 指向不存在的 ID【%2】。")
                                     .arg(me.m_id, fallback_style_id));
            } else if (!isStyleMediaType(manifest_by_id.value(fallback_style_id).m_mtype)) {
                addLocatedResult(result, ValidationResult::ResType_Warn, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("fallback-style")),
                                 QStringLiteral("EPUBCheck：manifest 项【%1】的 fallback-style【%2】不是 CSS 样式资源。")
                                     .arg(me.m_id, fallback_style_id));
            }
        }
    }
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

static bool hasMp4FtypSignature(const QByteArray& header)
{
    return header.size() >= 8 && header.mid(4, 4) == QByteArray("ftyp", 4);
}

static bool hasEbmlSignature(const QByteArray& header)
{
    return header.startsWith(QByteArray::fromHex("1A45DFA3"));
}

static bool hasMp3Signature(const QByteArray& header)
{
    if (header.startsWith("ID3")) {
        return true;
    }
    if (header.size() < 2) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(header.at(0));
    const unsigned char second = static_cast<unsigned char>(header.at(1));
    return first == 0xff && (second & 0xe0) == 0xe0;
}

static bool hasAacAdtsSignature(const QByteArray& header)
{
    if (header.size() < 2) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(header.at(0));
    const unsigned char second = static_cast<unsigned char>(header.at(1));
    return first == 0xff && (second == 0xf1 || second == 0xf9);
}

static bool hasFontSignature(const QByteArray& header)
{
    return header.startsWith(QByteArray::fromHex("00010000")) ||
           header.startsWith("OTTO") ||
           header.startsWith("ttcf") ||
           header.startsWith("true") ||
           header.startsWith("wOFF") ||
           header.startsWith("wOF2");
}

static bool expectedFontSignatureMatches(const QString& extension, const QByteArray& header)
{
    if (extension == QLatin1String("woff")) {
        return header.startsWith("wOFF");
    }
    if (extension == QLatin1String("woff2")) {
        return header.startsWith("wOF2");
    }
    if (extension == QLatin1String("ttc")) {
        return header.startsWith("ttcf");
    }
    if (extension == QLatin1String("ttf")) {
        return header.startsWith(QByteArray::fromHex("00010000")) || header.startsWith("true");
    }
    if (extension == QLatin1String("otf")) {
        return header.startsWith("OTTO") ||
               header.startsWith(QByteArray::fromHex("00010000")) ||
               header.startsWith("true");
    }
    return hasFontSignature(header);
}

static void validateFontDiagnostics(EpubStructureNormalizer::Result& result,
                                    Resource* resource,
                                    const QFileInfo& file_info,
                                    const QByteArray& header)
{
    const QString bookpath = resource->GetRelativePath();
    const QString extension = file_info.suffix().toLower();
    if (!hasFontSignature(header)) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                         QStringLiteral("字体检查：无法识别字体文件头，文件内容可能与扩展名或 media-type 不一致。"));
        return;
    }
    if (!expectedFontSignatureMatches(extension, header)) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                         QStringLiteral("字体检查：字体文件头与扩展名【%1】不一致，建议检查文件类型。").arg(extension));
    }
}

static void validateAudioDiagnostics(EpubStructureNormalizer::Result& result,
                                     Resource* resource,
                                     const QFileInfo& file_info,
                                     const QByteArray& header)
{
    const QString bookpath = resource->GetRelativePath();
    const QString extension = file_info.suffix().toLower();
    bool valid = true;

    if (extension == QLatin1String("mp3") || extension == QLatin1String("mpeg") || extension == QLatin1String("mpg")) {
        valid = hasMp3Signature(header);
    } else if (extension == QLatin1String("oga") || extension == QLatin1String("ogg") || extension == QLatin1String("opus")) {
        valid = header.startsWith("OggS");
    } else if (extension == QLatin1String("m4a") || extension == QLatin1String("mp4")) {
        valid = hasMp4FtypSignature(header);
    } else if (extension == QLatin1String("aac")) {
        valid = hasAacAdtsSignature(header) || hasMp4FtypSignature(header);
    }

    if (!valid) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                         QStringLiteral("音频检查：文件头与扩展名【%1】不一致，建议检查 media-type 和实际文件内容。").arg(extension));
    }
}

static void validateVideoDiagnostics(EpubStructureNormalizer::Result& result,
                                     Resource* resource,
                                     const QFileInfo& file_info,
                                     const QByteArray& header)
{
    const QString bookpath = resource->GetRelativePath();
    const QString extension = file_info.suffix().toLower();
    bool valid = true;

    if (extension == QLatin1String("mp4") || extension == QLatin1String("m4v") || extension == QLatin1String("mov")) {
        valid = hasMp4FtypSignature(header);
    } else if (extension == QLatin1String("ogv")) {
        valid = header.startsWith("OggS");
    } else if (extension == QLatin1String("webm")) {
        valid = hasEbmlSignature(header);
    } else if (extension == QLatin1String("vtt")) {
        valid = header.startsWith("WEBVTT");
    }

    if (!valid) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                         QStringLiteral("视频检查：文件头与扩展名【%1】不一致，建议检查 media-type 和实际文件内容。").arg(extension));
    }
}

static void validatePdfDiagnostics(EpubStructureNormalizer::Result& result,
                                   Resource* resource,
                                   const QByteArray& header)
{
    if (!header.startsWith("%PDF-")) {
        addLocatedResult(result, ValidationResult::ResType_Warn, resource->GetRelativePath(), SourceLocation(),
                         QStringLiteral("PDF检查：文件头不是 %PDF-，文件内容可能与扩展名或 media-type 不一致。"));
    }
}

static void validateCssDiagnostics(EpubStructureNormalizer::Result& result,
                                   Resource* resource,
                                   const QString& source)
{
    const QString bookpath = resource->GetRelativePath();
    QRegularExpression charset_re(QStringLiteral("@charset\\s+([\"'])(.*?)\\1\\s*;"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator charset_it = charset_re.globalMatch(source);
    int charset_count = 0;
    while (charset_it.hasNext()) {
        const QRegularExpressionMatch match = charset_it.next();
        charset_count++;
        QString prefix = source.left(match.capturedStart(0));
        if (prefix.startsWith(QChar(0xfeff))) {
            prefix.remove(0, 1);
        }
        if (!prefix.trimmed().isEmpty()) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                             sourceLocationForOffset(source, match.capturedStart(0)),
                             QStringLiteral("CSS检查：@charset 不是样式表中的第一条规则，阅读器可能忽略该声明。"));
        }
        if (charset_count > 1) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                             sourceLocationForOffset(source, match.capturedStart(0)),
                             QStringLiteral("CSS检查：样式表中存在多个 @charset 声明，建议只保留第一条。"));
        }
        const QString encoding = match.captured(2).trimmed().toLower();
        if (encoding != QLatin1String("utf-8") && encoding != QLatin1String("utf8")) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                             sourceLocationForOffset(source, match.capturedStart(2)),
                             QStringLiteral("CSS检查：@charset 声明为【%1】，EPUB 中建议统一使用 UTF-8。")
                                 .arg(match.captured(2)));
        }
    }

    QList<SourceLocation> brace_stack;
    bool in_comment = false;
    bool in_string = false;
    QChar string_quote;
    int token_start = -1;

    for (int i = 0; i < source.size(); i++) {
        const QChar ch = source.at(i);
        const QChar next = (i + 1 < source.size()) ? source.at(i + 1) : QChar();

        if (in_comment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                in_comment = false;
                i++;
            }
            continue;
        }

        if (in_string) {
            if (ch == QLatin1Char('\\')) {
                i++;
                continue;
            }
            if (ch == string_quote) {
                in_string = false;
            }
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            in_comment = true;
            token_start = i;
            i++;
            continue;
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            in_string = true;
            string_quote = ch;
            token_start = i;
            continue;
        }

        if (ch == QLatin1Char('{')) {
            brace_stack << sourceLocationForOffset(source, i);
        } else if (ch == QLatin1Char('}')) {
            if (brace_stack.isEmpty()) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                                 sourceLocationForOffset(source, i),
                                 QStringLiteral("CSS检查：发现没有匹配开始花括号的 }。"));
            } else {
                brace_stack.removeLast();
            }
        }
    }

    if (in_comment) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, token_start),
                         QStringLiteral("CSS检查：存在未闭合的块注释。"));
    }
    if (in_string) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, token_start),
                         QStringLiteral("CSS检查：存在未闭合的字符串。"));
    }
    for (const SourceLocation& location : brace_stack) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, location,
                         QStringLiteral("CSS检查：发现没有匹配结束花括号的 {。"));
    }
}

static bool pathContainsWhitespace(const QString& path)
{
    for (const QChar& ch : path) {
        if (ch.isSpace()) {
            return true;
        }
    }
    return false;
}

static bool pathContainsNonAscii(const QString& path)
{
    for (const QChar& ch : path) {
        if (ch.unicode() > 0x7f) {
            return true;
        }
    }
    return false;
}

static bool isAllowedMetaInfPath(const QString& bookpath)
{
    const QString normalized = bookpath.toLower();
    return normalized == QLatin1String("meta-inf/container.xml") ||
           normalized == QLatin1String("meta-inf/encryption.xml") ||
           normalized == QLatin1String("meta-inf/metadata.xml") ||
           normalized == QLatin1String("meta-inf/signatures.xml") ||
           normalized == QLatin1String("meta-inf/manifest.xml") ||
           normalized == QLatin1String("meta-inf/com.apple.ibooks.display-options.xml");
}

static QString ocfComparablePath(const QString& bookpath)
{
    return bookpath.normalized(QString::NormalizationForm_C).toCaseFolded();
}

static void validateMimetypeFile(EpubStructureNormalizer::Result& result,
                                 const QString& root_folder)
{
    QFile file(root_folder + QStringLiteral("/mimetype"));
    if (!file.exists()) {
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        addLocatedResult(result, ValidationResult::ResType_Warn, QStringLiteral("mimetype"), SourceLocation(),
                         QStringLiteral("OCF检查：无法读取 mimetype 文件。"));
        return;
    }

    const QByteArray content = file.readAll();
    if (content != QByteArray("application/epub+zip")) {
        addLocatedResult(result, ValidationResult::ResType_Warn, QStringLiteral("mimetype"), SourceLocation{1, 0},
                         QStringLiteral("OCF检查：mimetype 文件内容不是精确的 application/epub+zip；导出时会由 Sigil 重新写入。"));
    }
}

static OpfTagLocation firstXmlTag(const QString& source, const QString& tag_name)
{
    const QList<OpfTagLocation> tags = scanOpfTags(source, tag_name, 0, source.size());
    return tags.isEmpty() ? OpfTagLocation() : tags.first();
}

static void validateContainerXml(EpubStructureNormalizer::Result& result,
                                 const QString& root_folder,
                                 const QString& opf_bookpath,
                                 bool dry_run)
{
    const QString container_bookpath = QStringLiteral("META-INF/container.xml");
    const QString container_path = root_folder + QStringLiteral("/") + container_bookpath;
    QFileInfo container_info(container_path);

    if (!container_info.exists()) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath, SourceLocation(),
                         dry_run ?
                             QStringLiteral("OCF检查：缺少 META-INF/container.xml，需要按当前 OPF 路径重建。") :
                             QStringLiteral("OCF检查：缺少 META-INF/container.xml，已按当前 OPF 路径重建。"));
        result.modified = true;
        if (!dry_run) {
            FolderKeeper::UpdateContainerXML(root_folder, opf_bookpath);
        }
        return;
    }

    QFile file(container_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath, SourceLocation(),
                         QStringLiteral("OCF检查：无法读取 META-INF/container.xml。"));
        return;
    }

    const QString source = QString::fromUtf8(file.readAll());
    QXmlStreamReader reader(source);
    while (!reader.atEnd()) {
        reader.readNext();
    }
    if (reader.hasError()) {
        addLocatedResult(result, ValidationResult::ResType_Error, container_bookpath,
                         sourceLocationForLineColumn(source, reader.lineNumber(), reader.columnNumber()),
                         QStringLiteral("OCF检查：container.xml 解析错误：%1").arg(reader.errorString()));
        return;
    }

    QList<OpfTagLocation> rootfiles = scanOpfTags(source, QStringLiteral("rootfile"), 0, source.size());
    if (rootfiles.isEmpty()) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath, firstXmlTag(source, QStringLiteral("container")).tagLocation,
                         dry_run ?
                             QStringLiteral("OCF检查：container.xml 缺少 rootfile，需要按当前 OPF 路径重建。") :
                             QStringLiteral("OCF检查：container.xml 缺少 rootfile，已按当前 OPF 路径重建。"));
        result.modified = true;
        if (!dry_run) {
            FolderKeeper::UpdateContainerXML(root_folder, opf_bookpath);
        }
        return;
    }

    int selected_index = 0;
    for (int i = 0; i < rootfiles.count(); i++) {
        if (rootfiles.at(i).attrs.value(QStringLiteral("media-type")) == QLatin1String("application/oebps-package+xml")) {
            selected_index = i;
            break;
        }
    }

    const OpfTagLocation rootfile = rootfiles.at(selected_index);
    const QString full_path = rootfile.attrs.value(QStringLiteral("full-path")).trimmed();
    const QString media_type = rootfile.attrs.value(QStringLiteral("media-type")).trimmed();
    bool rewrite_container = false;

    if (full_path.isEmpty()) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath, rootfile.tagLocation,
                         dry_run ?
                             QStringLiteral("OCF检查：rootfile 缺少 full-path，需要按当前 OPF 路径重建。") :
                             QStringLiteral("OCF检查：rootfile 缺少 full-path，已按当前 OPF 路径重建。"));
        rewrite_container = true;
    } else if (full_path != opf_bookpath) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath,
                         opfAttrLocation(rootfile, QStringLiteral("full-path")),
                         (dry_run ?
                              QStringLiteral("OCF检查：rootfile full-path【%1】与当前 OPF【%2】不一致，需要重建 container.xml。") :
                              QStringLiteral("OCF检查：rootfile full-path【%1】与当前 OPF【%2】不一致，已重建 container.xml。"))
                             .arg(full_path, opf_bookpath));
        rewrite_container = true;
    }

    if (media_type != QLatin1String("application/oebps-package+xml")) {
        addLocatedResult(result, ValidationResult::ResType_Warn, container_bookpath,
                         opfAttrLocation(rootfile, QStringLiteral("media-type")),
                         dry_run ?
                             QStringLiteral("OCF检查：rootfile media-type 应为 application/oebps-package+xml，需要重建 container.xml。") :
                             QStringLiteral("OCF检查：rootfile media-type 应为 application/oebps-package+xml，已重建 container.xml。"));
        rewrite_container = true;
    }

    if (rewrite_container) {
        result.modified = true;
        if (!dry_run) {
            FolderKeeper::UpdateContainerXML(root_folder, opf_bookpath);
        }
    }
}

static void validateOcfPathDiagnostics(EpubStructureNormalizer::Result& result,
                                       const QString& root_folder)
{
    QHash<QString, QString> comparable_to_path;
    const QStringList filepaths = Utility::walkDirs(root_folder);

    foreach(QString filepath, filepaths) {
        QString bookpath = filepath.mid(root_folder.size() + 1);
        bookpath.replace(QChar('\\'), QChar('/'));

        const QString comparable = ocfComparablePath(bookpath);
        if (comparable_to_path.contains(comparable) && comparable_to_path.value(comparable) != bookpath) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件路径【%1】与【%2】在 Unicode 规范化和大小写折叠后冲突，可能在部分文件系统或阅读器中互相覆盖。")
                                 .arg(bookpath, comparable_to_path.value(comparable)));
        } else {
            comparable_to_path[comparable] = bookpath;
        }

        const QString filename = QFileInfo(bookpath).fileName();
        if (filename.endsWith(QLatin1Char('.'))) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件名【%1】以点号结尾，EPUBCheck 会报告路径兼容性风险。").arg(filename));
        }
        if (filename.startsWith(QLatin1Char('.'))) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件名【%1】是隐藏文件风格，建议不要打包进 EPUB。").arg(filename));
        }
        if (pathContainsWhitespace(bookpath)) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件路径【%1】包含空白字符，建议改名并使用 URL 编码引用。").arg(bookpath));
        }
        if (pathContainsNonAscii(bookpath)) {
            addLocatedResult(result, ValidationResult::ResType_Info, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件路径【%1】包含非 ASCII 字符，现代 EPUB 可用，但旧阅读器可能兼容性较差。").arg(bookpath));
        }
        if (bookpath.startsWith(QStringLiteral("META-INF/")) && !isAllowedMetaInfPath(bookpath)) {
            addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation(),
                             QStringLiteral("OCF检查：文件【%1】位于 META-INF 中。Publication Resource 不应放在 META-INF。").arg(bookpath));
        }
    }
}

static QString canonicalEncodingName(const QString& encoding)
{
    QString normalized = encoding.trimmed().toLower();
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char('_'));
    return normalized;
}

static void validateXmlDeclaration(EpubStructureNormalizer::Result& result,
                                   const QString& bookpath,
                                   const QString& source)
{
    QRegularExpression xml_decl_re(QStringLiteral("^\\s*<\\?xml\\b([^?]*)\\?>"),
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch decl_match = xml_decl_re.match(source);
    if (!decl_match.hasMatch()) {
        return;
    }

    QRegularExpression encoding_re(QStringLiteral("\\bencoding\\s*=\\s*([\"'])(.*?)\\1"),
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch encoding_match = encoding_re.match(decl_match.captured(1));
    if (!encoding_match.hasMatch()) {
        return;
    }

    const QString encoding = encoding_match.captured(2);
    if (canonicalEncodingName(encoding) != QLatin1String("utf8")) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, decl_match.capturedStart(1) + encoding_match.capturedStart(2)),
                         QStringLiteral("XML检查：声明的 encoding 为【%1】，EPUB 内容文档建议使用 UTF-8。").arg(encoding));
    }
}

static QList<QRegularExpressionMatch> doctypeMatches(const QString& source)
{
    QList<QRegularExpressionMatch> matches;
    QRegularExpression doctype_re(QStringLiteral("<!DOCTYPE\\b([\\s\\S]*?)>"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = doctype_re.globalMatch(source);
    while (it.hasNext()) {
        matches << it.next();
    }
    return matches;
}

static void validateExternalEntities(EpubStructureNormalizer::Result& result,
                                     const QString& bookpath,
                                     const QString& source)
{
    QRegularExpression entity_re(QStringLiteral("<!ENTITY\\s+%?\\s*[^>]*\\b(?:SYSTEM|PUBLIC)\\b[^>]*>"),
                                 QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = entity_re.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, match.capturedStart(0)),
                         QStringLiteral("XML检查：发现外部实体声明，EPUB3 不允许外部实体，且会带来解析和安全风险。"));
    }
}

static void validateDoctype(EpubStructureNormalizer::Result& result,
                            const QString& bookpath,
                            const QString& source,
                            Resource::ResourceType resource_type,
                            const QString& epub_version)
{
    const QList<QRegularExpressionMatch> matches = doctypeMatches(source);
    if (matches.count() > 1) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, matches.at(1).capturedStart(0)),
                         QStringLiteral("XML检查：同一文件中发现多个 DOCTYPE 声明，请保留一个符合 EPUB 版本的声明。"));
    }

    const bool has_doctype = !matches.isEmpty();
    const QString doctype = has_doctype ? matches.first().captured(0).simplified() : QString();
    const QString lower_doctype = doctype.toLower();

    if (resource_type == Resource::HTMLResourceType) {
        if (epub_version.startsWith(QLatin1Char('2'))) {
            if (!has_doctype) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation{1, 0},
                                 QStringLiteral("EPUBCheck：EPUB2 XHTML 建议声明 XHTML 1.1 DOCTYPE。"));
            } else if (!lower_doctype.contains(QStringLiteral("xhtml 1.1"))) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                                 sourceLocationForOffset(source, matches.first().capturedStart(0)),
                                 QStringLiteral("EPUBCheck：EPUB2 XHTML 的 DOCTYPE 不是 XHTML 1.1，建议检查文档类型声明。"));
            }
        } else if (epub_version.startsWith(QLatin1Char('3')) && has_doctype) {
            const bool html5_doctype = lower_doctype == QStringLiteral("<!doctype html>");
            const bool has_external_identifier = lower_doctype.contains(QStringLiteral(" public ")) ||
                                                 lower_doctype.contains(QStringLiteral(" system "));
            if (!html5_doctype || has_external_identifier) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                                 sourceLocationForOffset(source, matches.first().capturedStart(0)),
                                 QStringLiteral("EPUBCheck：EPUB3 XHTML 建议使用简单的 <!DOCTYPE html>，避免旧式外部标识。"));
            }
        }
        return;
    }

    if (resource_type == Resource::NCXResourceType) {
        if (epub_version.startsWith(QLatin1Char('2'))) {
            if (!has_doctype) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath, SourceLocation{1, 0},
                                 QStringLiteral("EPUBCheck：EPUB2 NCX 建议声明 NCX 2005-1 DOCTYPE。"));
            } else if (!lower_doctype.contains(QStringLiteral("ncx 2005-1"))) {
                addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                                 sourceLocationForOffset(source, matches.first().capturedStart(0)),
                                 QStringLiteral("EPUBCheck：NCX DOCTYPE 不是 NCX 2005-1，建议检查声明。"));
            }
        }
        return;
    }

    if (resource_type == Resource::OPFResourceType && has_doctype) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, matches.first().capturedStart(0)),
                         QStringLiteral("EPUBCheck：OPF package document 通常不应声明 DOCTYPE，建议移除旧式声明。"));
        return;
    }

    if ((resource_type == Resource::SVGResourceType || resource_type == Resource::XMLResourceType) &&
        has_doctype &&
        (lower_doctype.contains(QStringLiteral(" public ")) || lower_doctype.contains(QStringLiteral(" system ")))) {
        addLocatedResult(result, ValidationResult::ResType_Warn, bookpath,
                         sourceLocationForOffset(source, matches.first().capturedStart(0)),
                         QStringLiteral("XML检查：DOCTYPE 使用外部标识，建议避免依赖外部 DTD。"));
    }
}

static void validateXmlWellFormed(EpubStructureNormalizer::Result& result,
                                  const QString& bookpath,
                                  const QString& source)
{
    QXmlStreamReader reader(source);
    while (!reader.atEnd()) {
        reader.readNext();
    }

    if (!reader.hasError()) {
        return;
    }

    const SourceLocation location = sourceLocationForLineColumn(source, reader.lineNumber(), reader.columnNumber());
    addLocatedResult(result, ValidationResult::ResType_Error, bookpath, location,
                     QStringLiteral("XML检查：解析错误：%1").arg(reader.errorString()));
}

static void validateXmlTextResource(EpubStructureNormalizer::Result& result,
                                    Resource* resource)
{
    TextResource* text_resource = qobject_cast<TextResource*>(resource);
    if (!text_resource) {
        return;
    }

    const QString source = text_resource->GetText();
    const QString bookpath = resource->GetRelativePath();

    validateXmlDeclaration(result, bookpath, source);
    validateExternalEntities(result, bookpath, source);
    validateDoctype(result, bookpath, source, resource->Type(), resource->GetEpubVersion());
    validateXmlWellFormed(result, bookpath, source);
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

    appendResult(result, validateResourceDiagnostics(options));

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

EpubStructureNormalizer::Result EpubStructureNormalizer::validateResourceDiagnostics(const Options& options) const
{
    Result result;

    if (!m_Book) {
        return result;
    }

    const QString root_folder = m_Book->GetFolderKeeper()->GetFullPathToMainFolder();
    if (m_Book->GetOPF()) {
        validateContainerXml(result, root_folder, m_Book->GetOPF()->GetRelativePath(), options.dryRun);
    }
    validateMimetypeFile(result, root_folder);
    validateOcfPathDiagnostics(result, root_folder);

    const QList<Resource*> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach(Resource* resource, resources) {
        if (!resource) {
            continue;
        }

        switch (resource->Type()) {
        case Resource::HTMLResourceType:
        case Resource::SVGResourceType:
        case Resource::XMLResourceType:
        case Resource::OPFResourceType:
        case Resource::NCXResourceType:
            validateXmlTextResource(result, resource);
            break;
        default:
            break;
        }

        if (resource->Type() == Resource::CSSResourceType) {
            if (CSSResource* css_resource = qobject_cast<CSSResource*>(resource)) {
                validateCssDiagnostics(result, resource, css_resource->GetText());
            }
        }

        const QString bookpath = resource->GetRelativePath();
        const QFileInfo file_info(resource->GetFullPath());
        if (resource->Type() == Resource::ImageResourceType ||
            resource->Type() == Resource::FontResourceType ||
            resource->Type() == Resource::AudioResourceType ||
            resource->Type() == Resource::VideoResourceType ||
            resource->Type() == Resource::PdfResourceType) {
            if (!file_info.exists() || !file_info.isFile()) {
                addResult(result, ValidationResult::ResType_Warn, bookpath,
                          QStringLiteral("资源检查：文件在临时书目录中不存在，建议检查 Manifest 与 Book Browser。"));
                continue;
            }

            if (file_info.size() == 0) {
                addResult(result, ValidationResult::ResType_Warn, bookpath,
                          QStringLiteral("资源检查：文件大小为 0，阅读器无法正常显示。"));
                continue;
            }
        }

        if (resource->Type() == Resource::ImageResourceType) {
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
            continue;
        }

        const QByteArray header = readFileHeader(resource->GetFullPath(), 16);
        switch (resource->Type()) {
        case Resource::FontResourceType:
            validateFontDiagnostics(result, resource, file_info, header);
            break;
        case Resource::AudioResourceType:
            validateAudioDiagnostics(result, resource, file_info, header);
            break;
        case Resource::VideoResourceType:
            validateVideoDiagnostics(result, resource, file_info, header);
            break;
        case Resource::PdfResourceType:
            validatePdfDiagnostics(result, resource, header);
            break;
        default:
            break;
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
        context.mediaTypesByBookPath[bookpath] = resource->GetMediaType();

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

    const QString opf_source = opf->GetText();
    const OpfSourceIndex opf_index = buildOpfSourceIndex(opf_source);

    OPFParser p;
    p.parse(opf_source);

    const ValidationResult::ResType error_type = ValidationResult::ResType_Error;
    const ValidationResult::ResType warning_type = ValidationResult::ResType_Warn;
    bool opf_modified = false;

    if (!p.m_package.m_atts.contains("xmlns")) {
        addLocatedResult(result, error_type, opfpath, opf_index.packageTag.tagLocation,
                         QStringLiteral("OPF规范：找不到在package节点的xmlns属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        addLocatedResult(result, error_type, opfpath, opfAttrLocation(opf_index.packageTag, QStringLiteral("xmlns")),
                         QStringLiteral("OPF规范：不规范的xmlns属性值【%1】,建议改为\"http://www.idpf.org/2007/opf\"").arg(p.m_package.m_atts["xmlns"]));
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        addLocatedResult(result, error_type, opfpath, opf_index.metadataTag.tagLocation,
                         QStringLiteral("OPF规范：找不到在metadata节点的xmlns:dc属性，,建议以\"http://purl.org/dc/elements/1.1/\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        addLocatedResult(result, error_type, opfpath, opfAttrLocation(opf_index.metadataTag, QStringLiteral("xmlns:dc")),
                         QStringLiteral("OPF规范：不规范的xmlns:dc属性值【%1】，建议改为\"http://purl.org/dc/elements/1.1/\"").arg(p.m_metans.m_atts["xmlns:dc"]));
    }

    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        addLocatedResult(result, error_type, opfpath, opf_index.metadataTag.tagLocation,
                         QStringLiteral("OPF规范：找不到在metadata节点的xmlns:opf属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        addLocatedResult(result, error_type, opfpath, opfAttrLocation(opf_index.metadataTag, QStringLiteral("xmlns:opf")),
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
            addLocatedResult(result, error_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("id")),
                             QStringLiteral("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (id != manifest_key_to_id[href_info.key]) {
            addLocatedResult(result, error_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("id")),
                             QStringLiteral("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (!href_info.isLocal) {
            if (href_info.isFileUrl) {
                addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                                 QStringLiteral("EPUBCheck：manifest href【%1】使用 file: URL，建议改为书内相对路径或移除。").arg(me.m_href));
            } else if (href_info.isDataUrl) {
                addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                                 QStringLiteral("EPUBCheck：manifest href【%1】使用 data: URL，EPUB3 中不允许作为 Manifest 资源。").arg(me.m_href.left(64)));
            } else if (p.m_package.m_version.startsWith("3") && !isRemoteManifestMediaTypeAllowed(me.m_mtype)) {
                addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("media-type")),
                                 QStringLiteral("EPUBCheck：EPUB3 Manifest 远程资源【%1】的 media-type【%2】通常只允许音频、视频或字体。")
                                     .arg(me.m_href, me.m_mtype));
            } else if (!p.m_package.m_version.startsWith("3")) {
                addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                                 QStringLiteral("EPUBCheck：Manifest href【%1】是远程资源，EPUB2 通常要求 Manifest 资源为包内文件。").arg(me.m_href));
            }
            new_manifest.append(me);
            continue;
        }

        if (lower_bkpath == QLatin1String("mimetype") || lower_bkpath.startsWith(QStringLiteral("meta-inf/"))) {
            addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("OCF检查：Manifest 项【%1】指向 mimetype 或 META-INF 内文件，Publication Resource 不应登记在这些位置。").arg(me.m_href));
        }

        if (href_info.hasQuery || href_info.hasFragment) {
            addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("EPUBCheck：manifest href【%1】包含 query 或 fragment，已按资源路径规范化。").arg(me.m_href));
            opf_modified = true;
        }

        if (!lower_bkpath_to_original_bkpath.contains(lower_bkpath)) {
            addLocatedResult(result, error_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("OPF规范：无效OPF超链接：在manifest项发现无效href【%1】，已进行删除对应项的处理。").arg(me.m_href));
            opf_modified = true;
            continue;
        }

        QString original_bkpath = lower_bkpath_to_original_bkpath[lower_bkpath];
        if (original_bkpath == opf->GetRelativePath()) {
            addLocatedResult(result, error_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("EPUBCheck：manifest 不应登记 OPF package document 自身，已删除 href【%1】。").arg(me.m_href));
            opf_modified = true;
            continue;
        }

        if (bkpath != original_bkpath) {
            addLocatedResult(result, error_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("OPF规范：大小写不一致：发现 OPF 超链接【%1】与实际路径大小写不一致，已自动校正。").arg(me.m_href));
            opf_modified = true;
        }
        me.m_href = manifestHrefForBookPath(original_bkpath, opf->GetFolder());

        QString expected_media_type = mediaTypeForBookPath(original_bkpath, tempfolder);
        if (!expected_media_type.isEmpty() && me.m_mtype != expected_media_type) {
            addLocatedResult(result, warning_type, opfpath, opfManifestLocation(opf_index, i, QStringLiteral("media-type")),
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
            addResult(result, warning_type, opfpath,
                      QStringLiteral("OPF规范：新增 XHTML 文件【%1】未在 spine 中登记，已自动追加阅读顺序项【%2】。")
                          .arg(bkpath, me.m_id));
        }

        addResult(result, warning_type, opfpath,
                  QStringLiteral("OPF规范：发现文件【%1】未登记到Manifest，已自动登记。").arg(bkpath));
    }

    if (p.m_package.m_version.startsWith("3")) {
        if (nav_ids.count() > 1) {
            addLocatedResult(result, warning_type, opfpath,
                             opfManifestLocationById(opf_index, nav_ids.at(1), QStringLiteral("properties")),
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
                addLocatedResult(result, warning_type, opfpath,
                                 opfManifestLocationById(opf_index, me.m_id, QStringLiteral("id")),
                                 QStringLiteral("EPUBCheck：EPUB3 未标记导航文档，已为 manifest 项【%1】添加 properties=\"nav\"。").arg(me.m_id));
                opf_modified = true;
            } else {
                addLocatedResult(result, warning_type, opfpath, opf_index.packageTag.tagLocation,
                                 QStringLiteral("EPUBCheck：EPUB3 Manifest 中没有唯一的 nav 导航文档，请手动确认并设置 properties=\"nav\"。"));
            }
        } else {
            ManifestEntry nav_entry = manifest_by_id.value(nav_ids.first());
            if (nav_entry.m_mtype != "application/xhtml+xml") {
                addLocatedResult(result, warning_type, opfpath,
                                 opfManifestLocationById(opf_index, nav_entry.m_id, QStringLiteral("media-type")),
                                 QStringLiteral("EPUBCheck：nav 导航文档【%1】的 media-type 应为 application/xhtml+xml。").arg(nav_entry.m_id));
            }
        }
    }

    for (int i = 0; i < new_manifest.count(); i++) {
        ManifestEntry me = new_manifest.at(i);
        if (hasProperty(me, "cover-image") && !isImageMediaType(me.m_mtype)) {
            addLocatedResult(result, warning_type, opfpath,
                             opfManifestLocationById(opf_index, me.m_id, QStringLiteral("properties")),
                             QStringLiteral("EPUBCheck：manifest 项【%1】声明 cover-image，但 media-type【%2】不是图片类型。")
                                 .arg(me.m_id, me.m_mtype));
        }
        if (hasProperty(me, "nav") && me.m_mtype != "application/xhtml+xml") {
            addLocatedResult(result, warning_type, opfpath,
                             opfManifestLocationById(opf_index, me.m_id, QStringLiteral("properties")),
                             QStringLiteral("EPUBCheck：manifest 项【%1】声明 nav，但 media-type【%2】不是 application/xhtml+xml。")
                                 .arg(me.m_id, me.m_mtype));
        }
        if (p.m_package.m_version.startsWith("3") && me.m_id == cover_idref &&
            isImageMediaType(me.m_mtype) && !hasProperty(me, "cover-image")) {
            addProperty(me, "cover-image");
            new_manifest[i] = me;
            manifest_by_id[me.m_id] = me;
            addLocatedResult(result, warning_type, opfpath,
                             opfManifestLocationById(opf_index, me.m_id, QStringLiteral("id")),
                             QStringLiteral("EPUBCheck：EPUB3 封面图片【%1】缺少 cover-image 属性，已自动补齐。").arg(me.m_id));
            opf_modified = true;
        }
    }

    validateManifestFallbackDiagnostics(result, opfpath, opf_index, new_manifest,
                                        manifest_by_id, p.m_package.m_version);

    if (!cover_idref.isEmpty() && !all_ids_without_duplication.contains(cover_idref)) {
        addLocatedResult(result, warning_type, opfpath, opfCoverMetaLocation(opf_index, cover_idref),
                         QStringLiteral("OPF规范：无效ID引用：在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。").arg(cover_idref));
    }

    bool has_linear_spine_item = false;
    QSet<QString> seen_spine_ids;
    for (int i = 0; i < p.m_spine.count(); i++) {
        SpineEntry se = p.m_spine.at(i);
        QString idref = se.m_idref;
        if (!all_ids_without_duplication.contains(idref)) {
            addLocatedResult(result, warning_type, opfpath, opfSpineLocation(opf_index, i, QStringLiteral("idref")),
                             QStringLiteral("OPF规范：无效ID引用：在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。").arg(idref));
            continue;
        }

        ManifestEntry spine_entry = manifest_by_id.value(idref);
        ManifestHrefInfo spine_href_info = manifestHrefInfo(spine_entry.m_href, opf->GetFolder());
        if (!spine_href_info.isLocal) {
            addLocatedResult(result, warning_type, opfpath, opfSpineLocation(opf_index, i, QStringLiteral("idref")),
                             QStringLiteral("EPUBCheck：spine 项【%1】引用远程 Manifest 资源，阅读器可能无法作为阅读顺序内容处理。").arg(idref));
        }

        if (!isContentDocumentMediaType(spine_entry.m_mtype, p.m_package.m_version)) {
            const QString fallback_id = spine_entry.m_atts.value("fallback", "").trimmed();
            if (fallback_id.isEmpty()) {
                addLocatedResult(result, warning_type, opfpath, opfSpineLocation(opf_index, i, QStringLiteral("idref")),
                                 QStringLiteral("EPUBCheck：spine 项【%1】的 media-type【%2】不是内容文档，且缺少 fallback。")
                                     .arg(idref, spine_entry.m_mtype));
            } else if (!hasContentDocumentFallback(spine_entry, manifest_by_id, p.m_package.m_version)) {
                addLocatedResult(result, warning_type, opfpath, opfManifestLocationById(opf_index, idref, QStringLiteral("fallback")),
                                 QStringLiteral("EPUBCheck：spine 项【%1】的 fallback 链未指向可阅读内容文档。").arg(idref));
            }
        }

        if (se.m_atts.value("linear", "yes").toLower() != "no") {
            has_linear_spine_item = true;
        }

        if (p.m_package.m_version.startsWith("2")) {
            if (seen_spine_ids.contains(idref)) {
                addLocatedResult(result, warning_type, opfpath, opfSpineLocation(opf_index, i, QStringLiteral("idref")),
                                 QStringLiteral("EPUBCheck：EPUB2 spine 中重复引用了 manifest 项【%1】。").arg(idref));
            }
            seen_spine_ids.insert(idref);
        }
    }

    if (!p.m_spine.isEmpty() && !has_linear_spine_item) {
        addLocatedResult(result, warning_type, opfpath, opfSpineLocation(opf_index, 0, QStringLiteral("idref")),
                         QStringLiteral("EPUBCheck：spine 中没有 linear=\"yes\" 的阅读顺序项目，阅读器可能无法确定主要内容。"));
    }

    for (int i = 0; i < p.m_guide.count(); i++) {
        GuideEntry ge = p.m_guide.at(i);
        QUrl guide_url(ge.m_href);
        if (!guide_url.isRelative() || guide_url.hasQuery()) {
            addLocatedResult(result, warning_type, opfpath, opfGuideLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("EPUBCheck：guide 引用【%1】不是规范的书内相对链接。").arg(ge.m_href));
            continue;
        }

        const QString guide_bkpath = Utility::buildBookPath(Utility::URLDecodePath(guide_url.path()), opf->GetFolder());
        if (!manifest_by_lower_bkpath.contains(guide_bkpath.toLower())) {
            addLocatedResult(result, warning_type, opfpath, opfGuideLocation(opf_index, i, QStringLiteral("href")),
                             QStringLiteral("EPUBCheck：guide 引用【%1】没有对应的 Manifest 内容项。").arg(ge.m_href));
            continue;
        }

        ManifestEntry guide_entry = manifest_by_lower_bkpath.value(guide_bkpath.toLower());
        if (!isContentDocumentMediaType(guide_entry.m_mtype, p.m_package.m_version)) {
            addLocatedResult(result, warning_type, opfpath, opfGuideLocation(opf_index, i, QStringLiteral("href")),
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
