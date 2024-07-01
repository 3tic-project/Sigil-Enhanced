#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>

#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "qtextcodec.h" // modified: XHTML Fomat Configure

/*------------------ modified: XHTML Fomat Configure ----------------------*/

SettingsStoreExtend::SettingsStoreExtend()
   :isSettingsInitialized(false)
{
}

void SettingsStoreExtend::initSettings() 
{
    QString ini_path = Utility::DefinePrefsDir() + "/" + "sigil_extend.ini";
    settings.setPath(QSettings::IniFormat, QSettings::UserScope, ini_path);
    isSettingsInitialized = true;
}

QString SettingsStoreExtend::getXhtmlFormat() {
    if (!isSettingsInitialized) initSettings();
    if (!settings.contains("user_preferences/xhtml_format")) {
        return QString();
    }
    return settings.value("user_preferences/xhtml_format").toString();
}

void SettingsStoreExtend::setXhtmlFormat(QString conf) {
    if (!isSettingsInitialized) initSettings();
    settings.setValue("user_preferences/xhtml_format", conf);
}

void SettingsStoreExtend::setHTMLCompleterWordsJson(const QJsonObject& json) {
    QFile saveFile(Utility::DefinePrefsDir() + "/" + "html_completion_words.json");
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning("Couldn't open save file.");
        return;
    }
    saveFile.write(formatJson(QJsonDocument(json).toJson()));
}

void SettingsStoreExtend::setTxtImportingSettings(bool ignoreBlankLine) {
    if (!isSettingsInitialized) initSettings();
    settings.setValue("user_preferences/ignore_blankline", ignoreBlankLine);
}

void SettingsStoreExtend::setCSSCompleterWordsJson(const QJsonObject& json) {
    QFile saveFile(Utility::DefinePrefsDir() + "/" + "css_completion_words.json");
    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning("Couldn't open save file.");
        return;
    }
    saveFile.write(formatJson(QJsonDocument(json).toJson()));
}
QJsonObject SettingsStoreExtend::getHTMLCompleterWordsJson() {
    QFile loadFile(Utility::DefinePrefsDir() + "/" + "html_completion_words.json");
    if (!loadFile.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open save file.");
        return QJsonObject();
    }
    QJsonObject json;
    QByteArray data = loadFile.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    json = doc.object();
    return json;
}
QJsonObject SettingsStoreExtend::getCSSCompleterWordsJson() {
    QFile loadFile(Utility::DefinePrefsDir() + "/" + "css_completion_words.json");
    if (!loadFile.open(QIODevice::ReadOnly)) {
        qWarning("Couldn't open save file.");
        return QJsonObject();
    }
    QJsonObject json;
    QByteArray data = loadFile.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    json = doc.object();
    return json;
}

void SettingsStoreExtend::setCompleterEnabled(bool completerEnabled) {
    if (!isSettingsInitialized) initSettings();
    settings.setValue("user_preferences/completer_enabled", completerEnabled);
}

void SettingsStoreExtend::setEmmetEnabled(bool emmetEnabled) {
    if (!isSettingsInitialized) initSettings();
    settings.setValue("user_preferences/emmet_enabled", emmetEnabled);
}

bool SettingsStoreExtend::getCompleterEnabled(){
    if (!isSettingsInitialized) initSettings();
    bool completerEnabled = true;
    if (settings.contains("user_preferences/completer_enabled")) {
        completerEnabled = settings.value("user_preferences/completer_enabled").toBool();
    }
    return completerEnabled;
}

bool SettingsStoreExtend::getEmmetEnabled(){
    if (!isSettingsInitialized) initSettings();
    bool emmetEnabled = true;
    if (settings.contains("user_preferences/emmet_enabled")) {
        emmetEnabled = settings.value("user_preferences/emmet_enabled").toBool();
    }
    return emmetEnabled;
}

QByteArray SettingsStoreExtend::formatJson(const QByteArray &json_data) {
    QByteArray new_data;
    bool inQuotation = false;
    bool inBracket = false;
    foreach(char ch, json_data) {
        if (!inQuotation && ch == '[') {
            inBracket = true;
        }
        else if (!inQuotation && ch == ']') {
            inBracket = false;
        }
        else if (ch == '"') {
            inQuotation = inQuotation ? false : true;
        }
        if (inBracket && !inQuotation) {
            if (ch == ' ' || ch == '\n' || ch == '\t') {
                continue;
            }
        }
        new_data.append(ch);
    }
    return new_data;
}

// Ignore blank line while converting txt file to xhtml file.
void SettingsStoreExtend::setIgnoreBlankLine(bool ignore) {
    if (!isSettingsInitialized) initSettings();
    settings.setValue("user_preferences/txt_ignore_blankline", ignore);
}

bool SettingsStoreExtend::getIgnoreBlankLine() {
    if (!isSettingsInitialized) initSettings();
    bool ignore = false;
    if (settings.contains("user_preferences/txt_ignore_blankline")) {
        ignore = settings.value("user_preferences/txt_ignore_blankline").toBool();
    }
    return ignore;
}