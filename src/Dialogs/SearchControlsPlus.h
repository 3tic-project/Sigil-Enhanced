/************************************************************************
**
**  Copyright (C) 2021-2024 Kevin B. Hendricks, Stratford, Ontario, Canada
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
#ifndef SEARCHCONTROLSPLUS_H
#define SEARCHCONTROLSPLUS_H


#include "ui_SearchControlsPlus.h"

class QCloseEvent;
class QShowEvent;

class SearchControlsPlus : public QDialog
{
    Q_OBJECT

public:
    SearchControlsPlus(QWidget* parent);
    ~SearchControlsPlus();

    QString GetLookWhere();
    QString GetSearchMode();
    QString GetSearchDirection();

    void SetSearchMode(QString code);
    void SetLookWhere(QString code);
    void SetSearchDirection(QString code);

    void UpdateSearchControls(const QString &text = QString());
    QString GetControlsCode();


public slots:
    void DoClearAll();

    void closeEvent(QCloseEvent *e);
    void showEvent(QShowEvent *e);
    void show();
    void hide();


private:

    void ReadSettings();
    void WriteSettings();
    void ExtendUI();

    void ConnectSignalsToSlots();


    ///////////////////////////////
    // PRIVATE MEMBER VARIABLES
    ///////////////////////////////

    Ui::SearchControlsPlus ui;

    bool m_ClearAll;
};

#endif // SEARCHCONTROLSPLUS_H
