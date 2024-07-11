#include "MainUI/ValidationResultsView.h"
#include <qtextcodec.h>
#include "Parsers/OPFParser.h"
#include "MainUI/BookBrowser.h"
#include "Misc/HTMLEncodingResolver.h"
#include "BookManipulation/XhtmlDoc.h"

//------------------------modified: well formed check----------------

void ValidationResultsView::ValidateCurrentBook_M()
{
    ClearResults();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_Book->SaveAllResourcesToDisk();
    QList<ValidationResult> results = validateXhtml() + correctOPF();
    QApplication::restoreOverrideCursor();
    DisplayResults(results);
    show();
    raise();
}
//-------------------------------------------------------------------

//------------------------modified: validateXhtml--------------------------
QList<ValidationResult> ValidationResultsView::validateXhtml()
{
    QList<ValidationResult> results;
    QList<Resource*> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach(Resource * resource, resources) {
        if (resource->Type() == Resource::HTMLResourceType) {
            QString source = Utility::ReadUnicodeTextFile(resource->GetFullPath());
            XhtmlDoc::WellFormedError error = XhtmlDoc::WellFormedErrorForSource(source);
            if (error.line != -1) {
                ValidationResult::ResType vtype = ValidationResult::ResType_Error;
                QString bookpath = resource->GetRelativePath();
                int lineno = error.line;
                int colno = error.column;
                QString msg = error.message;
                results.append(ValidationResult(vtype, bookpath, lineno, colno, msg));
            }
        }
    }
    return results;
}
//-------------------------------------------------------------------------

//------------------------modified: correctOPF-------------------------
void ValidationResultsView::SetBookBrowser(BookBrowser* bookbrowser)
{
    m_BookBrowser = bookbrowser;
    ClearResults();
}
//---------------------------------------------------------------------

//----------------------------------------------------modified: correctOPF-------------------------------------------------
QList<ValidationResult> ValidationResultsView::correctOPF()
{
    QList<ValidationResult> results;
    OPFResource* opf = m_Book->GetOPF();
    QString opfpath = opf->GetCurrentBookRelPath();
    ValidationResult::ResType error_type = ValidationResult::ResType_Error;
    ValidationResult::ResType warning_type = ValidationResult::ResType_Warn;
    QTextCodec* codec = QTextCodec::codecForName("UTF-8");

    OPFParser p;
    p.parse(opf->GetText());
    // Inspect the namespace

    if (!p.m_package.m_atts.contains("xmlns")) {
        QString msg = codec->toUnicode("OPF规范：找不到在package节点的xmlns属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。");
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }
    else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        QString msg = codec->toUnicode("OPF规范：不规范的xmlns属性值【%1】,建议改为\"http://www.idpf.org/2007/opf\"").arg(p.m_package.m_atts["xmlns"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        QString msg = codec->toUnicode("OPF规范：找不到在metadata节点的xmlns:dc属性，,建议以\"http://purl.org/dc/elements/1.1/\"值补上该属性。");
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }
    else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        QString msg = codec->toUnicode("OPF规范：不规范的xmlns:dc属性值【%1】，建议改为\"http://purl.org/dc/elements/1.1/\"").arg(p.m_metans.m_atts["xmlns:dc"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }
    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        QString msg = codec->toUnicode("OPF规范：找不到在metadata节点的xmlns:opf属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。");
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }
    else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        QString msg = codec->toUnicode("OPF规范：不规范的xmlns:opf属性值【%1】，建议改为\"http://www.idpf.org/2007/opf\"").arg(p.m_metans.m_atts["xmlns:opf"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }


    // Inspect the duplicate id, invalid href, invalid idref
    QString tempfolder = m_Book->GetFolderKeeper()->GetFullPathToMainFolder();
    QStringList filepathList = Utility::walkDirs(tempfolder);
    QHash<QString, QString> loewrBkPath_to_oriBkPath;

    foreach(QString fpath, filepathList) {
        QString bkpath = fpath.mid(tempfolder.size() + 1);
        loewrBkPath_to_oriBkPath[bkpath.toLower()] = bkpath;
    }

    QStringList SpineIdrefList;
    foreach(SpineEntry se, p.m_spine) {
        SpineIdrefList << se.m_idref;
    }

    QString cover_idref = "";
    foreach(MetaEntry meta, p.m_metadata) {
        if (meta.m_name == "meta" && meta.m_atts["name"] == "cover") {
            cover_idref = meta.m_atts["content"];
        }
    }

    QStringList AllIdrefList;
    AllIdrefList = SpineIdrefList;
    if (cover_idref != "") {
        AllIdrefList.prepend(cover_idref);
    }

    QHash<QString, QString> lowerHref_to_id, id_to_lowerHref;
    foreach(ManifestEntry me, p.m_manifest) {
        bool duplicate = false;
        QString lowerHref = me.m_href.toLower();
        // Found dulplicate hrefs in manifest items.
        if (lowerHref_to_id.contains(lowerHref)) {
            // Prioritize selecting the ID referenced by spine.
            if (AllIdrefList.contains(me.m_id)) {
                lowerHref_to_id[lowerHref] = me.m_id;
            }
        }
        else {
            lowerHref_to_id[lowerHref] = me.m_id;
        }

        // Found dulplicate IDs in manifest items.
        if (id_to_lowerHref.contains(me.m_id)) {
            // Prioritize selecting the Href existing actually.
            QString lowerBkpath = Utility::buildBookPath(lowerHref, opf->GetFolder()).toLower();
            if (loewrBkPath_to_oriBkPath.contains(lowerBkpath)) {
                id_to_lowerHref[me.m_id] = lowerHref;
            }
        }
        else {
            id_to_lowerHref[me.m_id] = lowerHref;
        }
    }

    QList<ManifestEntry> new_manifest;
    for (int i = 0; i < p.m_manifest.count(); i++) {
        ManifestEntry me = p.m_manifest.at(i);
        QString lowerHref = me.m_href.toLower();
        QString id = me.m_id;
        // nonunique_id : More than one hrefs have bound with the same id;
        if (lowerHref != id_to_lowerHref[id]) {
            QString msg = codec->toUnicode("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。").arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            continue; // continue for avioding the new_mainifest to append this item, which would delete the item indirectly.
        }
        // unnecessary_id : More than one ids have bound with the same href;
        if (id != lowerHref_to_id[lowerHref]) {
            QString msg = codec->toUnicode("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。").arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            continue;
        }
        // invalid href : The hrefs which can not access existed files.
        QString bkpath = Utility::buildBookPath(me.m_href, opf->GetFolder());
        QString lowerBkpath = bkpath.toLower();
        if (!loewrBkPath_to_oriBkPath.contains(lowerBkpath)) {
            QString msg = codec->toUnicode("OPF规范：无效OPF超链接：在manifest项发现无效href【%1】，已进行删除对应项的处理。").arg(me.m_href);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            continue;
        }
        QString oriBkpath = loewrBkPath_to_oriBkPath[lowerBkpath];

        // inconsistent case : The href existed but the case is inconsistent with the actual path.
        if (bkpath != oriBkpath) {
            QString msg = codec->toUnicode("OPF规范：大小写不一致：发现 OPF 超链接【%1】与实际路径大小写不一致，已自动校正。").arg(me.m_href);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            // correct inconsistent case
            QString href = Utility::relativePath(oriBkpath,opf->GetFolder());
            me.m_href = href;
        }
        new_manifest.append(me);
    }

    // Before inspect the invalid id reference on spine or metadate node,
    // we should keep the manifest id list away from duplication and invalidation.
    QStringList AllIdWithoutDuplication;
    foreach(ManifestEntry me, new_manifest) {
        AllIdWithoutDuplication << me.m_id;
    }

    // Add new items;
    // Files that exist but not registered on manifest, would be registered here;
    QStringList lowerBkpathOnManifest;
    foreach(ManifestEntry me, new_manifest) {
        QString bkpath = Utility::buildBookPath(me.m_href, opf->GetFolder());
        lowerBkpathOnManifest << bkpath.toLower();
    }
    bool RefreshBookBrowser = false;
    foreach(QString filepath, filepathList) {
        QString bkpath = filepath.mid(tempfolder.size() + 1);
        QString lowerBkpath = bkpath.toLower();

        if (lowerBkpathOnManifest.contains(lowerBkpath))
            continue;

        QFileInfo bkpath_info = QFileInfo(bkpath);
        QString ext = bkpath_info.suffix().toLower();
        if (ext == "xml" || ext == "opf" || Utility::ExtToMTypeMap(ext) == "")
            continue;

        RefreshBookBrowser = true;
        // add new resource
        if (!m_Book->GetFolderKeeper()->GetAllBookPaths().contains(bkpath)) {
            Resource* resource = m_Book->GetFolderKeeper()->AddContentFileToFolder(filepath, false, Utility::ExtToMTypeMap(ext), bkpath);
            if (resource->Type() == Resource::HTMLResourceType) {
                HTMLResource* hresource = qobject_cast<HTMLResource*>(resource);
                hresource->SetText(HTMLEncodingResolver::ReadHTMLFile(filepath));
            }
        }
        // add manifest item
        ManifestEntry me;
        me.m_href = Utility::buildRelativePath(opf->GetRelativePath(), bkpath);
        QString unique_id = bkpath_info.baseName();
        while (AllIdWithoutDuplication.contains(unique_id)) {
            unique_id = "_" + unique_id;
        }
        AllIdWithoutDuplication << unique_id;
        me.m_id = unique_id;
        me.m_mtype = Utility::ExtToMTypeMap(ext);
        new_manifest << me;
        if (me.m_mtype == "application/xhtml+xml" && !SpineIdrefList.contains(me.m_id)) {
            SpineEntry se;
            se.m_idref = me.m_id;
            p.m_spine << se;
        }
        QString msg = codec->toUnicode("OPF规范：发现文件【%1】未登记到Manifest，已自动登记。").arg(bkpath);
        results << ValidationResult(warning_type, opfpath, -1, -1, msg);
    }

    // 检查引用
    // 检查 cover 的id引用
    if (cover_idref != "") {
        if (!AllIdWithoutDuplication.contains(cover_idref)) {
            // cover_idref无效
            QString msg = codec->toUnicode("OPF规范：无效ID引用：在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。").arg(cover_idref);
            results << ValidationResult(warning_type, opfpath, -1, -1, msg);
        }
    }
    // 检查spine的idref
    foreach(SpineEntry se, p.m_spine) {
        QString idref = se.m_idref;
        if (!AllIdWithoutDuplication.contains(idref)) {
            // cover_idref无效
            QString msg = codec->toUnicode("OPF规范：无效ID引用：在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。").arg(idref);
            results << ValidationResult(warning_type, opfpath, -1, -1, msg);
        }
    }

    if (!results.isEmpty()) {
        p.m_manifest = new_manifest;
        QString new_opf = p.convert_to_xml();
        opf->SetText(new_opf);
        if (RefreshBookBrowser)
            m_BookBrowser->Refresh();
    }

    return results;
}
//-------------------------------------------------------------------------------------------------------------------------