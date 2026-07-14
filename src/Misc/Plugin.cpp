/************************************************************************
**
**  Copyright (C) 2024 Kevin B. Hendricks, Stratford ON Canada
**  Copyright (C) 2014 John Schember <john@nachtimwald.com>
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

#include <QString>
#include <QStringList>

#include "Misc/Plugin.h"

#if defined(__APPLE__)
static const QString POS = "osx";
#elif defined(_WIN32)
static const QString POS = "win";
#else
static const QString POS = "unx";
#endif

Plugin::Plugin()
{
}

Plugin::Plugin(const QHash<QString, QString> &info)
{
    if (info.contains("name")) {
        set_name(info.value("name"));
    }
    if (info.contains("author")) {
        set_author(info.value("author"));
    }
    if (info.contains("description")) {
        set_description(info.value("description"));
    }
    if (info.contains("type")) {
        set_type(info.value("type"));
    }
    if (info.contains("version")) {
        set_version(info.value("version"));
    }
    if (info.contains("engine")) {
        set_engine(info.value("engine"));
    }
    if (info.contains("oslist")) {
        set_oslist(info.value("oslist"));
    }
    if (info.contains("autostart")) {
        set_autostart(info.value("autostart"));
    }
    if (info.contains("autoclose")) {
        set_autoclose(info.value("autoclose"));
    }
    if (info.contains("iconpath")) {
        set_iconpath(info.value("iconpath"));
    }
    if (info.contains("api_version")) {
        set_api(info.value("api_version").toInt(), info.value("api_interface"));
    }
    if (info.contains("lifetime")) {
        set_lifetime(info.value("lifetime"));
    }
    foreach (const QString &permission, info.value("permissions").split(',', Qt::SkipEmptyParts)) {
        add_permission(permission);
    }
    foreach (const QString &event, info.value("events").split(',', Qt::SkipEmptyParts)) {
        add_event(event);
    }


}

Plugin::~Plugin()
{
}

QHash<QString, QString> Plugin::serialize() const
{
    QHash <QString, QString> info;

    info.insert("name", get_name());
    info.insert("author", get_author());
    info.insert("description", get_description());
    info.insert("type", get_type());
    info.insert("version", get_version());
    info.insert("engine", get_engine());
    info.insert("oslist", get_oslist());
    info.insert("autostart", get_autostart());
    info.insert("autoclose", get_autoclose());
    info.insert("iconpath", get_iconpath());
    info.insert("api_version", QString::number(get_api_version()));
    info.insert("api_interface", get_api_interface());
    info.insert("lifetime", get_lifetime());
    info.insert("permissions", get_permissions().join(','));
    info.insert("events", get_events().join(','));

    return info;
}

bool Plugin::isvalid() const
{
    return (!m_name.isEmpty()   &&
            !m_type.isEmpty()   &&
            (!m_engine.isEmpty() && SupportedEngines().contains(m_engine)) &&
            (m_oslist.isEmpty() || m_oslist.split(',', Qt::SkipEmptyParts).contains(POS)));
}

QString Plugin::get_name() const
{
    return m_name;
}

QString Plugin::get_author() const
{
    return m_author;
}

QString Plugin::get_description() const
{
    return m_description;
}

QString Plugin::get_type() const
{
    return m_type;
}

QString Plugin::get_version() const
{
    return m_version;
}

QString Plugin::get_engine() const
{
    return m_engine;
}

QString Plugin::get_autostart() const
{
  if (m_autostart.isEmpty()) {
     return "false";
  }
  return m_autostart;
}

QString Plugin::get_autoclose() const
{
  if (m_autoclose.isEmpty()) {
     return "false";
  }
  return m_autoclose;
}

QString Plugin::get_oslist() const
{
    return m_oslist;
}


QString Plugin::get_iconpath() const
{
    return m_iconpath;
}

int Plugin::get_api_version() const
{
    return m_apiVersion;
}

QString Plugin::get_api_interface() const
{
    return m_apiInterface;
}

QString Plugin::get_lifetime() const
{
    return m_lifetime.isEmpty() ? QStringLiteral("command") : m_lifetime;
}

QStringList Plugin::get_permissions() const
{
    return m_permissions;
}

QStringList Plugin::get_events() const
{
    return m_events;
}

Plugin::RuntimeMode Plugin::get_declared_runtime() const
{
    return m_apiVersion == 2 && m_apiInterface == QStringLiteral("live")
        ? LiveRuntime : LegacyRuntime;
}

QStringList Plugin::SupportedEngines()
{
    return QStringList {
        QStringLiteral("python3"),
        QStringLiteral("python3.4"),
        QStringLiteral("python2.7,python3.4"),
        QStringLiteral("python3.4,python2.7")
    };
}

void Plugin::set_name(const QString &val)
{
    m_name = val;
}

void Plugin::set_author(const QString &val)
{
    m_author = val;
}

void Plugin::set_description(const QString &val)
{
    m_description = val;
}

void Plugin::set_type(const QString &val)
{
    m_type = val;
}

void Plugin::set_version(const QString &val)
{
    m_version = val;
}

// multiple engines are possible
void Plugin::set_engine(const QString &val)
{
    if (!m_engine.isEmpty()) {
        m_engine = m_engine + "," + val;
    } else {
        m_engine = val;
    }
}

void Plugin::set_oslist(const QString &val)
{
    m_oslist = val;
}

void Plugin::set_autostart(const QString &val)
{
    if (!val.isEmpty()) {
        m_autostart = val.toLower();
    }
}

void Plugin::set_autoclose(const QString &val)
{
    if (!val.isEmpty()) {
        m_autoclose = val.toLower();
    }
}

void Plugin::set_iconpath(const QString &val)
{
    m_iconpath = val;
}

void Plugin::set_api(int version, const QString &interface_name)
{
    m_apiVersion = version > 0 ? version : 1;
    m_apiInterface = interface_name.trimmed().toLower();
}

void Plugin::set_lifetime(const QString &val)
{
    const QString lifetime = val.trimmed().toLower();
    if (lifetime == QStringLiteral("command") || lifetime == QStringLiteral("book-session")) {
        m_lifetime = lifetime;
    }
}

void Plugin::add_permission(const QString &val)
{
    const QString permission = val.trimmed();
    if (!permission.isEmpty() && !m_permissions.contains(permission)) {
        m_permissions.append(permission);
    }
}

void Plugin::add_event(const QString &val)
{
    const QString event = val.trimmed();
    if (!event.isEmpty() && !m_events.contains(event)) {
        m_events.append(event);
    }
}
