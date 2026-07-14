#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonObject>

#include "PluginAPI/PluginTextTransaction.h"

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
    PluginApi::TextTransaction transaction(QStringLiteral("tx-1"),
                                           QStringLiteral("Test transaction"),
                                           QStringLiteral("auto"), 20);
    quint64 revision = 0;
    Require(transaction.ReadText(QStringLiteral("chapter"), QStringLiteral("abc"), 7, &revision)
                == QStringLiteral("abc"),
            "initial transaction read changed host text");
    Require(revision == 7, "initial transaction read returned wrong revision");

    QString error;
    Require(transaction.ApplyEdits(QStringLiteral("chapter"), QStringLiteral("abc"), 7, 7,
                                   QJsonArray { Edit(1, 2, QStringLiteral("X")) }, &error),
            "first staged patch failed");
    Require(transaction.ReadText(QStringLiteral("chapter"), QStringLiteral("host changed"), 8,
                                 &revision) == QStringLiteral("aXc"),
            "transaction does not read its own staged write");
    Require(revision == 7, "staged read did not preserve base revision");

    Require(transaction.ApplyEdits(QStringLiteral("chapter"), QStringLiteral("abc"), 7, 7,
                                   QJsonArray { Edit(3, 3, QStringLiteral("!")) }, &error),
            "second staged patch failed");
    const QList<PluginApi::StagedTextChange> changes = transaction.Changes();
    Require(changes.size() == 1, "multiple patches created multiple resource writes");
    Require(changes.first().originalText == QStringLiteral("abc"), "original text was not preserved");
    Require(changes.first().stagedText == QStringLiteral("aXc!"), "staged patches were not composed");
    Require(!transaction.ReplaceText(QStringLiteral("chapter"), QStringLiteral("abc"), 7, 8,
                                     QStringLiteral("bad"), &error),
            "staged write accepted a different revision");

    transaction.Clear();
    Require(transaction.IsEmpty(), "rollback did not discard staged data");
    return EXIT_SUCCESS;
}
