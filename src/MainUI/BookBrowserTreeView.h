#pragma once
#ifndef BOOKBROWSERTREEVIEW_H
#define BOOKBROWSERTREEVIEW_H

#include <QPoint>
#include <QTreeView>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPaintEvent>

class QLabel;
class QTimer;
class Resource;

class BookBrowserTreeView : public QTreeView
{
	Q_OBJECT

signals:
	void addFilesRequest(QStringList& filepaths);
	void insertHtmlRequest(QString& filepath,const QPoint& event_pos);
	void insertTXTRequest(QString& filepath, const QPoint& event_pos);

public:
	BookBrowserTreeView(QWidget* parent = nullptr);
	~BookBrowserTreeView();

protected:
	void mousePressEvent(QMouseEvent* event);
	void mouseMoveEvent(QMouseEvent* event);
	void dragEnterEvent(QDragEnterEvent* event);
	void dragMoveEvent(QDragMoveEvent* event);
	void dragLeaveEvent(QDragLeaveEvent* event);
	void dropEvent(QDropEvent* event);
	void paintEvent(QPaintEvent* event);
	void leaveEvent(QEvent* event);
	void scrollContentsBy(int dx, int dy);
	void startDrag(Qt::DropActions supportedActions);

private:
	struct Line {
		QPoint startPoint;
		QPoint endPoint;
	};
	bool dropIndicatorEnabled;
	Line dropIndicatorLine;
	QPoint dragStartPosition;
	QModelIndex dragStartIndex;
	QModelIndex imagePreviewIndex;
	QLabel* imagePreviewPopup;
	QTimer* imagePreviewTimer;

	void drawOtherDropIndicator(QPoint& eventPos);
	void scheduleImagePreview(const QModelIndex& index);
	void showImagePreview();
	void hideImagePreview();
	Resource* resourceForIndex(const QModelIndex& index) const;
};

#endif // !BOOKBROWSERTREEVIEW_H
