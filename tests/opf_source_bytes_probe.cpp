#include "ResourceObjects/OPFSourceBytes.h"

#include <QByteArray>
#include <QString>

#include <iostream>
#include <string>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const auto tab = line.find('\t');
            if (tab == std::string::npos || line.empty()) return 1;
            const QByteArray original = QByteArray::fromHex(QByteArray::fromStdString(line.substr(1, tab - 1)));
            QByteArray result;
            if (line.front() == 'L') {
                result = OPFSourceBytes::CodecName(QString::fromUtf8(original)).toUtf8();
            } else if (line.front() == 'D') {
                if (!OPFSourceBytes::CanDecodeNatively(original)) {
                    std::cout << "N\n";
                    continue;
                }
                result = OPFSourceBytes::Decode(original).toUtf8();
            } else if (line.front() == 'E') {
                const QByteArray source = QByteArray::fromHex(QByteArray::fromStdString(line.substr(tab + 1)));
                const QString text = QString::fromUtf8(source);
                if (!OPFSourceBytes::CanEncodeNatively(original, text)) {
                    std::cout << "N\n";
                    continue;
                }
                result = OPFSourceBytes::Encode(original, text);
            } else {
                return 1;
            }
            std::cout << 'S' << result.toHex().constData() << '\n';
        } catch (const std::exception &) {
            std::cout << "E\n";
        }
    }
    return 0;
}
