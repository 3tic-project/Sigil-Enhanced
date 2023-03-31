/************************************************************************
**
**  Copyright (C) 2015-2021 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include "EmbedPython/EmbeddedPython.h"

#include <QtCore/QFileInfo>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTableWidget>
#include <QRegularExpression>
#include <QVariant>
#include <QFileDialog>
#include <qtextcodec.h>  // modified: correctOPF

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/ValidationResultsView.h"
#include "Misc/Utility.h"
#include "sigil_exception.h"

#include "Parsers/OPFParser.h" // modified: correctOPF
#include "MainUI/BookBrowser.h" // modified: correctOPF
#include "Misc/HTMLEncodingResolver.h" // modified: correctOPF

#if(0)
static const QBrush INFO_BRUSH    = QBrush(QColor(224, 255, 255));
static const QBrush WARNING_BRUSH = QBrush(QColor(255, 255, 230));
static const QBrush ERROR_BRUSH   = QBrush(QColor(255, 230, 230));
#endif

const QString ValidationResultsView::SEP = QString(QChar(31));

static const QString SETTINGS_GROUP = "validation_results";


ValidationResultsView::ValidationResultsView(QWidget *parent)
    :
    QDockWidget(tr("Validation Results"), parent),
    m_ResultTable(new QTableWidget(this)),
    m_NoProblems(false),
    m_ContextMenu(new QMenu(this))
{
    setWidget(m_ResultTable);
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    SetUpTable();
    m_ResultTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ExportAll = new QAction(tr("Export All") + "...", this);
    ReadSettings();
    connect(m_ResultTable, SIGNAL(itemDoubleClicked(QTableWidgetItem *)),
            this, SLOT(ResultDoubleClicked(QTableWidgetItem *)));
    connect(m_ResultTable, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(OpenContextMenu(const QPoint &)));
    connect(m_ExportAll,   SIGNAL(triggered()), this, SLOT(ExportAll()));
}

void ValidationResultsView::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    m_LastFolderOpen = settings.value("last_folder_open").toString();
    settings.endGroup();
}

void ValidationResultsView::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("last_folder_open", m_LastFolderOpen);
    settings.endGroup();
}


void ValidationResultsView::OpenContextMenu(const QPoint &point)
{
    m_ContextMenu->addAction(m_ExportAll);
    m_ContextMenu->exec(m_ResultTable->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
        m_ExportAll->setEnabled(true);
    }
}


void ValidationResultsView::ExportAll()
{
    if (m_NoProblems || m_ResultTable->rowCount() == 0) return;

    // Get the filename to use
    QMap<QString,QString> file_filters;
    file_filters[ "csv" ] = tr("CSV files (*.csv)");
    file_filters[ "txt" ] = tr("Text files (*.txt)");
    QStringList filters = file_filters.values();
    QString filter_string = "";
    foreach(QString filter, filters) {
        filter_string += filter + ";;";
    }
    QString default_filter = file_filters.value("csv");

    QFileDialog::Options options = QFileDialog::Options();
#ifdef Q_OS_MAC
    options = options | QFileDialog::DontUseNativeDialog;
#endif

    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Export Validation Results"),
                                                    m_LastFolderOpen,
                                                    filter_string,
                                                    &default_filter,
                                                    options);
    if (filename.isEmpty()) return;

    QString ext = QFileInfo(filename).suffix().toLower();
    QChar sep = QChar(',');
    if (ext == "txt") sep = QChar(9);

    QStringList res;
    for (int i = 0; i < m_ResultTable->rowCount(); i++) {
        QStringList data;
        QTableWidgetItem *path_item = m_ResultTable->item(i, 0);
        data << path_item->data(Qt::UserRole+1).toString();
        data << m_ResultTable->item(i,1)->text();
        data << m_ResultTable->item(i,2)->text();
        data << m_ResultTable->item(i,3)->text();
        if (sep == ',') {
            res << Utility::createCSVLine(data);
        } else {
            res << data.join(sep);
        }
    }
    QString text = res.join('\n');
    QString message;
    try {
        Utility::WriteUnicodeTextFile(text, filename);
        m_LastFolderOpen = QFileInfo(filename).absolutePath();
        WriteSettings();
    } catch (CannotOpenFile& e) {
        message = QString(e.what());
        Utility::DisplayStdWarningDialog(tr("Export of Validation Results failed: "), message);
    }
}


void ValidationResultsView::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    raise();
}


QStringList ValidationResultsView::ValidateFile(QString &apath)
{
    int rv = 0;
    QString error_traceback;
    QStringList results;

    QList<QVariant> args;
    args.append(QVariant(apath));

    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("sanitycheck"),
                                         QString("perform_sanity_check"),
                                         args,
                                         &rv,
                                         error_traceback);    
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in sanitycheck perform_sanity_check: ") + QString::number(rv), 
                                         error_traceback);
        // an error happened - make no changes
        return results;
    }
    return res.toStringList();
}


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
        log.append({ "xmlns missing", "package" });
    }
    else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        log.append({ "xmlns error", p.m_package.m_atts["xmlns"] });
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        log.append({ "xmlns:dc missing", "metadata" });
    }
    else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        log.append({ "xmlns:dc error", p.m_metans.m_atts["xmlns:dc"] });
    }
    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        log.append({ "xmlns:opf missing", "metadata" });
    }
    else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        log.append({ "xmlns:dc error", p.m_metans.m_atts["xmlns:opf"] });
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
        QTextCodec* codec = QTextCodec::codecForName("GBK"); // turn the GBK chars to unicode codec.
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


void ValidationResultsView::ValidateCurrentBook()
{
    ClearResults();
    QList<ValidationResult> results;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_Book->SaveAllResourcesToDisk();

    QList<Resource *> resources = m_Book->GetFolderKeeper()->GetResourceList();
    foreach (Resource * resource, resources) {
        if (resource->Type() == Resource::HTMLResourceType) {
            QString apath = resource->GetFullPath();
            QString bookpath = resource->GetRelativePath();
            QStringList reslst = ValidateFile(apath);
            if (!reslst.isEmpty()) {
                foreach (QString res, reslst) {
                    QStringList details = res.split(SEP);
                    ValidationResult::ResType vtype;
                    QString etype = details[0];
                    if (etype == "info") {
                        vtype = ValidationResult::ResType_Info;
                    } else if (etype == "warning") {
                        vtype = ValidationResult::ResType_Warn;
                    } else if (etype == "error") {
                        vtype = ValidationResult::ResType_Error;
                    } else {
                        continue;
                    }
                    QString filename = details[1];
                    int lineno = details[2].toInt();
                    int charoffset = details[3].toInt();
                    QString msg = details[4];
                    results.append(ValidationResult(vtype,bookpath,lineno,charoffset,msg));
                }
            }
        }
    }
    QApplication::restoreOverrideCursor();
    DisplayResults(results);
    show();
    raise();
}


void ValidationResultsView::LoadResults(const QList<ValidationResult> &results)
{
    ClearResults();
    DisplayResults(results);
    show();
    raise();
}


void ValidationResultsView::ClearResults()
{
    m_ResultTable->clearContents();
    m_ResultTable->setRowCount(0);
}


void ValidationResultsView::SetBook(QSharedPointer<Book> book)
{
    m_Book = book;
    ClearResults();
}

//------------------------modified: correctOPF-------------------------
void ValidationResultsView::SetBookBrowser(BookBrowser* bookbrowser)
{
    m_BookBrowser = bookbrowser;
    ClearResults();
}
//---------------------------------------------------------------------

void ValidationResultsView::ResultDoubleClicked(QTableWidgetItem *item)
{
    Q_ASSERT(item);
    int row = item->row();
    QTableWidgetItem *path_item = m_ResultTable->item(row, 0);

    if (!path_item) {
        return;
    }

    QString shortname = path_item->text();
    QString bookpath = path_item->data(Qt::UserRole+1).toString();
    QTableWidgetItem *line_item = m_ResultTable->item(row, 1);
    QTableWidgetItem *offset_item = m_ResultTable->item(row, 2);

    if (!line_item || !offset_item) {
        return;
    }


    int line = line_item->text() != "N/A" ? line_item->text().toInt(): -1;
    int charoffset = offset_item->text() != "N/A" ? offset_item->text().toInt(): -1;

    try {
        Resource *resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(bookpath);
        // if character offset info exists, use it in preference to just the line number
        if (charoffset != -1) {
            emit OpenResourceRequest(resource, line, charoffset, QString());
        } else {
            emit OpenResourceRequest(resource, line, -1, QString());
        }
    } catch (ResourceDoesNotExist&) {
        return;
    }
}


void ValidationResultsView::SetUpTable()
{
    m_ResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ResultTable->setTabKeyNavigation(false);
    m_ResultTable->setDropIndicatorShown(false);
    m_ResultTable->horizontalHeader()->setStretchLastSection(true);
    m_ResultTable->verticalHeader()->setVisible(false);
}


void ValidationResultsView::DisplayResults(const QList<ValidationResult> &results)
{
    m_ResultTable->clear();
    m_NoProblems = false;

    if (results.empty()) {
        m_NoProblems = true;
        DisplayNoProblemsMessage();
        return;
    }

    ConfigureTableForResults();

    Q_FOREACH(ValidationResult result, results) {
        int rownum = m_ResultTable->rowCount();
        QTableWidgetItem *item = NULL;

        QBrush row_brush = Utility::ValidationResultBrush(Utility::INFO_BRUSH);
        if (result.Type() == ValidationResult::ResType_Warn) {
            row_brush = Utility::ValidationResultBrush(Utility::WARNING_BRUSH);
        } else if (result.Type() == ValidationResult::ResType_Error) {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
        }

        m_ResultTable->insertRow(rownum);
 
        QString path;
        QString bookpath = result.BookPath();
        try {
            Resource * resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(bookpath);
            path = resource->ShortPathName();
        } catch (ResourceDoesNotExist&) {
            if (bookpath.isEmpty()) {
                path = "***Invalid Book Path Provided ***";
            } else {
                path = bookpath;
            }
        }

        item = new QTableWidgetItem(RemoveEpubPathPrefix(path));
        item->setData(Qt::UserRole+1, bookpath);
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 0, item);

        item = result.LineNumber() > 0 ? new QTableWidgetItem(QString::number(result.LineNumber())) : new QTableWidgetItem("N/A");
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 1, item);

        item = result.CharOffset() >= 0 ? new QTableWidgetItem(QString::number(result.CharOffset())) : new QTableWidgetItem("N/A");
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 2, item);

        item = new QTableWidgetItem(result.Message());
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 3, item);
    }

    // Make Line and Offset columns as small as possible
    // Ditto for Filename
    m_ResultTable->resizeColumnToContents(0);
    m_ResultTable->resizeColumnToContents(1);
    m_ResultTable->resizeColumnToContents(2);
    //m_ResultTable->resizeColumnsToContents();
}

int ValidationResultsView::ResultCount()
{
    if (m_NoProblems) return 0;
    return m_ResultTable->rowCount();
}

void ValidationResultsView::DisplayNoProblemsMessage()
{
    m_ResultTable->setRowCount(1);
    m_ResultTable->setColumnCount(1);
    m_ResultTable->setHorizontalHeaderLabels(
        QStringList() << tr("Message"));
    QTableWidgetItem *item = new QTableWidgetItem(tr("No problems found!"));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    QFont font = item->font();
    font.setPointSize(16);
    item->setFont(font);
    m_ResultTable->setItem(0, 0, item);
    m_ResultTable->resizeRowToContents(0);
}


void ValidationResultsView::ConfigureTableForResults()
{
    m_ResultTable->setRowCount(0);
    m_ResultTable->setColumnCount(4);
    m_ResultTable->setHorizontalHeaderLabels(
    QStringList() << tr("File") << tr("Line") << tr("Offset") << tr("Message"));
    m_ResultTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ResultTable->setSortingEnabled(true);
    m_ResultTable->horizontalHeader()->setSortIndicatorShown(true);

}


QString ValidationResultsView::RemoveEpubPathPrefix(const QString &path)
{
    return QString(path).remove(QRegularExpression("^[\\w-]+\\.epub/?"));
}

void ValidationResultsView::SetItemPalette(QTableWidgetItem * item, QBrush &row_brush)
{
    if (Utility::IsDarkMode()) {
        item->setForeground(row_brush);
    } else {
        item->setBackground(row_brush);
    }   
}

