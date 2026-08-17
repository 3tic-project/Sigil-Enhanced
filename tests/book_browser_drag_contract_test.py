#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


repo = Path(sys.argv[1]).resolve()
tree_view = (repo / "src/MainUI/BookBrowserTreeView.cpp").read_text(encoding="utf-8")
book_browser = (repo / "src/MainUI/BookBrowser.cpp").read_text(encoding="utf-8")
model_header = (repo / "src/MainUI/OPFModel.h").read_text(encoding="utf-8")
model = (repo / "src/MainUI/OPFModel.cpp").read_text(encoding="utf-8")

# QAbstractItemView owns the InternalMove lifecycle. Its startDrag removes the
# source rows after QStandardItemModel has inserted their copies at the target.
# A local QDrag implementation caused duplicate resource entries by skipping
# that final source-row removal.
require(
    "void BookBrowserTreeView::startDrag" not in tree_view
    and "new QDrag" not in tree_view,
    "Book Browser must retain Qt's source-row cleanup for InternalMove",
)

# Add the editor-insertion payload at the model layer so Qt's normal drag
# implementation carries both its internal row data and stable resource IDs.
require(
    "QMimeData *mimeData(const QModelIndexList &indexes) const override;"
    in model_header,
    "OPFModel must own Book Browser MIME construction",
)
require(
    "QStandardItemModel::mimeData(indexes)" in model
    and "ResourceInsertion::BOOK_BROWSER_RESOURCE_MIME" in model,
    "custom resource IDs must augment, not replace, standard model MIME data",
)
require(
    "return Qt::CopyAction | Qt::MoveAction;" in model,
    "resource drags must support editor copy and internal XHTML move",
)
require(
    "m_TreeView->setDefaultDropAction(Qt::MoveAction);" in book_browser,
    "Book Browser must default internal reordering to MoveAction",
)
require(
    "ResourceInsertion::CanInsertResource(resource, ResourceInsertion::Context::HTML)"
    in model
    and "ResourceInsertion::CanInsertResource(resource, ResourceInsertion::Context::CSS)"
    in model,
    "resources supported by editor insertion must remain draggable",
)

# The model must never accept CopyAction internally (which recreates the
# reported duplicate) or allow non-XHTML resources into the Text folder.
require(
    "action != Qt::MoveAction" in model
    and "itemFromIndex(parent) != m_TextFolderItem" in model,
    "internal drops must be move-only and target the Text folder",
)
require(
    "resource->Type() != Resource::HTMLResourceType" in model,
    "only XHTML resources may be reordered in the Text folder",
)
require(
    "External file URLs are imported by BookBrowserTreeView" in model,
    "OPFModel must reject Finder/Explorer URL drops so they do not create fake rows",
)

require(
    "SIGNAL(addFilesRequest(QStringList&))" in book_browser
    and "SLOT(AddFiles(QStringList&))" in book_browser,
    "external file drops must call BookBrowser::AddFiles",
)
require(
    "SIGNAL(insertHtmlRequest(QString&, const QPoint&))" in book_browser
    and "SLOT(InsertHtmlToTextGroup(QString&, const QPoint&))" in book_browser,
    "positioned XHTML drops must call InsertHtmlToTextGroup",
)
require(
    "SIGNAL(insertTXTRequest(QString&, const QPoint&))" in book_browser
    and "SLOT(InsertTxtToTextGroup(QString&, const QPoint&))" in book_browser,
    "positioned TXT drops must call InsertTxtToTextGroup",
)
require(
    "m_TreeView->setAcceptDrops(true);" in book_browser,
    "Book Browser must accept external drops after InternalMove is configured",
)

require(
    "allDroppedUrlsAreEpub" in tree_view
    and "acceptProposedAction()" in tree_view,
    "external file drags must be accepted by the view, not the OPF model",
)
require(
    "Do not ask OPFModel to accept URL mime" in tree_view,
    "URL dragMove must not fall through to InternalMove canDropMimeData",
)
