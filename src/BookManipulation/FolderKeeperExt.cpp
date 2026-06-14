#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QThread>
#include <QTime>
#include <QApplication>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QIcon>
#include <QFileIconProvider>
#include <QDebug>

#include "BookManipulation/FolderKeeper.h"
#include "sigil_constants.h"
#include "sigil_exception.h"
#include "ResourceObjects/AudioResource.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/VideoResource.h"
#include "Misc/Utility.h"
#include "Misc/OpenExternally.h"
#include "Misc/SettingsStore.h"
#include "Misc/MediaTypes.h"

//------------------------------- modified: BulkResourceRenamed ------------------------------------
void FolderKeeper::BulkResourceRenamed(const QList<Resource*>resources, const QList<QString>old_full_paths)
{
    for (int i = 0; i < resources.size(); i++) {
        Resource* resource = resources.at(i);
        QString old_full_path = old_full_paths.at(i);
        QString book_path = old_full_path.right(old_full_path.length() - m_FullPathToMainFolder.length() - 1);
        Resource* res = m_Path2Resource[book_path];
        m_Path2Resource.remove(book_path);
        m_Path2Resource[resource->GetRelativePath()] = res;
    }
    m_OPF->BulkResourceRenamed(resources, old_full_paths);
    updateShortPathNames();
}
//--------------------------------------------------------------------------------------------------

//--------------------- modified: AddContentFromQByteArray ------------------------
// This routine should never process the opf or the ncx as they
// are special cased elsewhere in FolderKeeper
Resource* FolderKeeper::AddContentFromQByteArray(const QByteArray& data,
                                                    QString filename,
                                                    bool update_opf,
                                                    const QString& mimetype,
                                                    const QString& folderpath)
{
    QString bookpath;
    // initialize base file information
    QString norm_filename = filename;
    QFileInfo fi(filename);

    // check if mediatype is recognized
    QString mt = mimetype;
    if (!mt.isEmpty() && (MediaTypes::instance()->GetGroupFromMediaType(mt, "") == "")) {
        qDebug() << "Warning: unrecognized mediatype in OPF: " << mimetype;
        mt = "";
    }

    // try using the extension to determine the mediatype
    if (mt.isEmpty()) {
        QString extension = fi.suffix().toLower();
        mt = MediaTypes::instance()->GetMediaTypeFromExtension(extension, mimetype);
    }

    QString group = DetermineFileGroup(filename, mt);
    QString resdesc = MediaTypes::instance()->GetResourceDescFromMediaType(mt, "Resource");

    QDir folder(m_FullPathToMainFolder);

    Resource* resource = NULL;
    QString new_file_path;

    // lock for GetUniqueFilenameVersion() until the
    // resource with that file name has been created
    // and added to the list of all resources so it
    // will return that this filename is now taken
    {

        QMutexLocker locker(&m_AccessMutex);

        if (bookpath.isEmpty()) {
            // Use either the provided folder path or the default folder to store the file

            // Rename files that start with a '.'
            // These merely introduce needless difficulties
            if (filename.left(1) == ".") {
                norm_filename = filename.right(filename.size() - 1);
            }
            filename = GetUniqueFilenameVersion(norm_filename);
            QString folder_to_use = folderpath;
            if (folder_to_use == "\\") folder_to_use = GetDefaultFolderForGroup(group);
            if (!folder_to_use.isEmpty()) {
                folder.mkpath(folder_to_use);
                new_file_path = m_FullPathToMainFolder + "/" + folder_to_use + "/" + filename;
            }
            else {
                new_file_path = m_FullPathToMainFolder + "/" + filename;
            }
        }

        if (resdesc == "MiscTextResource") {
            resource = new MiscTextResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "AudioResource") {
            resource = new AudioResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "VideoResource") {
            resource = new VideoResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "ImageResource") {
            resource = new ImageResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "SVGResource") {
            resource = new SVGResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "FontResource") {
            resource = new FontResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "HTMLResource") {
            resource = new HTMLResource(m_FullPathToMainFolder, new_file_path, this);
        }
        else if (resdesc == "CSSResource") {
            resource = new CSSResource(m_FullPathToMainFolder, new_file_path);
        }
        else if (resdesc == "XMLResource") {
            resource = new XMLResource(m_FullPathToMainFolder, new_file_path);
        }
        else {
            // Fallback mechanism - follow previous setting of new_file_path
            // But make it a generic Resource
            resource = new Resource(m_FullPathToMainFolder, new_file_path);
        }

        m_Resources[resource->GetIdentifier()] = resource;

        // Note:  m_FullPathToMainFolder **never** ends with a "/"
        QString book_path = bookpath;
        if (book_path.isEmpty()) {
            book_path = new_file_path.right(new_file_path.length() - m_FullPathToMainFolder.length() - 1);
        }
        m_Path2Resource[book_path] = resource;
        resource->SetEpubVersion(m_OPF->GetEpubVersion());
        resource->SetMediaType(mt);
        resource->SetShortPathName(filename);
        // cache file icons by media type
        if (!m_FileIconCache.contains(mt)) {
            m_FileIconCache[mt] = QFileIconProvider().icon(fi);
        }
    }

    // skip copy if unpacking zip already put it in the right place
    if (!new_file_path.isEmpty()) {
        QFile file(new_file_path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
        }
        file.close();
        QFile::setPermissions(new_file_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ReadUser | QFileDevice::WriteUser |
            QFileDevice::ReadOther);
    }


    if (QThread::currentThread() != QApplication::instance()->thread()) {
        resource->moveToThread(QApplication::instance()->thread());
    }

    connect(resource, SIGNAL(Deleted(const Resource*)),
        this, SLOT(RemoveResource(const Resource*)), Qt::DirectConnection);
    connect(resource, SIGNAL(Renamed(const Resource*, QString)),
        this, SLOT(ResourceRenamed(const Resource*, QString)), Qt::DirectConnection);
    connect(resource, SIGNAL(Moved(const Resource*, QString)),
        this, SLOT(ResourceMoved(const Resource*, QString)), Qt::DirectConnection);

    if (update_opf) {
        emit ResourceAdded(resource);
    }

    return resource;
}
//-----------------------------------------------------------------------------------------
