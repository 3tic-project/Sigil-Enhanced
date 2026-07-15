#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QTemporaryDir>

#include "BookManipulation/FontSubset/FontSubsetTransaction.h"
#include "BookManipulation/FontSubset/GlobalFontUsageCollector.h"

namespace
{

using namespace FontSubset;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

QByteArray Read(const QString& path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "could not read test file");
    return file.readAll();
}

void Write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    Require(file.open(QIODevice::WriteOnly), "could not write test file");
    Require(file.write(data) == data.size(), "short test file write");
}

void TestUsageCollection()
{
    const QList<UsageSource> sources = {
        {QStringLiteral("Text/chapter.xhtml"),
         QStringLiteral("application/xhtml+xml"),
         QStringLiteral("<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
                        "<head><style>.content::before { content: \"\\4E2D 文\"; }</style>"
                        "<script>ignored-script</script></head><body>Latin 中文 😀"
                        "<img alt=\"代替\" title=\"Title\"/></body></html>")},
        {QStringLiteral("Styles/main.css"), QStringLiteral("text/css"),
         QStringLiteral("p::after { content: '終' counter(chapter); } "
                        "a::before { content: attr(title); }")},
        {QStringLiteral("toc.ncx"), QStringLiteral("application/x-dtbncx+xml"),
         QStringLiteral("<?xml version=\"1.0\"?><ncx><text>目次 "
                        "&copy; &euro; End</text></ncx>")}
    };
    const GlobalFontUsage usage = GlobalFontUsageCollector().Collect(sources);
    for (quint32 codepoint : {quint32('L'), quint32(0x4e2d), quint32(0x6587),
                              quint32(0x1f600), quint32(0x4ee3), quint32(0x7d42),
                              quint32(0x76ee), quint32(0x6b21), quint32(0x00a9),
                              quint32(0x20ac), quint32('E'), quint32(0x3000)}) {
        Require(usage.codepoints.contains(codepoint),
                "usage collector omitted visible content");
    }
    Require(!usage.codepoints.contains(quint32('g')),
            "usage collector included script-only text");
    Require(!usage.shapingSamples.isEmpty(), "usage collector produced no samples");
    Require(usage.warnings.size() >= 2,
            "dynamic CSS content did not produce complete warnings");
}

void TestTransactionCommitAndConflict()
{
    QTemporaryDir directory;
    Require(directory.isValid(), "could not create transaction directory");
    const QString first = directory.filePath(QStringLiteral("first.ttf"));
    const QString second = directory.filePath(QStringLiteral("second.otf"));
    Write(first, "font-one");
    Write(second, "font-two");

    FontSubsetTransaction transaction;
    QString error;
    Require(transaction.Stage(first, "font-one", "subset-one", &error),
            "could not stage first font");
    Require(transaction.Stage(second, "font-two", "subset-two", &error),
            "could not stage second font");
    Require(transaction.Commit(&error), error.toUtf8().constData());
    Require(Read(first) == "subset-one" && Read(second) == "subset-two",
            "transaction did not commit all replacements");
    Require(transaction.GetState() == FontSubsetTransaction::State::Committed,
            "transaction did not enter committed state");

    FontSubsetTransaction conflict;
    Require(conflict.Stage(first, "subset-one", "new-one", &error),
            "could not stage conflict fixture");
    Write(first, "external-change");
    Require(!conflict.Commit(&error) && Read(first) == "external-change",
            "changed source was overwritten");
}

void TestTransactionRollback()
{
    QTemporaryDir directory;
    Require(directory.isValid(), "could not create rollback directory");
    const QString first = directory.filePath(QStringLiteral("first.ttf"));
    const QString second = directory.filePath(QStringLiteral("second.otf"));
    Write(first, "original-one");
    Write(second, "original-two");

    FontSubsetTransaction transaction;
    QString error;
    Require(transaction.Stage(first, "original-one", "subset-one", &error),
            "could not stage rollback first font");
    Require(transaction.Stage(second, "original-two", "subset-two", &error),
            "could not stage rollback second font");
    transaction.SetFailureAfterWritesForTesting(1);
    Require(!transaction.Commit(&error), "injected transaction failure was ignored");
    Require(Read(first) == "original-one" && Read(second) == "original-two",
            "transaction failure left a partial replacement");
    Require(transaction.GetState() == FontSubsetTransaction::State::RolledBack,
            "transaction did not report successful rollback");
    Require(!transaction.Stage(first, "original-one", "another-one", &error),
            "rolled-back transaction accepted new work without Clear");
    transaction.Clear();
    Require(!transaction.Stage(first, "original-one", "original-one", &error),
            "transaction accepted an unchanged replacement");
}

}

int main()
{
    TestUsageCollection();
    TestTransactionCommitAndConflict();
    TestTransactionRollback();
    return EXIT_SUCCESS;
}
