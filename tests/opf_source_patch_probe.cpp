#include "ResourceObjects/OPFSourcePatch.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include <QByteArray>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const QString text = QString::fromStdString(line);
            const QStringList parts = text.split(QLatin1Char('\t'));
            if (parts.size() < 2) return 1;
            const auto field = [](const QString &hex) {
                return QString::fromUtf8(QByteArray::fromHex(hex.toLatin1()));
            };
            QString result;
            if (parts.at(0) == QLatin1String("M") && parts.size() == 2) result = OPFSourcePatch::ModelXml(field(parts.at(1)));
            else if (parts.at(0) == QLatin1String("A") && parts.size() == 4)
                result = OPFSourcePatch::ApplyModelUpdate(field(parts.at(1)), field(parts.at(2)), field(parts.at(3)));
            else if (parts.at(0) == QLatin1String("N") && parts.size() == 4)
                result = OPFSourcePatch::AddNavigationManifest(field(parts.at(1)), field(parts.at(2)), field(parts.at(3)));
            else if ((parts.at(0) == QLatin1String("P") && parts.size() == 4) || (parts.at(0) == QLatin1String("J") && parts.size() == 2)) {
                const QJsonDocument payload = QJsonDocument::fromJson(field(parts.last()).toUtf8());
                if (!payload.isObject()) throw std::runtime_error("payload is not a JSON object");
                // J returns the JSON text the application hands to the legacy Python.
                result = parts.at(0) == QLatin1String("J")
                    ? QString::fromUtf8(QJsonDocument(payload.object()).toJson(QJsonDocument::Compact))
                    : OPFSourcePatch::ApplyPackageUpdate(field(parts.at(1)), field(parts.at(2)), payload.object());
            }
            else return 1;
            std::cout << 'S' << result.toUtf8().toHex().constData() << '\n';
        } catch (const std::exception &) {
            std::cout << "E\n";
        }
    }
    return 0;
}
