
#include <QRect>
#include <QPainter>
#include <QModelIndex>
#include <QMimeData>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QPixmap>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QStringList>

#include "BookBrowserTreeView.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "Misc/ImagePreviewPolicy.h"
#include "Misc/ImagePreviewService.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"

QStringList IMPORT_SUFFIX = { "xhtml","html","htm","txt" };

//------------------- modified: BookBrowserTreeView -----------------------

BookBrowserTreeView::BookBrowserTreeView(QWidget* parent)
	:
	QTreeView(parent),
	dropIndicatorEnabled(false),
	imagePreviewIndex(QModelIndex()),
	imagePreviewPopup(new QLabel(nullptr, Qt::ToolTip)),
	imagePreviewTimer(new QTimer(this)),
	imagePreviewService(new ImagePreviewService(this)),
	imagePreviewDelayMs(ImagePreviewPolicy::hoverDelayMs(
		style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, this),
#if defined(Q_OS_MAC) && defined(Q_PROCESSOR_ARM_64)
		true
#else
		false
#endif
	)),
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
	refreshImagePreviewSettings();
}


BookBrowserTreeView::~BookBrowserTreeView()
{
	delete imagePreviewPopup;
}

void BookBrowserTreeView::refreshImagePreviewSettings()
{
	SettingsStore settings;
	hideImagePreview();
	imagePreviewService->setMaximumPreviewSide(settings.bookBrowserImagePreviewSize());
}

void BookBrowserTreeView::resetImagePreviewState()
{
	hideImagePreview();
	imagePreviewService->reset();
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

// Keep the hover preview fully visible on the current screen.
// Prefer the right of the tree item; flip to the left (or clamp) near edges.
static QPoint imagePreviewPopupPosition(const QRect& anchor_global,
                                        const QSize& popup_size)
{
	constexpr int kGap = 12;
	constexpr int kMargin = 8;

	QScreen* screen = QGuiApplication::screenAt(anchor_global.center());
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	if (!screen) {
		return QPoint(anchor_global.right() + kGap, anchor_global.top());
	}

	const QRect available = screen->availableGeometry().adjusted(kMargin, kMargin, -kMargin, -kMargin);
	// Use exclusive right/bottom so popup size comparisons match window geometry.
	const int available_right = available.x() + available.width();
	const int available_bottom = available.y() + available.height();
	const int max_x = available.x() + qMax(0, available.width() - popup_size.width());
	const int max_y = available.y() + qMax(0, available.height() - popup_size.height());

	// Prefer to the right of the item; flip left when that would clip.
	int x = anchor_global.right() + kGap;
	if (x + popup_size.width() > available_right) {
		const int left_x = anchor_global.left() - kGap - popup_size.width();
		x = (left_x >= available.x()) ? left_x : max_x;
	}
	x = qBound(available.x(), x, max_x);

	// Prefer top-aligned with the item; clamp vertically into the screen.
	int y = anchor_global.top();
	if (y + popup_size.height() > available_bottom) {
		y = max_y;
	}
	y = qBound(available.y(), y, max_y);

	return QPoint(x, y);
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

	if (imagePreviewIndex == index) {
		if (imagePreviewPopup->isVisible() || imagePreviewRequestId != 0 ||
		    imagePreviewTimer->isActive()) {
			// Dwell is measured per item; small pointer movements within the
			// same row must not postpone the preview indefinitely.
			return;
		}
	}

	imagePreviewService->cancelPending();
	imagePreviewRequestId = 0;
	imagePreviewPath.clear();
	imagePreviewIndex = index;
	imagePreviewPopup->hide();
	imagePreviewPopup->clear();
	imagePreviewTimer->start(imagePreviewDelayMs);
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

	const QRect item_rect = visualRect(imagePreviewIndex);
	const QRect anchor_global(viewport()->mapToGlobal(item_rect.topLeft()), item_rect.size());
	const QPoint pos = imagePreviewPopupPosition(anchor_global, imagePreviewPopup->size());
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
		imagePreviewPopup->clear();
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
}

void BookBrowserTreeView::mouseMoveEvent(QMouseEvent* e)
{
	QTreeView::mouseMoveEvent(e);

	if (e->buttons() == Qt::NoButton) {
		scheduleImagePreview(indexAt(e->position().toPoint()));
	}
}


static bool isExternalFileDrop(const QMimeData *mime_data)
{
	return mime_data && mime_data->hasUrls();
}

static bool allDroppedUrlsAreLocalFiles(const QList<QUrl> &urls)
{
	if (urls.isEmpty()) {
		return false;
	}

	foreach(const QUrl &url, urls) {
		if (!url.isLocalFile()) {
			return false;
		}
		const QFileInfo info(url.toLocalFile());
		if (url.toLocalFile().isEmpty() || !info.isFile() || info.isDir()) {
			return false;
		}
	}
	return true;
}

static bool allDroppedUrlsAreEpub(const QList<QUrl> &urls)
{
	if (urls.isEmpty()) {
		return false;
	}

	foreach(const QUrl &url, urls) {
		if (QFileInfo(url.toLocalFile()).suffix().compare(QLatin1String("epub"), Qt::CaseInsensitive) != 0) {
			return false;
		}
	}
	return true;
}


void BookBrowserTreeView::dragEnterEvent(QDragEnterEvent* e)
{
	hideImagePreview();
	if (isExternalFileDrop(e->mimeData())) {
		const QList<QUrl> urls = e->mimeData()->urls();
		// Let MainWindow handle all-EPUB drops (add vs open-in-new-window).
		if (allDroppedUrlsAreLocalFiles(urls) && !allDroppedUrlsAreEpub(urls)) {
			e->acceptProposedAction();
			return;
		}
		e->ignore();
		return;
	}

	QTreeView::dragEnterEvent(e);
}


void BookBrowserTreeView::dragMoveEvent(QDragMoveEvent* e)
{
	if (isExternalFileDrop(e->mimeData())) {
		const QList<QUrl> urls = e->mimeData()->urls();
		if (!allDroppedUrlsAreLocalFiles(urls) || allDroppedUrlsAreEpub(urls)) {
			dropIndicatorEnabled = false;
			e->ignore();
			return;
		}

		bool show_insert_indicator = false;
		if (urls.size() == 1) {
			const QString ext = QFileInfo(urls.at(0).toLocalFile()).suffix().toLower();
			const QPoint pos = e->position().toPoint();
			const QModelIndex mindex = indexAt(pos);
			if (IMPORT_SUFFIX.contains(ext) && mindex.parent().data(0) == "Text") {
				show_insert_indicator = true;
				QPoint indicator_pos = pos;
				drawOtherDropIndicator(indicator_pos);
			}
		}
		if (!show_insert_indicator && dropIndicatorEnabled) {
			viewport()->update();
		}
		dropIndicatorEnabled = show_insert_indicator;

		// Do not ask OPFModel to accept URL mime. InternalMove only
		// understands XHTML reading-order payloads.
		e->acceptProposedAction();
		return;
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
	if (isExternalFileDrop(e->mimeData())) {
		const QList<QUrl> urls = e->mimeData()->urls();
		QStringList filepaths;
		foreach(const QUrl &url, urls) {
			filepaths << url.toLocalFile();
		}

		bool requestEmitted = false;
		if (dropIndicatorEnabled && filepaths.size() == 1) {
			QString url = filepaths[0];
			if (IMPORT_SUFFIX.contains(QFileInfo(url).suffix().toLower())) {
				if (QFileInfo(url).suffix().toLower() == "txt") {
					emit insertTXTRequest(url, e->position().toPoint());
					requestEmitted = true;
				} else {
					emit insertHtmlRequest(url, e->position().toPoint());
					requestEmitted = true;
				}
			}
		}

		dropIndicatorEnabled = false;
		viewport()->update();
		if (!requestEmitted && !filepaths.isEmpty()) {
			emit addFilesRequest(filepaths);
		}
		e->acceptProposedAction();
		return;
	}

	dropIndicatorEnabled = false;
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
