#pragma once

#include <QString>

namespace XmlWellFormed {

struct Result {
    int line = -1;
    int column = -1;
    QString message = QStringLiteral("well-formed");
};

// Matches xmlprocessor.WellFormedXMLErrorCheck, including its first-error text.
Result Check(const QString &source);

}
