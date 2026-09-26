#include "BookManipulation/ManifestIdRebase.h"

#include <iostream>
#include <string>

#include <QByteArray>
#include <QString>
#include <QStringList>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const QStringList parts = QString::fromStdString(line).split(QLatin1Char('\t'));
            if (parts.size() != 2) return 1;
            const QString source = QString::fromUtf8(QByteArray::fromHex(parts.at(1).toLatin1()));
            const QString result = parts.at(0) == QLatin1String("L") ? ManifestIdRebase::Legacy(source)
                : parts.at(0) == QLatin1String("P") ? ManifestIdRebase::Preserving(source)
                : QString();
            if (parts.at(0) != QLatin1String("L") && parts.at(0) != QLatin1String("P")) return 1;
            std::cout << 'S' << result.toUtf8().toHex().constData() << '\n';
        } catch (const std::exception &) {
            std::cout << "E\n";
        }
    }
    return 0;
}
