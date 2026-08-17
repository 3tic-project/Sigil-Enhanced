/************************************************************************
**
**  Copyright (C) 2019 Kevin B. Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2020 Doug Massay
**  Copyright (C) 2012 John Schember <john@nachtimwald.com>
**  Copyright (C) 2012 Dave Heiland
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

#pragma once
#ifndef TABBAR_H
#define TABBAR_H

#include <QtCore/QByteArray>
#include <QtCore/QPoint>
#include <QtWidgets/QTabBar>

class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QWidget;

class TabBar : public QTabBar
{
    Q_OBJECT

public:
    static const char EditorTabMimeType[];

    TabBar(QWidget *parent = 0);
    void SetMoveLastTabAllowed(bool allowed);

    static QByteArray EncodeTab(QWidget *tab);
    static QWidget *DecodeTab(const QMimeData *mime);

signals:
    void TabBarClicked();
    void TabBarDoubleClicked();
    void CloseOtherTabsRequest(int tab_index);
    void MoveToOtherGroupRequest(int tab_index);
    void TabDropRequest(QWidget *tab, int insert_index);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dropEvent(QDropEvent *event);

private slots:
    void EmitCloseOtherTabs();
    void EmitMoveToOtherGroup();

private:
    void ShowContextMenu(QMouseEvent *event, int tab_index);
    void StartTabDrag(int index);
    int InsertIndexAt(const QPoint &pos) const;

    int m_TabIndex;
    int m_PressIndex;
    QPoint m_PressPos;
    bool m_MoveLastTabAllowed;
};

#endif // TABBAR_H
