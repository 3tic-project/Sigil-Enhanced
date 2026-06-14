#pragma once
#ifndef MODIFIEDVERPREFSWIDGET_H
#define MODIFIEDVERPREFSWIDGET_H

#include "PreferencesWidget.h"
#include "ui_PModifiedVerPrefs.h"

class ModifiedVerPrefsWidget :  public PreferencesWidget
{
	Q_OBJECT

public:
	ModifiedVerPrefsWidget();
	PreferencesWidget::ResultActions saveSettings();

private slots:
	void resetXhtmlFormat(); //modified: XHTML Fomat Configure

private:
	void readSettings();
	void connectSignalsToSlots();

	Ui::PModifiedVerPrefsWidget ui;

};

#endif // !MODIFIEDVERPREFSWIDGET_H
