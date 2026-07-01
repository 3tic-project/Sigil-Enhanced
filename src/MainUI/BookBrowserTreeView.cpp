
#include <QRect>
#include <QPainter>
#include <QModelIndex>
#include <QMimeData>
#include <QFileInfo>
#include <QApplication>
#include <QDrag>
#include <QItemSelectionModel>
#include <QStringList>

#include "BookBrowserTreeView.h"
#include "Misc/ResourceInsertion.h"

QStringList IMPORT_SUFFIX = { "xhtml","html","htm","txt" };

//------------------- modified: BookBrowserTreeView -----------------------

BookBrowserTreeView::BookBrowserTreeView(QWidget* parent)
	:
	QTreeView(parent),
	dropIndicatorEnabled(false),
	dragStartPosition(QPoint()),
	dragStartIndex(QModelIndex())
{
}


BookBrowserTreeView::~BookBrowserTreeView()
{
}


void BookBrowserTreeView::drawOtherDropIndicator(QPoint& pos)
{
	QModelIndex mindex = indexAt(pos);
	QRect rect = visualRect(mindex);
	int itemVCenter = rect.center().y();
	int vpos = pos.y();

	QPoint pt1, pt2;
	if (vpos <= itemVCenter) {
		pt1 = rect.topLeft();
		pt2 = rect.topRight();
	}
	else {
		pt1 = rect.bottomLeft();
		pt2 = rect.bottomRight();
	}
	dropIndicatorLine = { pt1,pt2 };
	viewport()->update();
}


void BookBrowserTreeView::paintEvent(QPaintEvent* e)
{
	QTreeView::paintEvent(e);
	if (dropIndicatorEnabled) {
		QPainter painter = QPainter(viewport());
		QPoint startPt = dropIndicatorLine.startPoint,
			endPt = dropIndicatorLine.endPoint;
		painter.drawLine(startPt, endPt);
	}
}

void BookBrowserTreeView::mousePressEvent(QMouseEvent* e)
{
	QTreeView::mousePressEvent(e);

	if (e->button() == Qt::LeftButton) {
		QModelIndex index = indexAt(e->position().toPoint());
		if (index.isValid() && !index.data(Qt::UserRole + 1).toString().isEmpty()) {
			dragStartPosition = e->position().toPoint();
			dragStartIndex = index;
		} else {
			dragStartIndex = QModelIndex();
		}
	}
}

void BookBrowserTreeView::mouseMoveEvent(QMouseEvent* e)
{
	if ((e->buttons() & Qt::LeftButton) &&
		dragStartIndex.isValid() &&
		(e->position().toPoint() - dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
		startDrag(Qt::CopyAction | Qt::MoveAction);
		dragStartIndex = QModelIndex();
		e->accept();
		return;
	}

	QTreeView::mouseMoveEvent(e);
}

void BookBrowserTreeView::startDrag(Qt::DropActions supportedActions)
{
	QModelIndexList indexes;
	if (selectionModel()) {
		indexes = selectionModel()->selectedRows(0);
	}
	if (indexes.isEmpty()) {
		indexes = selectedIndexes();
	}

	QMimeData* mime_data = model() ? model()->mimeData(indexes) : nullptr;
	if (!mime_data) {
		mime_data = new QMimeData;
	}

	QStringList identifiers;
	foreach(QModelIndex index, indexes) {
		if (!index.isValid()) {
			continue;
		}
		const QString identifier = index.data(Qt::UserRole + 1).toString();
		if (!identifier.isEmpty()) {
			identifiers << identifier;
		}
	}
	if (!identifiers.isEmpty()) {
		mime_data->setData(ResourceInsertion::BOOK_BROWSER_RESOURCE_MIME, identifiers.join("\n").toUtf8());
	}

	QDrag* drag = new QDrag(this);
	drag->setMimeData(mime_data);
	drag->exec(supportedActions | Qt::CopyAction, Qt::MoveAction);
}


void BookBrowserTreeView::dragEnterEvent(QDragEnterEvent* e)
{
	if (e->mimeData()->hasUrls()) {
		QList<QUrl> urls = e->mimeData()->urls();
		bool no_directory = true;
		foreach(QUrl url, urls) {
			QString filepath = url.toLocalFile();
			if (QFileInfo(filepath).isDir()) {
				no_directory = false;
				break;
			}
		}
		if (no_directory) {
			e->accept();
		}
		else {
			e->ignore();
		}
	}
	else {
		QTreeView::dragEnterEvent(e);
	}
}


void BookBrowserTreeView::dragMoveEvent(QDragMoveEvent* e)
{
	QList<QUrl>urls = e->mimeData()->urls();
	if (urls.size() == 1) {
		QString url = urls.at(0).toLocalFile();
		QString ext = QFileInfo(url).suffix().toLower();
		if (!IMPORT_SUFFIX.contains(ext))
			return QTreeView::dragMoveEvent(e);
		QPoint pos = e->position().toPoint();
		QModelIndex mindex = indexAt(pos);
		if (mindex.parent().data(0) != "Text") {
			if (dropIndicatorEnabled == true) {
				dropIndicatorEnabled = false;
				viewport()->update();
			}
			return QTreeView::dragMoveEvent(e);
		}
		else {
			dropIndicatorEnabled = true;
			drawOtherDropIndicator(pos);
		}
	}
	QTreeView::dragMoveEvent(e);
}


void BookBrowserTreeView::dragLeaveEvent(QDragLeaveEvent* e)
{
	dropIndicatorEnabled = false;
	QTreeView::dragLeaveEvent(e);
}


void BookBrowserTreeView::dropEvent(QDropEvent* e)
{
	QList<QUrl> urls = e->mimeData()->urls();
	QStringList filepaths;
	bool requestEmitted = false;
	foreach(QUrl url, urls) {
		filepaths << url.toLocalFile();
	}
	if (dropIndicatorEnabled && filepaths.size() == 1) {
		QString url = filepaths[0];
		if (IMPORT_SUFFIX.contains(QFileInfo(url).suffix().toLower())) {
			if (QFileInfo(url).suffix().toLower() == "txt") {
				emit insertTXTRequest(url, e->position().toPoint());
				requestEmitted = true;
			}
			else {
				emit insertHtmlRequest(url,e->position().toPoint());
				requestEmitted = true;
			}
		}
	}

	dropIndicatorEnabled = false;
	if (!requestEmitted)
		emit addFilesRequest(filepaths);

	QTreeView::dropEvent(e);
}
