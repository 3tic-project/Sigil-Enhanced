#include <iostream>
#include <string>

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "BookManipulation/XmlProcessor.h"

// Line protocol: operation, then hex UTF-8 fields. Replies "S<hex>" or "F" when
// the native path defers to the recovering legacy parser.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const auto field = [](const QString &hex) {
        const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
        // QString::fromUtf8 drops a leading BOM; keep it as text.
        return bytes.startsWith("\xEF\xBB\xBF") ? QChar(0xFEFF) + QString::fromUtf8(bytes.mid(3)) : QString::fromUtf8(bytes);
    };
    const auto map = [](const QString &json) {
        QHash<QString, QString> result;
        const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
        for (auto it = object.begin(); it != object.end(); ++it) result.insert(it.key(), it.value().toString());
        return result;
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        QStringList parts = QString::fromStdString(line).split(QLatin1Char('\t'));
        const QString operation = parts.takeFirst();
        QStringList values;
        for (const QString &part : parts) values.append(field(part));
        QString out;
        bool ok = true;
        if (operation == QLatin1String("R")) ok = XmlProcessor::RepairXML(values.at(0), values.at(1), &out);
        else if (operation == QLatin1String("O")) ok = XmlProcessor::PerformOPFSourceUpdates(values.at(0), values.at(1), values.at(2), map(values.at(3)), &out);
        else if (operation == QLatin1String("N")) ok = XmlProcessor::PerformNCXSourceUpdates(values.at(0), values.at(1), values.at(2), map(values.at(3)), &out);
        else if (operation == QLatin1String("S")) ok = XmlProcessor::PerformSMILUpdates(values.at(0), values.at(1), values.at(2), map(values.at(3)), &out);
        else if (operation == QLatin1String("P")) ok = XmlProcessor::PerformPageMapUpdates(values.at(0), values.at(1), values.at(2), map(values.at(3)), &out);
        else if (operation == QLatin1String("A")) ok = XmlProcessor::AnchorNCXUpdates(values.at(0), values.at(1), values.at(2), map(values.at(3)), &out);
        else if (operation == QLatin1String("M")) {
            QStringList merged;
            for (const QJsonValue &value : QJsonDocument::fromJson(values.at(3).toUtf8()).array()) merged.append(value.toString());
            ok = XmlProcessor::AnchorNCXUpdatesAfterMerge(values.at(0), values.at(1), values.at(2), merged, &out);
        }
        else if (operation == QLatin1String("E")) out = XmlProcessor::UrlEncodePart(values.at(0));
        else if (operation == QLatin1String("D")) out = XmlProcessor::UrlDecodePart(values.at(0));
        else if (operation == QLatin1String("B")) out = XmlProcessor::BuildBookPath(values.at(0), values.at(1));
        else if (operation == QLatin1String("L")) out = XmlProcessor::BuildRelativePath(values.at(0), values.at(1));
        else if (operation == QLatin1String("T")) out = XmlProcessor::StartingDir(values.at(0));
        else if (operation == QLatin1String("X")) ok = XmlProcessor::RebuildOpfXml(values.at(0), &out);
        else return 1;
        if (ok) std::cout << 'S' << out.toUtf8().toHex().constData() << '\n';
        else std::cout << "F\n";
        std::cout.flush();
    }
    return 0;
}
