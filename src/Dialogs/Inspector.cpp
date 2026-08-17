/************************************************************************
 **
 **  Copyright (C) 2019-2026 Kevin B. Hendricks, Stratford Ontario Canada
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
#include <QtWebEngineWidgets>
#include <QtWebEngineCore>
#include <QWebEngineView>
#include <QWebEnginePage>

#include "Misc/WebProfileMgr.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "Dialogs/Inspector.h"

const float ZOOM_STEP               = 0.1f;
const float ZOOM_MIN                = 0.09f;
const float ZOOM_MAX                = 5.0f;
const float ZOOM_NORMAL             = 1.0f;

Inspector::Inspector(QWidget *parent) :
    QWidget(parent),
    m_Layout(new QVBoxLayout(this)),
    m_view(nullptr),
    m_LoadingFinished(false),
    m_LoadOkay(false),
    m_ZoomIn(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this)),
    m_ZoomOut(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this)),
    m_ZoomReset(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this))
{
    m_inspectView = new QWebEngineView(WebProfileMgr::instance().GetInspectorProfile(), this);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(QSize(200, 80));

    m_Layout->setContentsMargins(0, 0, 0, 0);
    m_Layout->setSpacing(0);
    m_Layout->addWidget(m_inspectView, 1);
    // QtWebEngine WebInspector needs to run javascript in MainWorld
    // See WebProfileMgr for profile settings
    // Toggle visibility from Preview's inspect button; no extra title bar.

    LoadSettings();
    connect(m_inspectView->page(), SIGNAL(loadFinished(bool)), this, SLOT(UpdateFinishedState(bool)));
    connect(m_inspectView->page(), SIGNAL(loadStarted()),      this, SLOT(LoadingStarted()));
    connect(m_ZoomIn,              SIGNAL(activated()),        this, SLOT(ZoomIn()));
    connect(m_ZoomOut,             SIGNAL(activated()),        this, SLOT(ZoomOut()));
    connect(m_ZoomReset,           SIGNAL(activated()),        this, SLOT(ZoomReset()));
}


Inspector::~Inspector()
{
    if (m_inspectView) {
        m_inspectView->close();
        if (m_inspectView->page()) {
            m_inspectView->page()->setInspectedPage(nullptr);
        }
        m_view = nullptr;
        delete m_inspectView;
        m_inspectView = nullptr;
    }
}

void Inspector::SetZoomFactor(float factor)
{
    if (factor > ZOOM_MAX) factor = ZOOM_MAX;
    if (factor < ZOOM_MIN) factor = ZOOM_MIN;
    SettingsStore settings;
    settings.setZoomInspector(factor);
    SetCurrentZoomFactor(factor);
    Zoom();
}

void  Inspector::ZoomIn()    { ZoomByStep(true);           }
void  Inspector::ZoomOut()   { ZoomByStep(false);          }
void  Inspector::ZoomReset() { SetZoomFactor(ZOOM_NORMAL); }

void  Inspector::SetCurrentZoomFactor(float factor) { m_CurrentZoomFactor = factor; }
float Inspector::GetZoomFactor() const              { return m_CurrentZoomFactor;   }

void Inspector::Zoom()
{
    if (m_inspectView) {
        m_inspectView->setZoomFactor(m_CurrentZoomFactor);
    }
}

void Inspector::ZoomByStep(bool zoom_in)
{
    // zoom out - neg. zoom step, round down; zoom in  - pos. zoom step, round UP
    float zoom_stepping       = zoom_in ? ZOOM_STEP : - ZOOM_STEP;
    float rounding_helper     = zoom_in ? 0.05f : - 0.05f;
    float current_zoom_factor = GetZoomFactor();
    float rounded_zoom_factor = Utility::RoundToOneDecimal(current_zoom_factor + rounding_helper);
    if (qAbs(current_zoom_factor - rounded_zoom_factor) < 0.01f) {
        SetZoomFactor(Utility::RoundToOneDecimal(current_zoom_factor + zoom_stepping));
    } else {
        SetZoomFactor(rounded_zoom_factor);
    }
}

void Inspector::LoadingStarted()
{
    m_LoadingFinished = false;
    m_LoadOkay = false;
}

void Inspector::UpdateFinishedState(bool okay)
{
    m_LoadingFinished = true;
    m_LoadOkay = okay;
}

void Inspector::InspectPageofView(QWebEngineView* view)
{
    m_view = view;

    if (m_inspectView && m_inspectView->page() && m_view) {
        m_inspectView->page()->setInspectedPage(m_view->page());
    }
}

void Inspector::StopInspection()
{
    m_view = nullptr;
    if (m_inspectView && m_inspectView->page()) {
        m_inspectView->page()->setInspectedPage(nullptr);
    }
}

QSize Inspector::sizeHint() const
{
    return QSize(450, 250);
}

void Inspector::LoadSettings()
{
    SettingsStore settings;
    SetCurrentZoomFactor(settings.zoomInspector());
    Zoom();
}
