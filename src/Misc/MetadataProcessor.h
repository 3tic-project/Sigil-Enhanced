#pragma once

#include <QString>
#include <QStringList>

// Native port of the former python3lib metaproc2.py (EPUB 2), metaproc3.py (EPUB 3) and
// metadata_utils.py OPFMetadataParser (now tests/fixtures/*_legacy.py) for the metadata editor.
namespace MetadataProcessor {

struct Pieces {
    QString data;
    QString otherxml;
    QStringList idlist;
    QString metatag;
};

// process_metadata(opfdata) and its getters. Returns false, leaving *out
// empty, where the legacy function returned None (it swallowed an exception)
// and for a '<' without a later '>', on which the legacy scanner never ended.
bool Extract(const QString &opfdata, const QString &version, Pieces *out);

// set_new_metadata(...). Returns false where the legacy function raised, so
// the caller keeps the original OPF text as before.
bool Apply(const Pieces &pieces, const QString &opfdata, const QString &version, QString *out);

}
