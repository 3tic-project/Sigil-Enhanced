#include "BookManipulation/NcxGenerator.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <iostream>
#include <string>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        QStringList arguments;
        size_t begin = 0;
        while (begin <= line.size()) {
            const size_t end = line.find('\t', begin);
            const std::string field = line.substr(begin, end == std::string::npos ? end : end - begin);
            arguments.append(QString::fromUtf8(QByteArray::fromHex(QByteArray::fromStdString(field))));
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        if (arguments.size() != 5) return 1;
        const QString output = NcxGenerator::Generate(arguments.at(0), arguments.at(1),
                                                       arguments.at(2), arguments.at(3),
                                                       arguments.at(4));
        std::cout << output.toUtf8().toHex().constData() << '\n';
    }
    return 0;
}
