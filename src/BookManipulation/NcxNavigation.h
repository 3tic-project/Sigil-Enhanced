#pragma once
#ifndef NCX_NAVIGATION_H
#define NCX_NAVIGATION_H

#include <QList>
#include <QString>

struct NcxNavPoint {
    int level = 1;
    QString label;
    QString src;
};

struct NcxPageTarget {
    QString value;
    QString src;
};

class NcxNavigation
{
public:
    QString doctitle;
    QList<NcxNavPoint> toc;
    QList<NcxPageTarget> pages;

    static NcxNavigation parse(const QString &ncx_source);

    // Convert NCX content@src (relative to the NCX folder) to an epub-root book path.
    static QString srcToBookPath(const QString &src, const QString &ncx_folder);

    // Convert a book path (optional #fragment) to an href relative to the nav file.
    static QString bookPathToNavHref(const QString &bookpath, const QString &nav_bookpath);
};

#endif
