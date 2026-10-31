#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>

namespace BuiltinPlugins
{

class PlainTextExporter
{
    Q_DECLARE_TR_FUNCTIONS(PlainTextExporter)

public:
    struct Document {
        QString bookPath;
        QString xhtml;
    };

    struct Result {
        QString text;
        QString error;
        bool ok() const { return error.isEmpty(); }
    };

    // Documents are immutable snapshots, already in publication reading order.
    static Result render(const QList<Document>& documents);
    static bool exportToFile(const QList<Document>& documents, const QString& destination,
                             QString* error = nullptr);
};

}
