
#include <QRect>
#include <QPainter>
#include <QModelIndex>
#include <QMimeData>
#include <QApplication>
#include <QDrag>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QStringList>

#include "BookBrowserTreeView.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "Misc/ImagePreviewService.h"
#include "Misc/ResourceInsertion.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"

QStringList IMPORT_SUFFIX = { "xhtml","html","htm","txt" };
static const int IMAGE_PREVIEW_DELAY_MS = 150;

//------------------- modified: BookBrowserTreeView -----------------------

BookBrowserTreeView::BookBrowserTreeView(QWidget* parent)
	:
	QTreeView(parent),
	dropIndicatorEnabled(false),
	dragStartPosition(QPoint()),
	dragStartIndex(QModelIndex()),
	imagePreviewIndex(QModelIndex()),
	imagePreviewPopup(new QLabel(nullptr, Qt::ToolTip)),
	imagePreviewTimer(new QTimer(this)),
	imagePreviewService(new ImagePreviewService(this)),
	imagePreviewRequestId(0)
{
	setMouseTracking(true);
	viewport()->setMouseTracking(true);
	imagePreviewPopup->setAttribute(Qt::WA_ShowWithoutActivating);
	imagePreviewPopup->setAlignment(Qt::AlignCenter);
	imagePreviewPopup->setStyleSheet("QLabel { background: palette(base); border: 1px solid palette(mid); padding: 4px; }");
	imagePreviewPopup->hide();
	imagePreviewTimer->setSingleShot(true);
	connect(imagePreviewTimer, &QTimer::timeout, this, [this]() { showImagePreview(); });
	connect(imagePreviewService, &ImagePreviewService::previewReady,
	        this, &BookBrowserTreeView::imagePreviewReady);
}


BookBrowserTreeView::~BookBrowserTreeView()
{
	delete imagePreviewPopup;
}

Resource* BookBrowserTreeView::resourceForIndex(const QModelIndex& index) const
{
	if (!index.isValid()) {
		return nullptr;
	}

	const QString identifier = index.data(Qt::UserRole + 1).toString();
	if (identifier.isEmpty()) {
		return nullptr;
	}

	MainWindow* mainwin = qobject_cast<MainWindow*>(Utility::GetMainWindow());
	if (!mainwin || mainwin->GetCurrentBook().isNull()) {
		return nullptr;
	}

	return mainwin->GetCurrentBook()->GetFolderKeeper()->GetResourceByIdentifier(identifier);
}

static QString formatPreviewFileSize(qint64 bytes)
{
	if (bytes < 1024) {
		return QString("%1 B").arg(bytes);
	}

	double size = bytes / 1024.0;
	QString unit = "KB";
	if (size >= 1024.0) {
		size = size / 1024.0;
		unit = "MB";
	}
	if (size >= 1024.0) {
		size = size / 1024.0;
		unit = "GB";
	}

	return QString("%1 %2").arg(QString::number(size, 'f', size < 10.0 ? 1 : 0), unit);
}

static QString formatPreviewInfo(const QSize& pixel_size, qint64 file_size)
{
	QString dimensions = pixel_size.isEmpty() ?
	                     QString("Unknown px") :
	                     QString("%1 x %2 px").arg(pixel_size.width()).arg(pixel_size.height());
	return QString("%1 | %2").arg(dimensions, formatPreviewFileSize(file_size));
}

static QPixmap imagePreviewWithInfoBar(const ImagePreviewData& preview,
                                       const QFont& font,
                                       const QPalette& palette)
{
	const QString info = formatPreviewInfo(preview.pixelSize, preview.fileSize);
	const QFontMetrics fm(font);
	const int horizontal_padding = 12;
	const int info_height = fm.height() + 10;
	const int width = qMax(preview.image.width(), fm.horizontalAdvance(info) + horizontal_padding * 2);
	const int height = preview.image.height() + info_height;

	QPixmap pixmap(width, height);
	pixmap.fill(palette.base().color());

	QPainter painter(&pixmap);
	const int image_x = (width - preview.image.width()) / 2;
	painter.drawImage(image_x, 0, preview.image);

	const QRect info_rect(0, preview.image.height(), width, info_height);
	painter.fillRect(info_rect, palette.window().color());
	painter.setPen(palette.mid().color());
	painter.drawLine(info_rect.topLeft(), info_rect.topRight());
	painter.setFont(font);
	painter.setPen(palette.text().color());
	painter.drawText(info_rect.adjusted(horizontal_padding, 0, -horizontal_padding, 0),
	                 Qt::AlignCenter,
	                 info);
	return pixmap;
}

void BookBrowserTreeView::scheduleImagePreview(const QModelIndex& index)
{
	Resource* resource = resourceForIndex(index);
	if (!resource ||
		(resource->Type() != Resource::ImageResourceType &&
		 resource->Type() != Resource::SVGResourceType)) {
		hideImagePreview();
		return;
	}

	if (imagePreviewIndex == index &&
	    (imagePreviewPopup->isVisible() || imagePreviewTimer->isActive() ||
	     imagePreviewRequestId != 0)) {
		return;
	}

	imagePreviewService->cancelPending();
	imagePreviewRequestId = 0;
	imagePreviewPath.clear();
	imagePreviewIndex = index;
	imagePreviewPopup->hide();
	imagePreviewTimer->start(IMAGE_PREVIEW_DELAY_MS);
}

void BookBrowserTreeView::showImagePreview()
{
	Resource* resource = resourceForIndex(imagePreviewIndex);
	if (!resource ||
		(resource->Type() != Resource::ImageResourceType &&
		 resource->Type() != Resource::SVGResourceType)) {
		hideImagePreview();
		return;
	}

	imagePreviewPath = resource->GetFullPath();
	if (imagePreviewPath.isEmpty()) {
		hideImagePreview();
		return;
	}
	const ImagePreviewService::Format format =
		resource->Type() == Resource::SVGResourceType ?
		ImagePreviewService::Format::Svg : ImagePreviewService::Format::Bitmap;
	imagePreviewRequestId = imagePreviewService->request(imagePreviewPath, format);
}

void BookBrowserTreeView::imagePreviewReady(quint64 requestId,
	                                        const ImagePreviewData& preview)
{
	if (requestId != imagePreviewRequestId || !imagePreviewIndex.isValid()) {
		return;
	}
	Resource* resource = resourceForIndex(imagePreviewIndex);
	if (!resource || resource->GetFullPath() != imagePreviewPath || preview.image.isNull()) {
		hideImagePreview();
		return;
	}

	QPixmap pixmap = imagePreviewWithInfoBar(preview,
	                                         imagePreviewPopup->font(),
	                                         imagePreviewPopup->palette());
	imagePreviewPopup->setPixmap(pixmap);
	imagePreviewPopup->adjustSize();

	QRect item_rect = visualRect(imagePreviewIndex);
	QPoint pos = viewport()->mapToGlobal(item_rect.topRight() + QPoint(12, 0));
	imagePreviewPopup->move(pos);
	imagePreviewPopup->show();
	imagePreviewRequestId = 0;
}

void BookBrowserTreeView::hideImagePreview()
{
	imagePreviewTimer->stop();
	imagePreviewService->cancelPending();
	imagePreviewRequestId = 0;
	imagePreviewPath.clear();
	imagePreviewIndex = QPersistentModelIndex();
	if (imagePreviewPopup) {
		imagePreviewPopup->hide();
	}
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
	hideImagePreview();
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
		hideImagePreview();
		startDrag(Qt::CopyAction | Qt::MoveAction);
		dragStartIndex = QModelIndex();
		e->accept();
		return;
	}

	QTreeView::mouseMoveEvent(e);

	if (e->buttons() == Qt::NoButton) {
		scheduleImagePreview(indexAt(e->position().toPoint()));
	}
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
	hideImagePreview();
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
	hideImagePreview();
	QTreeView::dragLeaveEvent(e);
}


void BookBrowserTreeView::dropEvent(QDropEvent* e)
{
	hideImagePreview();
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

void BookBrowserTreeView::leaveEvent(QEvent* e)
{
	hideImagePreview();
	QTreeView::leaveEvent(e);
}

void BookBrowserTreeView::scrollContentsBy(int dx, int dy)
{
	hideImagePreview();
	QTreeView::scrollContentsBy(dx, dy);
}
