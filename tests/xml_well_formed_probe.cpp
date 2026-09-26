#include "BookManipulation/XmlWellFormed.h"

#include <iostream>
#include <string>

#include <QByteArray>
#include <QString>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const QString source = QString::fromUtf8(QByteArray::fromHex(QByteArray::fromStdString(line)));
        const XmlWellFormed::Result result = XmlWellFormed::Check(source);
        std::cout << result.line << '\t' << result.column << '\t'
                  << result.message.toUtf8().toHex().constData() << '\n';
    }
    return 0;
}
