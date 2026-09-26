#pragma once

#include <QString>

namespace OPFSourceText {

// Restore the source's individual line endings after edits in Qt's LF-only
// text document. This mirrors opf_source_bytes.restore_source_text.
QString Restore(const QString &original, const QString &edited);

}
