#include <cstdlib>
#include <iostream>

#include <QByteArray>

#include "Exporters/ExportMetadataPolicy.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

}

int main()
{
    qunsetenv("SIGIL_ENABLE_VERSION_META");
    qunsetenv("SIGIL_DISABLE_VERSION_META");

    require(!ExportMetadataPolicy::shouldWriteSigilVersion(false),
            "an unchanged publication must never gain version metadata");
    require(!ExportMetadataPolicy::shouldWriteSigilVersion(true),
            "version metadata must be disabled by default");
    require(!ExportMetadataPolicy::shouldUpdateModificationDate(false),
            "an unchanged publication must keep its modification date");
    require(ExportMetadataPolicy::shouldUpdateModificationDate(true),
            "an edited publication must update its modification date");

    qputenv("SIGIL_ENABLE_VERSION_META", QByteArrayLiteral("1"));
    require(ExportMetadataPolicy::shouldWriteSigilVersion(true),
            "explicit opt-in must enable version metadata for edited books");

    qputenv("SIGIL_DISABLE_VERSION_META", QByteArrayLiteral("1"));
    require(!ExportMetadataPolicy::shouldWriteSigilVersion(true),
            "the legacy disable override must win over opt-in");

    qunsetenv("SIGIL_ENABLE_VERSION_META");
    qunsetenv("SIGIL_DISABLE_VERSION_META");
    return 0;
}
