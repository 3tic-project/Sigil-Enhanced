#pragma once
#ifndef SETTINGSSTOREEXTEND_H
#define SETTINGSSTOREEXTEND_H

#include <QMap>
#include <QSettings>
#include <QJsonObject>

/*------------------ modified: XHTML Fomat Configure ----------------------*/

class SettingsStoreExtend : public QSettings
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
    void setFindReplaceEnhancedMode(bool isEnhancedMode);
    bool getFindReplaceEnhancedMode();
    void setOtherGroupTarget(const QString &target);
    QString getOtherGroupTarget();
    void setDivParagraphConvertBlankLines(bool enabled);
    bool getDivParagraphConvertBlankLines() const;
    void setDivParagraphConvertSceneBreaks(bool enabled);
    bool getDivParagraphConvertSceneBreaks() const;
    void setDivParagraphConvertImageWrappers(bool enabled);
    bool getDivParagraphConvertImageWrappers() const;
    void setDivParagraphConvertSingleBlockWrappers(bool enabled);
    bool getDivParagraphConvertSingleBlockWrappers() const;
    void setDivParagraphFormatSource(bool enabled);
    bool getDivParagraphFormatSource() const;
};

#endif // SETTINGSSTOREEXTEND_H
