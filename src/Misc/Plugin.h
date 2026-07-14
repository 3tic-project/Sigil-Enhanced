/************************************************************************
**
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

#pragma once
#ifndef PLUGIN_H
#define PLUGIN_H

#include <QHash>
#include <QStringList>

class QString;

class Plugin
{
public:
    enum RuntimeMode {
        LegacyRuntime,
        LiveRuntime
    };

    Plugin();
    Plugin(const QHash<QString, QString> &info);
    ~Plugin();

    QHash<QString, QString> serialize();

    bool isvalid();

    QString get_name();
    QString get_author();
    QString get_description();
    QString get_type();
    QString get_version();
    QString get_engine();
    QString get_oslist();
    QString get_autostart();
    QString get_autoclose();
    QString get_iconpath();
    int get_api_version() const;
    QString get_api_interface() const;
    QString get_lifetime() const;
    QStringList get_permissions() const;
    QStringList get_events() const;
    RuntimeMode get_declared_runtime() const;
    static QStringList SupportedEngines();

    void set_name(const QString &val);
    void set_author(const QString &val);
    void set_description(const QString &val);
    void set_type(const QString &val);
    void set_version(const QString &val);
    void set_engine(const QString &val);
    void set_oslist(const QString &val);
    void set_autostart(const QString &val);
    void set_autoclose(const QString &val);
    void set_iconpath(const QString &val);
    void set_api(int version, const QString &interface_name);
    void set_lifetime(const QString &val);
    void add_permission(const QString &val);
    void add_event(const QString &val);

private:
    QString m_name;
    QString m_author;
    QString m_description;
    QString m_type;
    QString m_version;
    QString m_engine;
    QString m_oslist;
    QString m_autostart;
    QString m_autoclose;
    QString m_iconpath;
    int m_apiVersion = 1;
    QString m_apiInterface;
    QString m_lifetime;
    QStringList m_permissions;
    QStringList m_events;
};

#endif // PLUGIN_H
