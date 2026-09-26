#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace UpdateVersionFeed {

// Empty means a failed request, invalid UTF-8, or no current-version element.
QString ParseVersion(const QByteArray& xml);
QString FetchVersion(const QUrl& url, int timeout_ms = 2000);

}
