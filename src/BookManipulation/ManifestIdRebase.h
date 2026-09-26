#pragma once

#include <QString>

namespace ManifestIdRebase {

// Full rewrite matching the legacy fix_opf_ids.rebase_manifest_ids result.
QString Legacy(const QString &opf);

// Same id rules, but only the id and its references change in the original text.
QString Preserving(const QString &opf);

}
