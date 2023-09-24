#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QSignalMapper>
#include <QMimeData>
#include <QDragEnterEvent>

#include "MainUI/BookBrowser.h"
#include "MainUI/MainWindow.h"
#include "sigil_constants.h"
#include "Misc/Utility.h"
#include "BookManipulation/FolderKeeper.h"
#include "sigil_exception.h"
#include "Importers/ImportHTML.h"
#include "Importers/ImportTXT.h" // modified: BookBrowserTreeView
//------------------------ modified: AddFiles ------------------------------
QStringList BookBrowser::AddFiles(QStringList &filepaths)
{
    QStringList added_book_paths;
    bool replacements_made = false;
    QString filter_string = "";

    if (filepaths.isEmpty()) {
        return added_book_paths;
    }

    QStringList invalid_filenames;
    HTMLResource* current_html_resource = qobject_cast<HTMLResource*>(GetCurrentResource());
    Resource* open_resource = NULL;

    int progress_value = 0;
    int file_count = filepaths.count();

    QProgressDialog progress(QObject::tr("Adding Existing Files.."), 0, 0, file_count, Utility::GetMainWindow());
    if (file_count > 1) {
        progress.setMinimumDuration(PROGRESS_BAR_MINIMUM_DURATION);
        progress.setValue(progress_value);
        // since not modal force it to be shown and move it to the top
        progress.show();
        progress.raise();
        progress.activateWindow();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    bool yes_to_all = false;
    bool no_to_all = false;
    QList<Resource*> resToBeAdded;
    FolderKeeper* folderkeeper = m_Book->GetFolderKeeper();
    foreach(QString filepath, filepaths) {
        if (file_count > 1) {
            // Set progress value and ensure dialog has time to display when doing extensive updates
            // Set ahead of actual add since it can abort in several places
            progress.setValue(progress_value++);
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        QString filename = QFileInfo(filepath).fileName();
        bool CoverImageSemanticsSet = false;
        // try to see if an existing file has this filename and allow overwriting
        QString existing_book_path = folderkeeper->GetBookPathByPathEnd(filename);

        bool needUpdatingOPF = true;
        if (!existing_book_path.isEmpty()) {
            // If this is an image prompt to replace it.
            if (IMAGE_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()) ||
                SVG_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()) ||
                VIDEO_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()) ||
                AUDIO_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()) ||
                FONT_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower())) {
                bool do_replacement = false;
                if (yes_to_all) do_replacement = true;
                if (no_to_all) do_replacement = false;
                if (!yes_to_all && !no_to_all) {
                    QMessageBox::StandardButton button_pressed;
                    button_pressed = QMessageBox::warning(this, tr("Sigil"),
                        tr("The multimedia file \"%1\" already exists in the book.\n\nOK to replace?").arg(filename),
                        QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll);

                    if (button_pressed == QMessageBox::YesToAll) {
                        yes_to_all = true;
                        do_replacement = true;
                    }
                    if (button_pressed == QMessageBox::NoToAll) {
                        no_to_all = true;
                        do_replacement = false;
                    }
                    if (button_pressed == QMessageBox::Yes) do_replacement = true;
                    if (button_pressed == QMessageBox::No) do_replacement = false;
                }

                if (!do_replacement) continue;

                try {
                    Resource* old_resource = folderkeeper->GetResourceByBookPath(existing_book_path);
                    ImageResource* image_resource = qobject_cast<ImageResource*>(old_resource);
                    if (image_resource) {
                        CoverImageSemanticsSet = m_Book->GetOPF()->IsCoverImage(image_resource);
                    }
                    folderkeeper->RemoveWithoutUpdatingOPF(old_resource);
                    replacements_made = true;
                    needUpdatingOPF = false;
                }
                catch (ResourceDoesNotExist&) {
                    Utility::DisplayStdErrorDialog(tr("Unable to delete or replace file \"%1\".").arg(filename)
                    );
                    continue;
                }
            }
            else {
                QMessageBox::warning(this, tr("Sigil"), tr("Unable to load \"%1\"\n\nA file with this name already exists in the book.").arg(filename));
                continue;
            }
        }

        if (QFileInfo(filepath).fileName() == "page-map.xml") {
            Resource* res = folderkeeper->AddContentFileToFolder(filepath, true, QString("application/oebps-page-map+xml"));
            added_book_paths << res->GetRelativePath();
        }
        else if (TEXT_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower())) {
            ImportHTML html_import(filepath);
            XhtmlDoc::WellFormedError error = html_import.CheckValidToLoad();

            if (error.line != -1) {
                invalid_filenames << QString("%1 (line %2: %3)").arg(QDir::toNativeSeparators(filepath)).arg(error.line).arg(error.message);
                continue;
            }

            html_import.SetBook(m_Book, true);
            // Since we set the Book manually,
            // this call merely mutates our Book.
            bool extract_metadata = false;
            html_import.setDoNotUpdateOPF(true);
            html_import.GetBook(extract_metadata);
            QStringList importedbookpaths = html_import.GetAddedBookPaths();
            //DBG qDebug() << "In BookBrowser Add Existing adding bookpaths: " << importedbookpaths;
            Resource* added_resource = folderkeeper->GetResourceByBookPath(importedbookpaths.at(0));
            if (needUpdatingOPF) resToBeAdded << added_resource;
            HTMLResource* added_html_resource = qobject_cast<HTMLResource*>(added_resource);
            added_book_paths.append(importedbookpaths);
            if (current_html_resource && added_html_resource) {
                m_Book->MoveResourceAfter(added_html_resource, current_html_resource);
                current_html_resource = added_html_resource;

                // Only open HTML files as they are likely to be edited whereas other items
                // are likely to be inserted into or linked to the current file.
                // Only open the first file in any added group.
                if (!open_resource) {
                    open_resource = added_resource;
                }
            }
        }
        else {
            Resource* resource = folderkeeper->AddContentFileToFolder(filepath,false);
            if (needUpdatingOPF) resToBeAdded << resource;
            added_book_paths << resource->GetRelativePath();
            // if replacing a cover image, set the cover image semantics
                ImageResource* new_image_resource = qobject_cast<ImageResource*>(resource);
            if (CoverImageSemanticsSet) {
                if (new_image_resource) {
                    m_Book->GetOPF()->SetResourceAsCoverImage(new_image_resource);
                }
            }
            // TODO: adding a CSS file should add the referenced fonts too
            if (resource->Type() == Resource::CSSResourceType) {
                CSSResource* css_resource = qobject_cast<CSSResource*> (resource);
                css_resource->InitialLoad();
            }
        }

    }
    folderkeeper->BulkAddResourcesToOPF(resToBeAdded);
    // turn off the QProgress Dialog by setting it as reaching its target
    progress.setValue(file_count);

    if (!invalid_filenames.isEmpty()) {
        QMessageBox::warning(this, tr("Sigil"),
            tr("The following file(s) were not loaded due to invalid content or not well formed XML:\n\n%1")
            .arg(invalid_filenames.join("\n")));
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    // we still need to set modified and refresh if image file replacements have been done
    if (!added_book_paths.isEmpty() || replacements_made) {
        emit ResourcesAdded();
        if (open_resource) {
            emit ResourceActivated(open_resource);
        }
        emit BookContentModified();
        Refresh();
        emit ShowStatusMessageRequest(tr("File(s) added or replaced."));
    }
    QApplication::restoreOverrideCursor();

    m_LastFolderOpen = QFileInfo(filepaths.first()).absolutePath();

    return added_book_paths;
}
//--------------------------------------------------------------------------


//--------------------- modified: AddImages ----------------------
QStringList BookBrowser::AddImagesFromFilePaths(QStringList& filepaths) {
    QStringList bookpaths;
    QList<Resource*> resToBeAdded;
    FolderKeeper* folderkeeper = m_Book->GetFolderKeeper();
    foreach(QString filepath, filepaths) {
        if (!IMAGE_EXTENSIONS.contains(QFileInfo(filepath).suffix().toLower()))
            continue;
        QString filename = QFileInfo(filepath).fileName();
        Resource* resource = folderkeeper->AddContentFileToFolder(filepath,false);
        resToBeAdded << resource;
        bookpaths << resource->GetRelativePath();
    }
    if (!resToBeAdded.isEmpty()) {
        folderkeeper->BulkAddResourcesToOPF(resToBeAdded);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!bookpaths.isEmpty()) {
        emit ResourcesAdded();
        emit BookContentModified();
        Refresh();
    }
    QApplication::restoreOverrideCursor();
    return bookpaths;
}

QString BookBrowser::AddImageFromClipboard(const QByteArray& data, QString defaultFilename) {
    QString bookpath;
    FolderKeeper* folderkeeper = m_Book->GetFolderKeeper();

    if (!IMAGE_EXTENSIONS.contains(QFileInfo(defaultFilename).suffix().toLower()))
        return "";
    Resource* resource = folderkeeper->AddContentFromQByteArray(data,defaultFilename);
    bookpath = resource->GetRelativePath();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!bookpath.isEmpty()) {
        emit ResourcesAdded();
        emit BookContentModified();
        Refresh();
    }
    QApplication::restoreOverrideCursor();
    return bookpath;
}
//-----------------------------------------------------------------------------

//------------------------ modified: insertFileToEditor -------------------------
void BookBrowser::insertFileToEditor() {
    emit InsertFileRequest();
}
//-------------------------------------------------------------------------------

//--------------------------------------------- modified: BookBrowserTreeView -----------------------------------------
void BookBrowser::InsertTxtToTextGroup(QString& filepath,const QPoint& pos)
{
    QModelIndex mindex = m_TreeView->indexAt(pos);
    if (mindex.parent().data(0) != "Text")
        return;
    if (QFileInfo(filepath).suffix().toLower() != "txt")
        return;

    FolderKeeper* folderkeeper = m_Book->GetFolderKeeper();

    QList<HTMLResource*> SpineOrderResources;
    foreach(Resource * res, m_Book->GetOPF()->GetSpineOrderResources(m_Book->GetAllResources())) {
        SpineOrderResources << qobject_cast<HTMLResource*>(res);
    }

    ImportTXT txt_import(filepath);
    txt_import.SetBook(m_Book);
    txt_import.GetBook(false);
    
    QString importedbookpath = txt_import.GetAddedBookPath();
    Resource* added_resource = folderkeeper->GetResourceByBookPath(importedbookpath);
    HTMLResource* added_html_resource = qobject_cast<HTMLResource*>(added_resource);
    InsertHTMLResource(added_html_resource, pos,SpineOrderResources);
}


void BookBrowser::InsertHtmlToTextGroup(QString& filepath,const QPoint& pos)
{
    QModelIndex mindex = m_TreeView->indexAt(pos);
    if (mindex.parent().data(0) != "Text")
        return;
    if (!QStringList({ "xhtml","html","htm","txt" }).contains(QFileInfo(filepath).suffix().toLower()))
        return;

    QString filename = QFileInfo(filepath).fileName();
    FolderKeeper* folderkeeper = m_Book->GetFolderKeeper();
    QString existing_book_path = folderkeeper->GetBookPathByPathEnd(filename);

    if (!existing_book_path.isEmpty()) {
        QMessageBox::warning(this, tr("Sigil"), tr("Unable to add \"%1\"\nA file with this name already exists in the book.").arg(filename));
        return;
    }

    ImportHTML html_import(filepath);
    XhtmlDoc::WellFormedError error = html_import.CheckValidToLoad();

    if (error.line != -1) {
        QString invalid_filename = QString("%1 (line %2: %3)").arg(QDir::toNativeSeparators(filepath)).arg(error.line).arg(error.message);
        QMessageBox::warning(this, tr("Sigil"), invalid_filename);
        return;
    }

    QList<HTMLResource*> SpineOrderResources;
    foreach(Resource * res, m_Book->GetOPF()->GetSpineOrderResources(m_Book->GetAllResources())) {
        SpineOrderResources << qobject_cast<HTMLResource*>(res);
    }

    html_import.SetBook(m_Book, true);
    html_import.GetBook(false);
    QStringList importedbookpaths = html_import.GetAddedBookPaths();
    Resource* added_resource = folderkeeper->GetResourceByBookPath(importedbookpaths.at(0));
    HTMLResource* added_html_resource = qobject_cast<HTMLResource*>(added_resource);
    InsertHTMLResource(added_html_resource, pos, SpineOrderResources);
}


void BookBrowser::InsertHTMLResource(HTMLResource* res,const QPoint& pos, QList<HTMLResource*>& spine)
{
    QModelIndex mindex = m_TreeView->indexAt(pos);


    QRect rect = m_TreeView->visualRect(mindex);
    QAbstractItemModel* model = m_TreeView->model();
    if (pos.y() <= rect.center().y()) { // Above of Item
        spine.insert(mindex.row(), res);
    }
    else { // Below of Item
        spine.insert(mindex.row() + 1, res);
    }

    m_Book->GetOPF()->UpdateSpineOrder(spine);

    emit ResourcesAdded();
    emit BookContentModified();
    Refresh();
    emit ResourceActivated(res);
    emit ShowStatusMessageRequest(tr("File(s) added or replaced."));

    m_LastFolderOpen = res->GetFullFolderPath();
}
//---------------------------------------------------------------------------------------------------------------------