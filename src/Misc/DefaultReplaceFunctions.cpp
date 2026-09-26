#include "Misc/DefaultReplaceFunctions.h"

#include <QFile>

namespace DefaultReplaceFunctions
{

bool CreateIfMissing(const QString &path)
{
    QFile defaults(QStringLiteral(":/replace_functions/defaults.json"));
    if (!defaults.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = defaults.readAll();
    if (bytes.isEmpty()) return false;

    // QFile::copy would copy the read-only permissions of the Qt resource.
    // Create a writable user file while keeping Python's no-overwrite rule.
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    const bool saved = output.write(bytes) == bytes.size() && output.flush();
    output.close();
    if (!saved) QFile::remove(path);
    return saved;
}

}
