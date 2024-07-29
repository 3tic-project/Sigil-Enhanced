#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>

#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "qtextcodec.h" // modified: XHTML Fomat Configure

/*------------------ modified: XHTML Fomat Configure ----------------------*/

SettingsStoreExtend::SettingsStoreExtend()
   :QSettings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini",QSettings::IniFormat)
{
}

QString SettingsStoreExtend::getXhtmlFormat() {
    if (!contains("user_preferences/xhtml_format")) {
        return QString();
    }
    return value("user_preferences/xhtml_format").toString();
}

void SettingsStoreExtend::setXhtmlFormat(QString conf) {
    setValue("user_preferences/xhtml_format", conf);
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
    setValue("user_preferences/ignore_blankline", ignoreBlankLine);
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
    setValue("user_preferences/completer_enabled", completerEnabled);
}

void SettingsStoreExtend::setEmmetEnabled(bool emmetEnabled) {
    setValue("user_preferences/emmet_enabled", emmetEnabled);
}

bool SettingsStoreExtend::getCompleterEnabled(){
    bool completerEnabled = true;
    if (contains("user_preferences/completer_enabled")) {
        completerEnabled = value("user_preferences/completer_enabled").toBool();
    }
    return completerEnabled;
}

bool SettingsStoreExtend::getEmmetEnabled(){
    bool emmetEnabled = true;
    if (contains("user_preferences/emmet_enabled")) {
        emmetEnabled = value("user_preferences/emmet_enabled").toBool();
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
    setValue("user_preferences/txt_ignore_blankline", ignore);
}

bool SettingsStoreExtend::getIgnoreBlankLine() {

    bool ignore = false;
    if (contains("user_preferences/txt_ignore_blankline")) {
        ignore = value("user_preferences/txt_ignore_blankline").toBool();
    }
    return ignore;
}

void SettingsStoreExtend::setFindReplaceEnhancedMode(bool isEnhancedMode) {
    setValue("user_preferences/find_replace_enhaced", isEnhancedMode);
}
bool SettingsStoreExtend::getFindReplaceEnhancedMode() {
    bool isEnhancedMode = true;
    if (contains("user_preferences/find_replace_enhaced")) {
        isEnhancedMode = value("user_preferences/find_replace_enhaced").toBool();
    }
    return isEnhancedMode;
}
