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
    void setCodeCompleterSettings(bool completerEnabled, bool emmetEnabled);
    QByteArray formatJson(const QByteArray& json_data);
    std::pair<bool,bool> getCodeCompleterSettings();
};

#endif // SETTINGSSTOREEXTEND_H
