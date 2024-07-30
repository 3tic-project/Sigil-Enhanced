/************************************************************************
**
**  Copyright (C) 2022 Kevin B. Hendricks, Stratford, Ontario
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

#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>
#include <QPainter>
#include <QModelIndex>
#include <QStyledItemDelegate>
#include "Misc/NumericItem.h"
#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/TextResource.h"
#include "PCRE2/PCRECache.h"
#include "PCRE2/SPCRE.h"
#include "Dialogs/ReplacementPreviewPlus.h"
#include "MainUI/FindReplacePlus.h"

static const QString SETTINGS_GROUP = "replacement_preview";

// These need to be changed if columns added or deleted
static const int BEFORE_COL = 0;
static const int AFTER_COL = 1;

static const int ISGROUP_ROLE = Qt::UserRole + 1;
static const int OFFSET_ROLE = Qt::UserRole + 2;
static const int SELECTION_START_ROLE = Qt::UserRole + 3;
static const int SELECTION_END_ROLE = Qt::UserRole + 4;
static const int SELECTED_TEXT_ROLE = Qt::UserRole + 5;

ReplacementPreviewPlus::ReplacementPreviewPlus(QWidget* parent)
    :
    QDialog(parent),
    m_ContextMenu(new QMenu(this)),
    m_replacement_count(0),
    m_current_count(0)
{
    m_FindReplacePlus = qobject_cast<FindReplacePlus*>(parent);
    ui.setupUi(this);

    //ReadSettings();
    connectSignalsSlots();
}


ReplacementPreviewPlus::~ReplacementPreviewPlus()
{
}

void ReplacementPreviewPlus::closeEvent(QCloseEvent *e)
{
    //WriteSettings();
    QDialog::closeEvent(e);
}



void ReplacementPreviewPlus::ApplyReplacements()
{
    close();
}

void ReplacementPreviewPlus::ReadSettings()
{

}
void ReplacementPreviewPlus::WriteSettings()
{

}

void ReplacementPreviewPlus::connectSignalsSlots()
{

}
