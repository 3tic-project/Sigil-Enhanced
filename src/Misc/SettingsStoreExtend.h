#pragma once
#ifndef SETTINGSSTOREEXTEND_H
#define SETTINGSSTOREEXTEND_H

#include <QtCore/QSettings>

/*------------------ modified: XHTML Fomat Configure ----------------------*/

class SettingsStoreExtend : public QSettings
{
private:
    void initXhtmlFormat();
public:
    QString m_defaultXhtmlFormat;
    SettingsStoreExtend();
    void setXhtmlFormat(QString conf);
    QString getXhtmlFormat();
};

#endif // SETTINGSSTOREEXTEND_H
