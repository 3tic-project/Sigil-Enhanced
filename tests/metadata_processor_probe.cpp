#include <iostream>
#include <string>

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "Misc/MetadataProcessor.h"

// Line protocol: operation, then tab-separated hex UTF-8 fields.
//   X version opf                          -> S<hex JSON pieces> | F
//   A version data other idsJSON tag opf   -> S<hex opf> | F
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const auto field = [](const QString &hex) {
        const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
        return bytes.startsWith("\xEF\xBB\xBF") ? QChar(0xFEFF) + QString::fromUtf8(bytes.mid(3)) : QString::fromUtf8(bytes);
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        QStringList parts = QString::fromStdString(line).split(QLatin1Char('\t'));
        const QString operation = parts.takeFirst();
        QStringList values;
        for (const QString &part : parts) values.append(field(part));
        QByteArray out;
        bool ok = false;
        if (operation == QLatin1String("X")) {
            MetadataProcessor::Pieces pieces;
            ok = MetadataProcessor::Extract(values.at(1), values.at(0), &pieces);
            QJsonObject object;
            object["data"] = pieces.data;
            object["other"] = pieces.otherxml;
            object["ids"] = QJsonArray::fromStringList(pieces.idlist);
            object["tag"] = pieces.metatag;
            out = QJsonDocument(object).toJson(QJsonDocument::Compact);
        } else if (operation == QLatin1String("A")) {
            MetadataProcessor::Pieces pieces;
            pieces.data = values.at(1);
            pieces.otherxml = values.at(2);
            for (const QJsonValue &id : QJsonDocument::fromJson(values.at(3).toUtf8()).array()) pieces.idlist.append(id.toString());
            pieces.metatag = values.at(4);
            QString result;
            ok = MetadataProcessor::Apply(pieces, values.at(5), values.at(0), &result);
            out = result.toUtf8();
        } else {
            return 1;
        }
        if (ok) std::cout << 'S' << out.toHex().constData() << '\n';
        else std::cout << "F\n";
        std::cout.flush();
    }
    return 0;
}
