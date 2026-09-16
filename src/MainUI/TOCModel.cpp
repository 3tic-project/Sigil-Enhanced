/************************************************************************
**
**  Copyright (C) 2016-2024 Kevin B. Hendricks, Stratford, Ontario, Canada
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

#include <QtCore/QtCore>
#include <QtCore/QThread>
#include <QtConcurrent/QtConcurrent>
#include <QtWidgets/QApplication>
#include <QXmlStreamReader>

#include "MainUI/TOCModel.h"
#include "Misc/Utility.h"
#include "ResourceObjects/NCXResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NavProcessor.h"
#include "BookManipulation/CleanSource.h"

TOCModel::TOCModel(QObject *parent)
    :
    QStandardItemModel(parent),
    m_Book(NULL),
    m_RefreshInProgress(false),
    m_TocRootWatcher(new QFutureWatcher<TOCModel::TOCEntry>(this))
{
    connect(m_TocRootWatcher, SIGNAL(finished()), this, SLOT(RefreshEnd()));
}


TOCModel::~TOCModel()
{
    // A background parse captures this model. Keep its book and parser state
    // alive until that parse has finished, even when the dock is closing.
    m_TocRootWatcher->waitForFinished();
}

void TOCModel::SetBook(QSharedPointer<Book> book, bool refresh)
{
    {
        // We need to make sure we don't step on the toes of GetNCXText
        QMutexLocker book_lock(&m_UsingBookMutex);
        m_Book = book;
        m_EpubVersion = m_Book->GetConstOPF()->GetEpubVersion();
    }
    if (refresh) Refresh();
}


QString TOCModel::GetBookPathForIndex(const QModelIndex &index)
{
    QStandardItem *item = itemFromIndex(index);

    if (!item) {
        return QString();
    }

    return item->data().toString();
}


// Should only ever be called from the main thread!
// This is because access to m_RefreshInProgress is not guarded.
// We *could* guard it, but there's no need to call this func
// from several threads so we just disallow it with the assert.
void TOCModel::Refresh()
{
    Q_ASSERT(QThread::currentThread() == QApplication::instance()->thread());

    if (m_RefreshInProgress) {
        m_RefreshPending = true;
        return;
    }

    m_RefreshInProgress = true;
    m_TocRootWatcher->setFuture(QtConcurrent::run(&TOCModel::GetRootTOCEntry, this));
}


void TOCModel::RefreshEnd()
{
    m_RefreshInProgress = false;
    if (m_RefreshPending) {
        m_RefreshPending = false;
        Refresh();
        return; // Do not display results from the superseded book/nav state.
    }
    BuildModel(m_TocRootWatcher->result());
    emit RefreshDone();
}


TOCModel::TOCEntry TOCModel::GetRootTOCEntry()
{
    QMutexLocker book_lock(&m_UsingBookMutex);
    if (m_EpubVersion.startsWith('3') && m_Book->GetConstOPF()->GetNavResource()) {
        NavProcessor navproc(m_Book->GetConstOPF()->GetNavResource());
        return navproc.GetRootTOCEntry();
    }
    return ParseNCX(GetNCXText());
}


QString TOCModel::GetNCXText()
{
    // Called only by GetRootTOCEntry while it holds m_UsingBookMutex.
    NCXResource *ncx = m_Book->GetNCX();
    if (!ncx) return QString();
    QReadLocker locker(&(ncx->GetLock()));
    return ncx->GetText(); // The viewing model must not repair or reserialize NCX.
}


TOCModel::TOCEntry TOCModel::ParseNCX(const QString &ncx_source)
{
    QXmlStreamReader ncx(ncx_source);
    bool in_navmap = false;
    TOCModel::TOCEntry root;
    root.is_root = true;

    while (!ncx.atEnd()) {
        ncx.readNext();

        if (ncx.isStartElement()) {
            if (!in_navmap) {
                if (ncx.name().compare(QLatin1String("navMap")) == 0) {
                    in_navmap = true;
                }

                continue;
            }

            if (ncx.name().compare(QLatin1String("navPoint")) == 0) {
                root.children.append(ParseNavPoint(ncx));
            }
        } else if (ncx.isEndElement() &&
                   ncx.name().compare(QLatin1String("navMap")) == 0) {
            break;
        }
    }

    if (ncx.hasError()) {
        TOCModel::TOCEntry empty;
        empty.is_root = true;
        return empty;
    }

    return root;
}


TOCModel::TOCEntry TOCModel::ParseNavPoint(QXmlStreamReader &ncx)
{
    TOCModel::TOCEntry current;

    while (!ncx.atEnd()) {
        ncx.readNext();

        if (ncx.isStartElement()) {
            if (ncx.name().compare(QLatin1String("text")) == 0) {
                // Consume this element only, including adjacent CDATA/entity
                // tokens. Waiting for a Characters token can steal a later
                // label after <text/> or loop forever on truncated input.
                current.text = ncx.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            } else if (ncx.name().compare(QLatin1String("content")) == 0) {
                QString href = ncx.attributes().value("", "src").toString();
                current.target = ConvertHREFToBookPath(href);
            } else if (ncx.name().compare(QLatin1String("navPoint")) == 0) {
                current.children.append(ParseNavPoint(ncx));
            }
        } else if (ncx.isEndElement() &&
                   ncx.name().compare(QLatin1String("navPoint")) == 0) {
            break;
        }
    }

    return current;
}


void TOCModel::BuildModel(const TOCModel::TOCEntry &root_entry)
{
    clear();
    foreach(const TOCModel::TOCEntry & child_entry, root_entry.children) {
        AddEntryToParentItem(child_entry, invisibleRootItem());
    }
}


void TOCModel::AddEntryToParentItem(const TOCEntry &entry, QStandardItem *parent)
{
    Q_ASSERT(parent);
    QStandardItem *item = new QStandardItem(entry.text);
    item->setData(entry.target);
    item->setToolTip(entry.target);
    item->setEditable(false);
    item->setDragEnabled(false);
    item->setDropEnabled(false);
    parent->appendRow(item);
    foreach(const TOCModel::TOCEntry & child_entry, entry.children) {
        AddEntryToParentItem(child_entry, item);
    }
}


// This routine is designed to work on url encoded hrefs
// and return things url encoded
QString TOCModel::ConvertHREFToBookPath(const QString &ahref)
{
    QString bookpath;
    if (ahref.indexOf(":") != -1) return ahref;
    // split off any fragment
    NCXResource* ncxres = m_Book->GetNCX();
    QStringList pieces = ahref.split('#', Qt::KeepEmptyParts);
    QString basepath = pieces.at(0);
    QString fragment = "";
    if (pieces.size() > 1) fragment = pieces.at(1);
    // handle special cases first                                                                                   
    if (basepath == "./" || basepath.isEmpty()) {
        // this link ends in the ncx itself                                                                         
        bookpath = Utility::URLEncodePath(ncxres->GetRelativePath());
        if (!fragment.isEmpty()) bookpath = bookpath + "#" + fragment;
        return bookpath;
    }
    bookpath = Utility::buildBookPath(Utility::URLDecodePath(basepath), ncxres->GetFolder());
    bookpath = Utility::URLEncodePath(bookpath);
    if (!fragment.isEmpty()) bookpath = bookpath + "#" + fragment;
    return bookpath;
}
