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

#ifndef INSPECTOR_H
#define INSPECTOR_H

#include <QWidget>
#include <QSize>
#include <QVBoxLayout>
#include <QtWebEngineWidgets>
#include <QtWebEngineCore>
#include <QWebEngineView>
#include <QShortcut>

class QWebEnginePage;

class Inspector : public QWidget
{
    Q_OBJECT

public:
    Inspector(QWidget *parent = nullptr);
    ~Inspector();

    bool IsLoadingFinished() { return m_LoadingFinished; }
    bool WasLoadOkay() { return m_LoadOkay; }

    QSize sizeHint() const override;

    void SetZoomFactor(float factor);
    void SetCurrentZoomFactor(float factor);
    float GetZoomFactor() const;
    void Zoom();
    void ZoomByStep(bool zoom_in);

public slots:
    void InspectPageofView(QWebEngineView *view);
    void StopInspection();

    void ZoomIn();
    void ZoomOut();
    void ZoomReset();

protected slots:
    void UpdateFinishedState(bool okay);
    void LoadingStarted();

private:
    void LoadSettings();

    QVBoxLayout *m_Layout;
    QWebEngineView *m_inspectView;
    QWebEngineView *m_view;
    bool m_LoadingFinished;
    bool m_LoadOkay;

    float m_CurrentZoomFactor;
    QShortcut *m_ZoomIn;
    QShortcut *m_ZoomOut;
    QShortcut *m_ZoomReset;
};

#endif // INSPECTOR_H
