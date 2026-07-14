#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonObject>
#include <QTextDocument>

#include "PluginAPI/PluginTextEdit.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QJsonObject Edit(int start, int end, const QString &text)
{
    return QJsonObject {
        { QStringLiteral("start"), start },
        { QStringLiteral("end"), end },
        { QStringLiteral("text"), text }
    };
}

}

int main()
{
    const QString original = QStringLiteral("A\U0001f600BC");
    QList<PluginApi::TextEdit> edits;
    QString error;
    Require(PluginApi::ParseTextEdits(
                QJsonArray { Edit(1, 3, QStringLiteral("X")), Edit(4, 5, QStringLiteral("Z")) },
                original, &edits, &error),
            "valid UTF-16 edits were rejected");
    Require(edits.first().start == 4 && edits.last().start == 1, "edits are not in descending order");

    QTextDocument document;
    document.setPlainText(original);
    document.setUndoRedoEnabled(true);
    PluginApi::ApplyTextEdits(&document, edits);
    Require(document.toRawText() == QStringLiteral("AXBZ"), "text edits produced unexpected content");
    document.undo();
    Require(document.toRawText() == original, "text edits did not undo in one step");

    Require(!PluginApi::ParseTextEdits(QJsonArray { Edit(2, 3, QStringLiteral("x")) },
                                       original, &edits, &error),
            "surrogate-splitting edit was accepted");
    Require(error.contains(QStringLiteral("surrogate")), "surrogate error is unclear");
    Require(!PluginApi::ParseTextEdits(
                QJsonArray { Edit(0, 2, QStringLiteral("x")), Edit(1, 3, QStringLiteral("y")) },
                original, &edits, &error),
            "overlapping edits were accepted");
    Require(!PluginApi::ParseTextEdits(QJsonArray { Edit(-1, 0, QString()) }, original, &edits, &error),
            "negative edit range was accepted");
    QJsonObject fractional = Edit(0, 1, QStringLiteral("x"));
    fractional.insert(QStringLiteral("start"), 0.5);
    Require(!PluginApi::ParseTextEdits(QJsonArray { fractional }, original, &edits, &error),
            "fractional edit position was accepted");
    Require(!PluginApi::ParseTextEdits(QJsonArray(), original, &edits, &error),
            "empty edit request was accepted");
    return EXIT_SUCCESS;
}
