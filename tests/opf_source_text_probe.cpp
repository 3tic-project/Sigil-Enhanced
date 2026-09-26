#include "ResourceObjects/OPFSourceText.h"

#include <QByteArray>
#include <QString>

#include <iostream>
#include <string>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto separator = line.find('\t');
        if (separator == std::string::npos) return 1;
        const QByteArray oldBytes = QByteArray::fromHex(QByteArray::fromStdString(line.substr(0, separator)));
        const QByteArray editedBytes = QByteArray::fromHex(QByteArray::fromStdString(line.substr(separator + 1)));
        const QString restored = OPFSourceText::Restore(QString::fromUtf8(oldBytes),
                                                        QString::fromUtf8(editedBytes));
        std::cout << restored.toUtf8().toHex().constData() << '\n';
    }
    return 0;
}
