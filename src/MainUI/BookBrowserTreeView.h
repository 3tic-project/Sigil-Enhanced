#pragma once
#ifndef BOOKBROWSERTREEVIEW_H
#define BOOKBROWSERTREEVIEW_H

#include <QPoint>
#include <QTreeView>
#include <QDragMoveEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QPaintEvent>

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
	void dragEnterEvent(QDragEnterEvent* event);
	void dragMoveEvent(QDragMoveEvent* event);
	void dragLeaveEvent(QDragLeaveEvent* event);
	void dropEvent(QDropEvent* event);
	void paintEvent(QPaintEvent* event);

private:
	struct Line {
		QPoint startPoint;
		QPoint endPoint;
	};
	bool dropIndicatorEnabled;
	Line dropIndicatorLine;

	void drawOtherDropIndicator(QPoint& eventPos);
};

#endif // !BOOKBROWSERTREEVIEW_H