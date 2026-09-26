#pragma once

#include <QString>

namespace NcxGenerator {

// Generate an EPUB 2 NCX from the current EPUB 3 nav source. The output
// preserves the legacy generator's ordering, indentation and raw text values.
QString Generate(const QString &navSource, const QString &navBookPath,
                 const QString &ncxDirectory, const QString &documentTitle,
                 const QString &mainIdentifier);

}
