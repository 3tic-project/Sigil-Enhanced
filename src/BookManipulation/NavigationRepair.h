#pragma once

#include <QCoreApplication>
#include <QSharedPointer>
#include <QString>

class Book;

// Planning is read-only. Applying revalidates the entire displayed plan against
// the current book before creating a file or changing the package.
class NavigationRepair
{
    Q_DECLARE_TR_FUNCTIONS(NavigationRepair)
public:
    struct Plan {
        QString navBookPath;
        QString navText;
        QString opfBefore;
        QString opfAfter;
        QByteArray baseState;
        bool fromNCX = false;
    };
    static bool Prepare(const QSharedPointer<Book> &book, Plan &plan, QString &error);
    static bool Apply(const QSharedPointer<Book> &book, const Plan &plan, QString &error);
};
