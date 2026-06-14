
#include "ModifiedVerPrefsWidget.h"
#include "Misc/SettingsStoreExtend.h"

ModifiedVerPrefsWidget::ModifiedVerPrefsWidget()
{
	ui.setupUi(this);
    readSettings();
    connectSignalsToSlots();
}

PreferencesWidget::ResultActions ModifiedVerPrefsWidget::saveSettings()
{
	PreferencesWidget::ResultActions results = PreferencesWidget::ResultAction_None;

    SettingsStoreExtend sse;

	// XHTML Fomat Configure
    sse.setXhtmlFormat(ui.editXHTMLFormat->toPlainText());

    // modified: CodeCompleterParser
    bool needToReset = false;
    bool completer_enabled = ui.EnableCompleter->isChecked();
    bool emmet_enabled = ui.EnableEmmet->isChecked();
    bool old_completerEnabled = sse.getCompleterEnabled();
    bool old_emmetEnabled = sse.getEmmetEnabled();
    needToReset = completer_enabled != old_completerEnabled || emmet_enabled != old_emmetEnabled;
    if (needToReset) {
        sse.setCompleterEnabled(completer_enabled);
        sse.setEmmetEnabled(emmet_enabled);
        results = results | PreferencesWidget::ResultAction_ReloadTabs;
    }
    // modified: TxtImporting
    bool ignore_blankline = ui.IgnoreBlankLine->isChecked();
    if (ignore_blankline != sse.getIgnoreBlankLine())
        sse.setIgnoreBlankLine(ignore_blankline);

	return results;
}

void ModifiedVerPrefsWidget::readSettings()
{
    SettingsStoreExtend sse;

    // XHTML Fomat Configure
    if (sse.getXhtmlFormat().isNull()) {
        ui.editXHTMLFormat->setDefaultText();
    }
    else {
        ui.editXHTMLFormat->setPlainText(sse.getXhtmlFormat());
    }

    // modified: CodeCompleterParser
    bool completerEnabled = sse.getCompleterEnabled();
    bool emmetEnabled = sse.getEmmetEnabled();
    ui.EnableCompleter->setChecked(completerEnabled);
    ui.EnableEmmet->setChecked(emmetEnabled);
    // modified: TxtImporting
    ui.IgnoreBlankLine->setChecked(sse.getIgnoreBlankLine());
}

void ModifiedVerPrefsWidget::connectSignalsToSlots()
{
    connect(ui.htmlFormatResetButton, SIGNAL(clicked()), this, SLOT(resetXhtmlFormat())); // XHTML Fomat Configure
}

void ModifiedVerPrefsWidget::resetXhtmlFormat()
{
    ui.editXHTMLFormat->setDefaultText();
}