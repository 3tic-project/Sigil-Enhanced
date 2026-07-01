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

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "Misc/HTMLEncodingResolver.h"
#include "Misc/Utility.h"
#include "Parsers/OPFParser.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "SourceUpdates/UniversalUpdates.h"

namespace BuiltinPlugins
{

namespace
{

struct BookPathIndex {
    QHash<QString, QStringList> lowerToBookPaths;
    QSet<QString> exactBookPaths;
};

struct LinkScanContext {
    BookPathIndex index;
    QHash<QString, QString> updates;
    QSet<QString> reported;
};

static bool shouldSkipLink(const QString& raw_link)
{
    const QString link = raw_link.trimmed();
    if (link.isEmpty() || link.startsWith("#") || link.startsWith("//")) {
        return true;
    }

    QUrl url(link);
    return !url.isRelative() || url.hasQuery();
}

static QString resolvedBookPath(const QString& raw_link, const QString& source_bookpath)
{
    QUrl url(raw_link.trimmed());
    QString href_path = url.path();
    if (href_path.isEmpty()) {
        return QString();
    }

    return Utility::buildBookPath(href_path, QFileInfo(source_bookpath).dir().path());
}

static void addUniqueResult(EpubStructureNormalizer::Result& result,
                            LinkScanContext& context,
                            ValidationResult::ResType type,
                            const QString& source_bookpath,
                            const QString& message)
{
    const QString key = QString::number(type) + "|" + source_bookpath + "|" + message;
    if (context.reported.contains(key)) {
        return;
    }
    context.reported.insert(key);
    result.validationResults << ValidationResult(type, source_bookpath, -1, -1, message);
}

static void inspectLink(EpubStructureNormalizer* normalizer,
                        EpubStructureNormalizer::Result& result,
                        LinkScanContext& context,
                        const QString& source_bookpath,
                        const QString& raw_link,
                        bool report_missing)
{
    Q_UNUSED(normalizer);

    if (shouldSkipLink(raw_link)) {
        return;
    }

    const QString target_bookpath = resolvedBookPath(raw_link, source_bookpath);
    if (target_bookpath.isEmpty() || context.index.exactBookPaths.contains(target_bookpath)) {
        return;
    }

    const QString lower_target = target_bookpath.toLower();
    if (!context.index.lowerToBookPaths.contains(lower_target)) {
        if (report_missing) {
            addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath,
                            QStringLiteral("链接检查：发现无法定位的书内链接【%1】。").arg(raw_link));
        }
        return;
    }

    const QStringList matches = context.index.lowerToBookPaths.value(lower_target);
    if (matches.count() != 1) {
        addUniqueResult(result, context, ValidationResult::ResType_Warn, source_bookpath,
                        QStringLiteral("链接检查：链接【%1】存在大小写歧义，未自动修改。").arg(raw_link));
        return;
    }

    const QString actual_bookpath = matches.first();
    if (actual_bookpath == target_bookpath) {
        return;
    }

    context.updates[target_bookpath] = actual_bookpath;
    addUniqueResult(result, context, ValidationResult::ResType_Info, source_bookpath,
                    QStringLiteral("链接检查：链接【%1】与实际文件路径大小写不一致，已计划修正为【%2】。")
                        .arg(raw_link, actual_bookpath));
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
        inspectLink(normalizer, result, context, source_bookpath, match.captured(2), true);
    }
}

static void scanCssUrls(EpubStructureNormalizer* normalizer,
                        EpubStructureNormalizer::Result& result,
                        LinkScanContext& context,
                        const QString& source_bookpath,
                        const QString& source,
                        bool report_missing)
{
    QRegularExpression url_re(QStringLiteral("url\\(\\s*[\"']?([^\\(\\)\"']*)[\"']?\\s*\\)"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator url_it = url_re.globalMatch(source);
    while (url_it.hasNext()) {
        QRegularExpressionMatch match = url_it.next();
        inspectLink(normalizer, result, context, source_bookpath, match.captured(1), report_missing);
    }

    QRegularExpression import_re(QStringLiteral("@import\\s+(?:url\\(\\s*[\"']?([^\\(\\)\"']*)[\"']?\\s*\\)|[\"']([^\"']*)[\"'])"),
                                 QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator import_it = import_re.globalMatch(source);
    while (import_it.hasNext()) {
        QRegularExpressionMatch match = import_it.next();
        QString href = match.captured(1);
        if (href.isEmpty()) {
            href = match.captured(2);
        }
        inspectLink(normalizer, result, context, source_bookpath, href, report_missing);
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
        scanCssUrls(normalizer, result, context, source_bookpath, match.captured(2), true);
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
        inspectLink(normalizer, result, context, source_bookpath, match.captured(2), true);
    }
}

static QString manifestBookPath(const QString& href, const QString& opf_folder)
{
    return Utility::buildBookPath(Utility::URLDecodePath(href), opf_folder);
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
            scanCssUrls(this, result, context, bookpath, css_resource->GetText(), false);
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

    QHash<QString, QString> lower_bookpath_to_id;
    QHash<QString, QString> id_to_lower_bookpath;
    foreach(ManifestEntry me, p.m_manifest) {
        QString lower_bookpath = manifestBookPath(me.m_href, opf->GetFolder()).toLower();

        if (lower_bookpath_to_id.contains(lower_bookpath)) {
            if (all_idref_list.contains(me.m_id)) {
                lower_bookpath_to_id[lower_bookpath] = me.m_id;
            }
        } else {
            lower_bookpath_to_id[lower_bookpath] = me.m_id;
        }

        if (id_to_lower_bookpath.contains(me.m_id)) {
            if (lower_bkpath_to_original_bkpath.contains(lower_bookpath)) {
                id_to_lower_bookpath[me.m_id] = lower_bookpath;
            }
        } else {
            id_to_lower_bookpath[me.m_id] = lower_bookpath;
        }
    }

    QList<ManifestEntry> new_manifest;
    for (int i = 0; i < p.m_manifest.count(); i++) {
        ManifestEntry me = p.m_manifest.at(i);
        QString bkpath = manifestBookPath(me.m_href, opf->GetFolder());
        QString lower_bkpath = bkpath.toLower();
        QString id = me.m_id;

        if (lower_bkpath != id_to_lower_bookpath[id]) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (id != lower_bookpath_to_id[lower_bkpath]) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。").arg(id));
            opf_modified = true;
            continue;
        }

        if (!lower_bkpath_to_original_bkpath.contains(lower_bkpath)) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：无效OPF超链接：在manifest项发现无效href【%1】，已进行删除对应项的处理。").arg(me.m_href));
            opf_modified = true;
            continue;
        }

        QString original_bkpath = lower_bkpath_to_original_bkpath[lower_bkpath];
        if (bkpath != original_bkpath) {
            addResult(result, error_type, opfpath,
                      QStringLiteral("OPF规范：大小写不一致：发现 OPF 超链接【%1】与实际路径大小写不一致，已自动校正。").arg(me.m_href));
            me.m_href = Utility::URLEncodePath(Utility::relativePath(original_bkpath, opf->GetFolder()));
            opf_modified = true;
        }

        new_manifest.append(me);
    }

    QStringList all_ids_without_duplication;
    foreach(ManifestEntry me, new_manifest) {
        all_ids_without_duplication << me.m_id;
    }

    QStringList lower_bkpath_on_manifest;
    foreach(ManifestEntry me, new_manifest) {
        QString bkpath = manifestBookPath(me.m_href, opf->GetFolder());
        lower_bkpath_on_manifest << bkpath.toLower();
    }

    foreach(QString filepath, filepath_list) {
        QString bkpath = filepath.mid(tempfolder.size() + 1);
        QString lower_bkpath = bkpath.toLower();

        if (lower_bkpath_on_manifest.contains(lower_bkpath)) {
            continue;
        }

        QFileInfo bkpath_info = QFileInfo(bkpath);
        QString ext = bkpath_info.suffix().toLower();
        QString media_type = Utility::ExtToMTypeMap(ext);
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
        lower_bkpath_on_manifest << lower_bkpath;

        if (me.m_mtype == "application/xhtml+xml" && !spine_idref_list.contains(me.m_id)) {
            SpineEntry se;
            se.m_idref = me.m_id;
            p.m_spine << se;
        }

        addResult(result, warning_type, opfpath,
                  QStringLiteral("OPF规范：发现文件【%1】未登记到Manifest，已自动登记。").arg(bkpath));
    }

    if (!cover_idref.isEmpty() && !all_ids_without_duplication.contains(cover_idref)) {
        addResult(result, warning_type, opfpath,
                  QStringLiteral("OPF规范：无效ID引用：在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。").arg(cover_idref));
    }

    foreach(SpineEntry se, p.m_spine) {
        QString idref = se.m_idref;
        if (!all_ids_without_duplication.contains(idref)) {
            addResult(result, warning_type, opfpath,
                      QStringLiteral("OPF规范：无效ID引用：在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。").arg(idref));
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
