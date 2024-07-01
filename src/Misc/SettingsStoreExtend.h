#pragma once
#ifndef SETTINGSSTOREEXTEND_H
#define SETTINGSSTOREEXTEND_H

#include <QMap>
#include <QSettings>
#include <QJsonObject>

/*------------------ modified: XHTML Fomat Configure ----------------------*/

class SettingsStoreExtend
{
public:
    SettingsStoreExtend();
    void setXhtmlFormat(QString conf);
    QString getXhtmlFormat();
    void setHTMLCompleterWordsJson(const QJsonObject &json);
    void setCSSCompleterWordsJson(const QJsonObject& json);
    QJsonObject getHTMLCompleterWordsJson();
    QJsonObject getCSSCompleterWordsJson();
    void setCompleterEnabled(bool completerEnabled);
    void setEmmetEnabled(bool emmetEnabled);
    void setTxtImportingSettings(bool ignoreBlankLine);
    QByteArray formatJson(const QByteArray& json_data);
    bool getCompleterEnabled();
    bool getEmmetEnabled();
    void setIgnoreBlankLine(bool ignore);
    bool getIgnoreBlankLine();
private:
    bool isSettingsInitialized;
    QSettings settings;
    void initSettings();
};

#endif // SETTINGSSTOREEXTEND_H
