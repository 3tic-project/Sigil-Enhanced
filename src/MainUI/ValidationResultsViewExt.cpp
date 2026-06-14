#include "MainUI/ValidationResultsView.h"
#include <qtextcodec.h>  // modified: correctOPF
#include "Parsers/OPFParser.h" // modified: correctOPF
#include "MainUI/BookBrowser.h" // modified: correctOPF
#include "Misc/HTMLEncodingResolver.h" // modified: correctOPF

//------------------------modified: correctOPF-------------------------
void ValidationResultsView::SetBookBrowser(BookBrowser* bookbrowser)
{
    m_BookBrowser = bookbrowser;
    ClearResults();
}
//---------------------------------------------------------------------

//----------------------------------------------------modified: correctOPF-------------------------------------------------
void ValidationResultsView::correctOPF()
{
    ClearResults();
    QList<pair<QString, QString>> log;
    OPFResource* opf = m_Book->GetOPF();
    OPFParser p;
    p.parse(opf->GetText());

    // Inspect the namespace

    if (!p.m_package.m_atts.contains("xmlns")) {
        log.append(pair<QString, QString>{"xmlns missing", "package"});
    }
    else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        log.append(pair<QString, QString>{ "xmlns error", p.m_package.m_atts["xmlns"] });
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        log.append(pair<QString, QString>{ "xmlns:dc missing", "metadata" });
    }
    else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        log.append(pair<QString, QString>{ "xmlns:dc error", p.m_metans.m_atts["xmlns:dc"] });
    }
    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        log.append(pair<QString, QString>{ "xmlns:opf missing", "metadata" });
    }
    else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        log.append(pair<QString, QString>{ "xmlns:dc error", p.m_metans.m_atts["xmlns:opf"] });
    }


    // Inspect the duplicate id, invalid href, invalid idref
    QString tempfolder = m_Book->GetFolderKeeper()->GetFullPathToMainFolder();
    QStringList filepathList = Utility::walkDirs(tempfolder);
    QStringList bookpathList;
    foreach(QString fpath, filepathList) {
        QString bkpath = fpath.mid(tempfolder.size() + 1);
        bookpathList.append(bkpath);
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

    QHash<QString, QString> href_to_id, id_to_href;
    QList<ManifestEntry> new_manifest;
    foreach(ManifestEntry me, p.m_manifest) {
        bool duplicate = false;
        if (href_to_id.contains(me.m_href)) {
            if (me.m_id == href_to_id[me.m_href]) {
                log.append({ "non unique id", me.m_id });
                duplicate = true;
            }
            if (AllIdrefList.contains(me.m_id)) {
                href_to_id[me.m_href] = me.m_id;
            }
        }
        else {
            href_to_id[me.m_href] = me.m_id;
        }
        if (id_to_href.contains(me.m_id)) {
            if (me.m_href == id_to_href[me.m_id]) {
                log.append({ "non unique id", me.m_id });
                duplicate = true;
            }
            QString bkpath = Utility::buildBookPath(me.m_href, opf->GetFolder());
            if (bookpathList.contains(bkpath)) {
                id_to_href[me.m_id] = me.m_href;
            }
        }
        else {
            id_to_href[me.m_id] = me.m_href;
        }
        if (duplicate)
            continue;
        new_manifest.append(me);
    }

    new_manifest.clear();
    for (int i = 0; i < p.m_manifest.count(); i++) {
        ManifestEntry me = p.m_manifest.at(i);
        QString href = me.m_href;
        QString id = me.m_id;
        // nonunique_id : More than one hrefs have bound with the same id;
        if (href != id_to_href[id]) {
            log.append({ "non unique id", id });
            continue; // continue for avioding the new_mainifest to append this item, which would delete the item indirectly.
        }
        // unnecessary_id : More than one ids have bound with the same href;
        if (id != href_to_id[href]) {
            log.append({ "unnecessary id", id });
            continue;
        }
        // invalid href : The hrefs which can not access existed files.
        QString bkpath = Utility::buildBookPath(href, opf->GetFolder());
        if (!bookpathList.contains(bkpath)) {
            log.append({ "invalid href", href });
            continue;
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
    QStringList bkpathOnManifest;
    foreach(ManifestEntry me, new_manifest) {
        QString bkpath = Utility::buildBookPath(me.m_href, opf->GetFolder());
        bkpathOnManifest << bkpath;
    }
    bool RefreshBookBrowser = false;
    foreach(QString filepath, filepathList) {
        QString bkpath = filepath.mid(tempfolder.size() + 1);
        QFileInfo bkpath_info = QFileInfo(bkpath);

        if (bkpathOnManifest.contains(bkpath))
            continue;
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
        log.append({ "new item",bkpath });
    }


    // 检查引用
    // 检查 cover 的id引用
    if (cover_idref != "") {
        if (!AllIdWithoutDuplication.contains(cover_idref)) {
            // cover_idref无效
            log.append({ "invalid cover id", cover_idref });
        }
    }
    // 检查spine的idref
    foreach(SpineEntry se, p.m_spine) {
        QString idref = se.m_idref;
        if (!AllIdWithoutDuplication.contains(idref)) {
            // cover_idref无效
            log.append({ "invalid idref", idref });
        }
    }

    if (log.isEmpty()) {
        DisplayNoProblemsMessage();
    }
    else {
        p.m_manifest = new_manifest;
        QString new_opf = p.convert_to_xml();
        DisplayCorrectOPFResults(log);
        //opf->p.parse(new_opf);
        opf->SetText(new_opf);
        if (RefreshBookBrowser)
            m_BookBrowser->Refresh();
    }

    show();
    raise();
}
//-------------------------------------------------------------------------------------------------------------------------


//------------------------------------------------------modified: correctOPF---------------------------------------------------------
void ValidationResultsView::DisplayCorrectOPFResults(QList<pair<QString, QString>> log)
{

    m_ResultTable->setRowCount(log.count());
    m_ResultTable->setColumnCount(3);
    m_ResultTable->setHorizontalHeaderLabels(QStringList() << "Type" << "Content" << "Operation");

    QTableWidgetItem* item;
    for (int i = 0; i < log.count(); i++) {
        QString type = log[i].first,
            cont = log[i].second;

        item = new QTableWidgetItem(type.toUpper());
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_ResultTable->setItem(i, 0, item);

        item = new QTableWidgetItem();
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_ResultTable->setItem(i, 1, item);

        item = new QTableWidgetItem();
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_ResultTable->setItem(i, 2, item);

        QBrush row_brush;
        QTextCodec* codec = QTextCodec::codecForName("UTF-8"); // turn the UTF-8 chars to unicode codec.
        if (type == "non unique id") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("非唯一ID"));
            m_ResultTable->item(i, 1)->setText("ID: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。")).arg(cont));
        }
        else if (type == "unnecessary id") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("多余ID"));
            m_ResultTable->item(i, 1)->setText("ID: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 在manifest项发现多余ID【%1】，已进行删除对应项的处理。")).arg(cont));
        }
        else if (type == "invalid href") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("无效Href"));
            m_ResultTable->item(i, 1)->setText("HREF: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 在manifest项发现无效href【%1】，已进行删除对应项的处理。")).arg(cont));
        }
        else if (type == "invalid cover id") {
            row_brush = Utility::ValidationResultBrush(Utility::WARNING_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("无效ID引用"));
            m_ResultTable->item(i, 1)->setText("ID: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。")).arg(cont));
        }
        else if (type == "invalid idref") {
            row_brush = Utility::ValidationResultBrush(Utility::WARNING_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("无效ID引用"));
            m_ResultTable->item(i, 1)->setText("ID: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。")).arg(cont));
        }
        else if (type == "xmlns missing") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns 缺失"));
            m_ResultTable->item(i, 1)->setText("NODE: package");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 找不到在package节点的xmlns属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。")));
        }
        else if (type == "xmlns:dc missing") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns:dc 缺失"));
            m_ResultTable->item(i, 1)->setText("NODE: metadata");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 不到在metadata节点的xmlns:dc属性，,建议以\"http://purl.org/dc/elements/1.1/\"值补上该属性。")));
        }
        else if (type == "xmlns:opf missing") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns:opf 缺失"));
            m_ResultTable->item(i, 1)->setText("NODE: metadata");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 找不到在metadata节点的xmlns:opf属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。")));
        }
        else if (type == "xmlns error") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns 错误"));
            m_ResultTable->item(i, 1)->setText("NODE: package");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 不规范的xmlns属性值【%1】,建议改为\"http://www.idpf.org/2007/opf\"")).arg(cont));
        }
        else if (type == "xmlns:dc error") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns:dc 错误"));
            m_ResultTable->item(i, 1)->setText("NODE: metadata");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 不规范的xmlns:dc属性值【%1】，建议改为\"http://purl.org/dc/elements/1.1/\"")).arg(cont));
        }
        else if (type == "xmlns:opf error") {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("xmlns:opf 错误"));
            m_ResultTable->item(i, 1)->setText("NODE: metadata");
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 不规范的xmlns:opf属性值【%1】，建议改为\"http://www.idpf.org/2007/opf\"")).arg(cont));
        }
        else if (type == "new item") {
            row_brush = Utility::ValidationResultBrush(Utility::WARNING_BRUSH);
            m_ResultTable->item(i, 0)->setText(codec->toUnicode("发现文件"));
            m_ResultTable->item(i, 1)->setText("PATH: " + cont);
            m_ResultTable->item(i, 2)->setText(QString(codec->toUnicode(" 发现文件【%1】未登记到Manifest，已自动登记。")).arg(cont));
        }

        for (int j = 0; j < 3; j++) {
            item = m_ResultTable->item(i, j);
            SetItemPalette(item, row_brush);
            m_ResultTable->resizeColumnToContents(j);
        }

    }
}
//-----------------------------------------------------------------------------------------------------------------------------------
