#include "Misc/SettingsStoreExtend.h"
#include "QtCore/QString"
#include "Misc/Utility.h"

/*------------------ modified: XHTML Fomat Configure ----------------------*/

SettingsStoreExtend::SettingsStoreExtend()
    : QSettings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat)
{}

QString SettingsStoreExtend::getXhtmlFormat() {
    if (!contains("user_preferences/xhtml_format")) 
        return NULL;
    return value("user_preferences/xhtml_format").toString();
}

void SettingsStoreExtend::setXhtmlFormat(QString conf) {
    setValue("user_preferences/xhtml_format", conf);
}