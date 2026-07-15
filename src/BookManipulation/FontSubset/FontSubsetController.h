#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "BookManipulation/FontSubset/FontSubsetTypes.h"
#include "BookManipulation/FontSubset/GlobalFontUsageCollector.h"

class Book;

namespace FontSubset
{

struct FontSnapshot {
    QString identifier;
    QString relativePath;
    QString fullPath;
    QString mediaType;
    QString obfuscationAlgorithm;
    QByteArray bytes;
};

struct BookSnapshot {
    QList<UsageSource> textSources;
    QList<FontSnapshot> fonts;
    QStringList warnings;
};

struct FontAnalysis {
    FontSnapshot font;
    Result result;
};

struct BatchResult {
    GlobalFontUsage usage;
    QList<FontAnalysis> fonts;
    QStringList warnings;
};

struct CommitResult {
    bool success = false;
    int fontCount = 0;
    qsizetype oldSize = 0;
    qsizetype newSize = 0;
    QString error;
};

class FontSubsetController
{
public:
    static BookSnapshot CreateSnapshot(Book* book);
    static BatchResult Analyze(const BookSnapshot& snapshot,
                               const Options& options = Options());
    static CommitResult Commit(Book* book,
                               const BatchResult& batch,
                               const QSet<QString>& selectedIdentifiers);
};

}
