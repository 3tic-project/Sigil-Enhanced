/************************************************************************
**
**  Copyright (C) 2022 Kevin B. Hendricks, Stratford, Ontario, Canada
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
#ifndef REPLACEMENTPREVIEWPLUS_H
#define REPLACEMENTPREVIEWPLUS_H

#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QDialog>
#include <QHash>
#include <QStandardItemModel>
#include "ui_ReplacementPreviewPlus.h"



class QString;
class QCloseEvent;
class Resource;
class FindReplacePlus;
class ReplacementPreviewPlus: public QDialog
{
    Q_OBJECT

public:
    ReplacementPreviewPlus(QWidget* parent=NULL);
    ~ReplacementPreviewPlus();

    int GetReplacementCount() { return m_replacement_count; };

public slots:

    void closeEvent(QCloseEvent *e);

private slots:
    void ApplyReplacements();

private:

    void ReadSettings();
    void WriteSettings();

    void connectSignalsSlots();

    FindReplacePlus* m_FindReplacePlus;

    int m_context_amt;

    QAction *m_Delete;

    QPointer<QMenu> m_ContextMenu;

    QHash<QString, Resource*> m_Resources;

    int m_replacement_count;

    int m_current_count;

    Ui::ReplacementPreviewPlus ui;

};

#endif // REPLACEMENTPREVIEWPLUS_H
