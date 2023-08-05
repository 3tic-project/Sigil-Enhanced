#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>

#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "qtextcodec.h" // modified: XHTML Fomat Configure

/*------------------ modified: XHTML Fomat Configure ----------------------*/

SettingsStoreExtend::SettingsStoreExtend()
{
}

QString SettingsStoreExtend::getXhtmlFormat() {
    QSettings settings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat);
    if (!settings.contains("user_preferences/xhtml_format")) {
        return QString();
    }
    return settings.value("user_preferences/xhtml_format").toString();
}

void SettingsStoreExtend::setXhtmlFormat(QString conf) {
    QSettings settings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat);
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
void SettingsStoreExtend::setCodeCompleterSettings(bool completerEnabled, bool emmetEnabled){
    QSettings settings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat);
    settings.setValue("user_preferences/completer_enabled", completerEnabled);
    settings.setValue("user_preferences/emmet_enabled", emmetEnabled);
}
std::pair<bool, bool> SettingsStoreExtend::getCodeCompleterSettings(){
    QSettings settings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat);
    bool completerEnabled = true, emmetEnabled = true;
    if (settings.contains("user_preferences/completer_enabled")) {
        completerEnabled = settings.value("user_preferences/completer_enabled").toBool();
    }
    if (settings.contains("user_preferences/emmet_enabled")) {
        emmetEnabled = settings.value("user_preferences/emmet_enabled").toBool();
    }
    return {completerEnabled, emmetEnabled};
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