/************************************************************************
**
**  Copyright (C) 2015-2024 Kevin B. Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2020      Doug Massay
**  Copyright (C) 2012      John Schember <john@nachtimwald.com>
**  Copyright (C) 2012      Dave Heiland
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

#include <QtCore/QDataStream>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QDrag>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QMouseEvent>
#include <QAction>
#include <QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTabWidget>
#include <QMimeData>
#include <QPointer>

#include "Misc/Utility.h"
#include "Tabs/TabBar.h"

const char TabBar::EditorTabMimeType[] = "application/x-sigil-editortab";

TabBar::TabBar(QWidget *parent)
    : QTabBar(parent),
      m_TabIndex(-1),
      m_PressIndex(-1),
      m_PressPos(),
      m_MoveLastTabAllowed(true)
{
    setAcceptDrops(true);
#if defined(Q_OS_MAC)
    // Qt MacOSX missing tab close icon - https://bugreports.qt.io/browse/QTBUG-61092
    // and prevent the silly show only when cursor is near it that came after
    // having a gui control that only appears if cursor is near is sheer stupidity
    const QString FORCE_TAB_CLOSE_BUTTON = 
        "QTabBar::close-button { "
            "background-image: url(:/dark/closedock-macstyle.svg);"
        "}";
    setStyleSheet(FORCE_TAB_CLOSE_BUTTON);
#endif
    setFocusPolicy(Qt::StrongFocus);
}

void TabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit TabBarDoubleClicked();
}


void TabBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        const int tab_index = tabAt(event->pos());
        if (tab_index >= 0) {
            emit tabCloseRequested(tab_index);
        }
        event->accept();
        return;
    } else if (event->button() == Qt::RightButton) {
        int tabCount = count();

        if (tabCount < 1) {
            return;
        }

        for (int i = 0; i < tabCount; i++) {
            if (tabRect(i).contains(event->pos())) {
                m_TabIndex = i;
                ShowContextMenu(event, i);
                break;
            }
        }
    } else if (event->button() == Qt::LeftButton) {
        m_PressIndex = tabAt(event->pos());
        m_PressPos = event->pos();
        emit TabBarClicked();
    }

    QTabBar::mousePressEvent(event);
}

void TabBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || m_PressIndex < 0) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    if ((event->pos() - m_PressPos).manhattanLength() < QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    StartTabDrag(m_PressIndex);
    m_PressIndex = -1;
}

void TabBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasFormat(QLatin1String(EditorTabMimeType))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TabBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData() && event->mimeData()->hasFormat(QLatin1String(EditorTabMimeType))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void TabBar::dropEvent(QDropEvent *event)
{
    QWidget *tab = DecodeTab(event->mimeData());
    if (!tab) {
        event->ignore();
        return;
    }
    emit TabDropRequest(tab, InsertIndexAt(event->position().toPoint()));
    event->acceptProposedAction();
}

QByteArray TabBar::EncodeTab(QWidget *tab)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << reinterpret_cast<quintptr>(tab);
    return data;
}

QWidget *TabBar::DecodeTab(const QMimeData *mime)
{
    if (!mime || !mime->hasFormat(QLatin1String(EditorTabMimeType))) {
        return 0;
    }
    QByteArray data = mime->data(QLatin1String(EditorTabMimeType));
    QDataStream stream(&data, QIODevice::ReadOnly);
    quintptr pointer = 0;
    stream >> pointer;
    return reinterpret_cast<QWidget *>(pointer);
}

void TabBar::StartTabDrag(int index)
{
    QTabWidget *tabs = qobject_cast<QTabWidget *>(parentWidget());
    if (!tabs || index < 0 || index >= tabs->count()) {
        return;
    }
    QWidget *tab = tabs->widget(index);
    if (!tab) {
        return;
    }

    QMimeData *mime = new QMimeData;
    mime->setData(QLatin1String(EditorTabMimeType), EncodeTab(tab));
    mime->setText(tabs->tabText(index));

    QDrag *drag = new QDrag(this);
    drag->setMimeData(mime);
    const QRect rect = tabRect(index);
    if (rect.isValid()) {
        drag->setPixmap(grab(rect));
        drag->setHotSpot(QPoint(rect.width() / 2, rect.height() / 2));
    }
    drag->exec(Qt::MoveAction);
}

int TabBar::InsertIndexAt(const QPoint &pos) const
{
    const int hovered = tabAt(pos);
    if (hovered < 0) {
        return count();
    }
    const QRect rect = tabRect(hovered);
    if (pos.x() > rect.center().x()) {
        return hovered + 1;
    }
    return hovered;
}

void TabBar::ShowContextMenu(QMouseEvent *event, int tab_index)
{
    QPointer<QMenu> menu = new QMenu();
    QAction *moveAction = new QAction(tr("Move Editor to Other Group"), menu);
    moveAction->setEnabled(count() > 1 || m_MoveLastTabAllowed);
    menu->addAction(moveAction);
    connect(moveAction, SIGNAL(triggered()), this, SLOT(EmitMoveToOtherGroup()));
    QAction *closeOtherTabsAction = new QAction(tr("Close Other Tabs"), menu);
    menu->addAction(closeOtherTabsAction);
    connect(closeOtherTabsAction, SIGNAL(triggered()), this, SLOT(EmitCloseOtherTabs()));
    QPoint p;
    p = mapToGlobal(event->pos());
#ifdef Q_OS_WIN32
    // Relocate the context menu slightly down and right to prevent "automatic" action 
    // highlight on Windows, which then closes all other tabs when the mouse is released.
    p.setX(p.x() + 2);
    p.setY(p.y() + 4);
#endif
    menu->exec(p);
    if (!menu.isNull()) {
        delete menu.data();
    }
}

void TabBar::EmitCloseOtherTabs()
{
    emit CloseOtherTabsRequest(m_TabIndex);
}

void TabBar::SetMoveLastTabAllowed(bool allowed)
{
    m_MoveLastTabAllowed = allowed;
}

void TabBar::EmitMoveToOtherGroup()
{
    emit MoveToOtherGroupRequest(m_TabIndex);
}
