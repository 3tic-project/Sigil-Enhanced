/************************************************************************
**
**  Copyright (C) 2018-2025  Kevin Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2018-2025  Doug Massay
**  Copyright (C) 2014  John Schember <john@nachtimwald.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QXmlStreamReader>

#include "Misc/Plugin.h"
#include "Misc/PluginDB.h"
#include "Misc/SafeArchiveExtractor.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "sigil_constants.h"

PluginDB *PluginDB::m_instance = 0;

PluginDB *PluginDB::instance()
{
    if (m_instance == 0) {
        m_instance = new PluginDB();
    }

    return m_instance;
}

PluginDB::PluginDB()
{
    SettingsStore ss;

    m_engine_paths = ss.pluginEnginePaths();

    QDir pluginDir(pluginsPath());
    if (!pluginDir.exists()) {
        pluginDir.mkpath(pluginsPath());
    }
}

PluginDB::~PluginDB()
{
    foreach(Plugin *p, m_plugins) {
        delete p;
    }
    m_plugins.clear();

    if (m_instance == this) {
        m_instance = nullptr;
    }
}

QString PluginDB::pluginsPath()
{
    return Utility::DefinePrefsDir() + "/plugins";
}


QString PluginDB::buildBundledInterpPath()
{
  QString bundled_python3_path;

#ifdef Q_OS_MAC
  // On Mac OS X QCoreApplication::applicationDirPath() points to Sigil.app/Contents/MacOS/
  // is located, but the Python.framework dir is in Contents/Frameworks
  QDir execdir(QCoreApplication::applicationDirPath());
  execdir.cdUp();
  bundled_python3_path = execdir.absolutePath() + PYTHON_MAIN_BIN_PATH;
#elif defined(Q_OS_WIN32)
  bundled_python3_path = QCoreApplication::applicationDirPath() + "/python3.exe";
#else
  if (APPIMAGE_BUILD) {
      bundled_python3_path = QCoreApplication::applicationDirPath() + "/python3";
  } else {
      bundled_python3_path = "";
  }

#endif

  QFileInfo checkPython3(bundled_python3_path);
  if (checkPython3.exists() && checkPython3.isFile() && checkPython3.isReadable() && checkPython3.isExecutable() ) {
    return bundled_python3_path;
  }
  return "";
}


QString PluginDB::launcherRoot()
{
    QString     launcher_root;
    QStringList launcher_roots;
    QDir        d;

#ifdef Q_OS_MAC
    launcher_roots.append(QCoreApplication::applicationDirPath() + "/../plugin_launchers/");
#elif defined(Q_OS_WIN32)
    launcher_roots.append(QCoreApplication::applicationDirPath() + "/plugin_launchers/");
#elif !defined(Q_OS_WIN32) && !defined(Q_OS_MAC)
    // user supplied environment variable to 'share/sigil' directory will overrides everything
    if (!sigil_extra_root.isEmpty()) {
        launcher_root = sigil_extra_root + "/plugin_launchers/";
    } else {
        launcher_roots.append(sigil_share_root + "/plugin_launchers/");
    }
#endif

    Q_FOREACH (QString s, launcher_roots) {
        if (d.exists(s)) {
            launcher_root = s;
            break;
        }
    }

    QDir base(launcher_root);
    return base.absolutePath();
}

void PluginDB::load_plugins_from_disk(bool force)
{
    QDir        d(pluginsPath());
    QStringList dplugins;

    if (!d.exists()) {
        return;
    }

    dplugins = d.entryList(QStringList("*"), QDir::Dirs|QDir::NoDotAndDotDot);

    Q_FOREACH(QString p, dplugins) {
        add_plugin_int(p, force);
    }

    emit plugins_changed();
}

PluginDB::AddResult PluginDB::add_plugin(const QString &path, bool force)
{
    QFileInfo zipinfo(path);
    QString name = zipinfo.baseName();

    // strip off any versioning present in zip name after first "_" to get internal folder name
    int version_index = name.indexOf("_");
    if (version_index > -1) {
        name.truncate(version_index);
    }

    const QString targetPath = QDir(pluginsPath()).filePath(name);
    if (QFileInfo::exists(targetPath) && !force) {
        return PluginDB::AR_EXISTS;
    }

    QTemporaryDir staging(QDir(pluginsPath()).filePath(".install-XXXXXX"));
    if (!staging.isValid()) {
        return PluginDB::AR_UNZIP;
    }
    const SafeArchiveExtractor::Result extractResult =
        SafeArchiveExtractor::extract(path, staging.path());
    if (!extractResult.ok) {
        return PluginDB::AR_UNZIP;
    }

    bool hasPluginXml = false;
    for (const SafeArchiveExtractor::Entry& entry : extractResult.entries) {
        if (entry.path.section('/', 0, 0) != name) {
            return PluginDB::AR_INVALID;
        }
        hasPluginXml = hasPluginXml || entry.path == name + "/plugin.xml";
    }
    const QString stagedPluginPath = QDir(staging.path()).filePath(name);
    if (!hasPluginXml ||
        !QFileInfo::exists(QDir(stagedPluginPath).filePath("plugin.xml"))) {
        return PluginDB::AR_INVALID;
    }

    QString backupPath;
    if (QFileInfo::exists(targetPath)) {
        backupPath = QDir(pluginsPath()).filePath(
            ".backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!QDir().rename(targetPath, backupPath)) {
            return PluginDB::AR_UNZIP;
        }
    }
    if (!QDir().rename(stagedPluginPath, targetPath)) {
        if (!backupPath.isEmpty()) {
            QDir().rename(backupPath, targetPath);
        }
        return PluginDB::AR_UNZIP;
    }

    const PluginDB::AddResult result = add_plugin_int(name, force);
    if (result != PluginDB::AR_SUCCESS) {
        Utility::removeDir(targetPath);
        if (!backupPath.isEmpty()) {
            QDir().rename(backupPath, targetPath);
        }
        return result;
    }
    if (!backupPath.isEmpty()) {
        Utility::removeDir(backupPath);
    }

    emit plugins_changed();
    return PluginDB::AR_SUCCESS;
}

PluginDB::AddResult PluginDB::add_plugin_int(const QString &name, bool force)
{
    Plugin *plugin;

    if (!force && m_plugins.contains(name)) {
        return PluginDB::AR_EXISTS;
    }

    plugin = load_plugin(name);
    if (plugin == NULL) {
        return PluginDB::AR_XML;
    }

    if (force && m_plugins.contains(name)) {
        delete m_plugins.take(plugin->get_name());
    }

    m_plugins.insert(plugin->get_name(), plugin);
    return PluginDB::AR_SUCCESS;
}

void PluginDB::remove_plugin(const QString &name)
{
    if (!m_plugins.contains(name)) {
        return;
    }

    Plugin *p = m_plugins.take(name);
    if (p != NULL) {
        Utility::removeDir(pluginsPath() + "/" + p->get_dirname());
        delete p;
    }
    emit plugins_changed();
}

void PluginDB::remove_all_plugins()
{
    Plugin *p;
    foreach (QString k, m_plugins.keys()) {
        p = m_plugins.take(k);
        if (p != NULL) {
            Utility::removeDir(pluginsPath() + "/" + p->get_dirname());
            delete p;
        }
    }
    m_plugins.clear();
    emit plugins_changed();
}

Plugin *PluginDB::get_plugin(const QString &name)
{
    return m_plugins.value(name);
}

QHash<QString, Plugin *> PluginDB::all_plugins()
{
    return m_plugins;
}

QStringList PluginDB::engines()
{
    return m_engine_paths.keys();
}

QString PluginDB::get_engine_path(const QString &engine)
{
    return m_engine_paths.value(engine);
}

void PluginDB::set_engine_path(const QString &engine, const QString &path)
{
    SettingsStore ss;

    if (path.isEmpty()) {
        m_engine_paths.remove(engine);
    } else {
        m_engine_paths.insert(engine, path);
    }

    ss.setPluginEnginePaths(m_engine_paths);
}

Plugin *PluginDB::load_plugin(const QString &name)
{
    QString xmlpath = pluginsPath() + "/" + name + "/plugin.xml";
    QFile file(xmlpath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return NULL;
    }

    Plugin           *plugin = new Plugin();
    // Keep the install folder name separate from the display name in plugin.xml.
    plugin->set_dirname(name);
    QXmlStreamReader  reader(&file);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name().compare(QLatin1String("name")) == 0) {
                plugin->set_name(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("author")) == 0) {
                plugin->set_author(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("description")) == 0) {
                plugin->set_description(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("type")) == 0) {
                plugin->set_type(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("engine")) == 0) {
                plugin->set_engine(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("version")) == 0) {
                plugin->set_version(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("oslist")) == 0) {
                plugin->set_oslist(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("autostart")) == 0) {
                plugin->set_autostart(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("autoclose")) == 0) {
                plugin->set_autoclose(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("api")) == 0) {
                const QXmlStreamAttributes attributes = reader.attributes();
                plugin->set_api(attributes.value(QLatin1String("version")).toInt(),
                                attributes.value(QLatin1String("interface")).toString());
            } else if (reader.name().compare(QLatin1String("lifetime")) == 0) {
                plugin->set_lifetime(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("permission")) == 0) {
                plugin->add_permission(reader.readElementText());
            } else if (reader.name().compare(QLatin1String("event")) == 0) {
                plugin->add_event(reader.readElementText());
            }
        }
    }

    // First look for a persistent custom user-specified
    // icon in the plugin's preference folder. And then
    // next look in the plugin folder itself for a dev
    // supplied icon. Prefer svg versions to png versions
    QStringList iconpaths;
    iconpaths << pluginsPath() + "/../plugins_prefs/" + name + "/plugin.svg";
    iconpaths << pluginsPath() + "/../plugins_prefs/" + name + "/plugin.png";
    iconpaths << pluginsPath() + "/" + name + "/plugin.svg";
    iconpaths << pluginsPath() + "/" + name + "/plugin.png";
    foreach(QString ipath, iconpaths) {
        QFileInfo iconinfo(ipath);
        if (iconinfo.exists() && iconinfo.isFile() && iconinfo.isReadable()) {
            plugin->set_iconpath(iconinfo.absoluteFilePath());
            break;
        }
    }

    if (!plugin->isvalid()) {
        delete plugin;
        plugin = NULL;
    }

    return plugin;
}
