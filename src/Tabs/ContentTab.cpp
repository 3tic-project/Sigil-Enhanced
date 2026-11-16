/************************************************************************
**
**  Copyright (C) 2015-2022 Kevin B. Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
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

#include <QtWidgets/QLayout>
#include <QtWidgets/QVBoxLayout>

#include "Tabs/ContentTab.h"
#include "ResourceObjects/Resource.h"
#include "ViewEditors/Searchable.h"


ContentTab::ContentTab(Resource *resource, QWidget *parent)
    :
    QWidget(parent),
    m_Resource(resource),
    m_Layout(new QVBoxLayout(this)),
    m_resource_was_deleted(false)
{
    connect(resource, SIGNAL(Deleted(const Resource *)),          this, SLOT(UnderlyingResourceDeleted()));
    connect(resource, SIGNAL(Renamed(const Resource *, QString)), this, SLOT(EmitTabRenamed()));
    connect(resource, SIGNAL(Moved(const Resource *, QString)), this, SLOT(EmitTabRenamed()));
    connect(resource, SIGNAL(TellTabUpdateName(const Resource *, QString)), this, SLOT(EmitTabRenamed()));
    // Direct destruction (including a failed file removal during book
    // teardown) does not necessarily send Deleted().
    connect(resource, &QObject::destroyed, this, &ContentTab::UnderlyingResourceDeleted);
    m_Layout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_Layout);
}

ContentTab::~ContentTab()
{
   delete m_Layout;
   m_Resource = NULL;
}

QString ContentTab::GetFilename()
{
    return m_Resource ? m_Resource->Filename() : QString();
}


QString ContentTab::GetShortPathName()
{
    return m_Resource ? m_Resource->ShortPathName() : QString();
}


QIcon ContentTab::GetIcon()
{
    return m_Resource ? m_Resource->Icon() : QIcon();
}


Resource *ContentTab::GetLoadedResource()
{
    return m_Resource.data();
}


Searchable *ContentTab::GetSearchableContent()
{
    return NULL;
}


void ContentTab::Close()
{
    if (!GetResourceWasDeleted()) SaveTabContent();
    EmitDeleteMe();
}


void ContentTab::SaveTabContent()
{
}


void ContentTab::LoadTabContent()
{
}

void ContentTab::EmitCentralTabRequest()
{
    emit CentralTabRequest(this);
}

void ContentTab::EmitUndoRedoStateChanged()
{
    emit UndoRedoStateChanged();
}


void ContentTab::ContentChangedExternally()
{
    // Connected resource documents already update the editors. Activating the
    // window here steals focus and dismisses popups in other books/processes
    // when an Agent or another background writer changes a resource.
    update();
}

void ContentTab::ChangeCasing(const Utility::Casing casing)
{
}

bool ContentTab::GetResourceWasDeleted()
{  
    return m_resource_was_deleted || m_Resource.isNull();
}

void ContentTab::UnderlyingResourceDeleted()
{
      if (m_resource_was_deleted) return;
      m_resource_was_deleted = true;
      EmitDeleteMe();
}

void ContentTab::EmitDeleteMe()
{
    emit DeleteMe(this);
}


void ContentTab::EmitTabRenamed()
{
    emit TabRenamed(this);
}


void ContentTab::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    LoadTabContent();
}


void ContentTab::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    if (!GetResourceWasDeleted()) SaveTabContent();
}
