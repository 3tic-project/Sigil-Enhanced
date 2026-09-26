#include <iostream>
#include <string>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "Misc/ReplaceFunctions.h"

// Line protocol for the legacy parity test: operation, then hex UTF-8 fields.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    std::string line;
    const auto field = [](const QString &hex) {
        const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
        // QString::fromUtf8 drops a leading BOM; U+FEFF is ordinary text here.
        return bytes.startsWith("\xEF\xBB\xBF") ? QChar(0xFEFF) + QString::fromUtf8(bytes.mid(3)) : QString::fromUtf8(bytes);
    };
    const auto data = [](const QString &text) {
        const QList<uint> values = text.toUcs4();
        return std::u32string(values.begin(), values.end());
    };
    const auto text = [](const std::u32string &value) {
        QString result;
        for (const char32_t item : value) {
            if (QChar::requiresSurrogates(item)) { result += QChar(QChar::highSurrogate(item)); result += QChar(QChar::lowSurrogate(item)); }
            else result += QChar(char16_t(item));
        }
        return result;
    };
    while (std::getline(std::cin, line)) {
        const QStringList parts = QString::fromStdString(line).split(QLatin1Char('\t'));
        const QString operation = parts.value(0);
        QString result;
        if (operation == QLatin1String("A") && parts.size() == 4) {
            ReplaceFunctions::Builtin builtin;
            if (!ReplaceFunctions::Resolve(field(parts.at(1)), QString(), &builtin)) return 1;
            QList<std::pair<int, int>> groups;
            for (const QString &group : field(parts.at(3)).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const QStringList bounds = group.split(QLatin1Char(','));
                groups.append({bounds.at(0).toInt(), bounds.at(1).toInt()});
            }
            result = ReplaceFunctions::Apply(builtin, field(parts.at(2)), groups);
        } else if (operation == QLatin1String("R") && parts.size() == 3) {
            ReplaceFunctions::Builtin builtin;
            result = ReplaceFunctions::Resolve(field(parts.at(1)), field(parts.at(2)), &builtin)
                ? QString::number(int(builtin)) : QStringLiteral("python");
        } else if (parts.size() == 2) {
            const std::u32string value = data(field(parts.at(1)));
            if (operation == QLatin1String("U")) result = text(ReplaceFunctions::Upper(value));
            else if (operation == QLatin1String("L")) result = text(ReplaceFunctions::Lower(value));
            else if (operation == QLatin1String("S")) result = text(ReplaceFunctions::Swapcase(value));
            else if (operation == QLatin1String("C")) result = text(ReplaceFunctions::Capitalize(value));
            else if (operation == QLatin1String("T")) result = text(ReplaceFunctions::Titlecase(value));
            else if (operation == QLatin1String("E")) result = text(ReplaceFunctions::Unescape(value));
            else return 1;
        } else {
            return 1;
        }
        std::cout << result.toUtf8().toHex().constData() << '\n';
    }
    return 0;
}
