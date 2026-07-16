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

    const QByteArray original_binary = QByteArray::fromHex("00112233");
    Require(transaction.ReadBinary(QStringLiteral("image"), original_binary, 3, &revision)
                == original_binary,
            "initial binary read changed host data");
    const QByteArray replacement_binary = QByteArray::fromHex("aabbcc");
    Require(transaction.ReplaceBinary(QStringLiteral("image"), original_binary, 3, 3,
                                      replacement_binary, &error),
            "binary replacement was not staged");
    Require(transaction.ReadBinary(QStringLiteral("image"), QByteArray("host"), 4, &revision)
                == replacement_binary,
            "transaction does not read its own binary write");
    Require(revision == 3, "staged binary read did not preserve base revision");
    Require(transaction.BinaryChanges().size() == 1, "binary staging count is wrong");

    Require(transaction.ReplacePackage(QStringLiteral("opf"), QStringLiteral("<package/>"),
                                       6, 6, QStringLiteral("<package version=\"3.0\"/>"),
                                       &error),
            "package replacement was not staged");
    Require(transaction.HasPackageChange(), "package staging flag is wrong");
    Require(transaction.PackageChange().baseRevision == 6,
            "package base revision was not preserved");
    Require(!transaction.ReplacePackage(QStringLiteral("opf"), QStringLiteral("<package/>"),
                                        6, 7, QStringLiteral("bad"), &error),
            "package replacement accepted a different revision");

    Require(transaction.ReplaceArchiveFile(QStringLiteral("META-INF/metadata.xml"),
                                           QByteArray("old"), QStringLiteral("hash"),
                                           QStringLiteral("hash"), QByteArray("new"), &error),
            "archive replacement was not staged");
    Require(transaction.RemoveArchiveFile(QStringLiteral("META-INF/metadata.xml"),
                                          QByteArray("old"), QStringLiteral("changed"),
                                          QStringLiteral("hash"), &error),
            "archive removal did not preserve its base fingerprint");
    Require(transaction.HasArchiveChange(QStringLiteral("META-INF/metadata.xml")),
            "archive staging flag is wrong");
    Require(transaction.ArchiveChanges().first().remove,
            "archive removal flag is wrong");

    PluginApi::StagedResourceAddition addition;
    addition.stagingId = QStringLiteral("new:1");
    addition.bookPath = QStringLiteral("OEBPS/Text/new.xhtml");
    addition.mediaType = QStringLiteral("application/xhtml+xml");
    addition.manifestId = QStringLiteral("new_chapter");
    addition.data = QByteArray("<html/>");
    addition.isText = true;
    Require(transaction.AddResource(addition, &error), "resource addition was not staged");
    Require(!transaction.AddResource(addition, &error), "duplicate resource addition was accepted");
    QString added_text;
    Require(transaction.ReadAddedText(QStringLiteral("new:1"), &added_text, &revision),
            "staged text addition could not be read");
    Require(added_text == QStringLiteral("<html/>") && revision == 0,
            "staged text addition returned wrong content or revision");
    Require(transaction.ApplyAddedTextEdits(
                QStringLiteral("new:1"), 0,
                QJsonArray { Edit(6, 6, QStringLiteral("body")) }, &error),
            "staged text addition could not be patched");
    Require(transaction.ReadAddedText(QStringLiteral("new:1"), &added_text, &revision)
                && added_text == QStringLiteral("<html/body>") && revision == 1,
            "staged text patch was not retained");
    Require(transaction.RemoveResource(QStringLiteral("old"), 9, &error),
            "resource removal was not staged");
    Require(transaction.RelocateResource(QStringLiteral("css"),
                                         QStringLiteral("OEBPS/Styles/a.css"),
                                         QStringLiteral("OEBPS/Styles/b.css"), 5, &error),
            "resource relocation was not staged");
    Require(transaction.Additions().size() == 1 && transaction.Removals().size() == 1
                && transaction.Relocations().size() == 1,
            "structure staging counts are wrong");

    transaction.Clear();
    Require(transaction.IsEmpty(), "rollback did not discard staged data");
    return EXIT_SUCCESS;
}
