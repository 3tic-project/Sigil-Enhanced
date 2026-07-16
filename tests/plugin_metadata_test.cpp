#include <cstdlib>
#include <iostream>

#include "Misc/Plugin.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main()
{
    Plugin legacy;
    legacy.set_name(QStringLiteral("Legacy"));
    legacy.set_type(QStringLiteral("edit"));
    legacy.set_engine(QStringLiteral("python3.4"));
    Require(legacy.isvalid(), "legacy metadata is invalid");
    Require(legacy.get_api_version() == 1, "legacy API default differs");
    Require(legacy.get_declared_runtime() == Plugin::LegacyRuntime, "legacy runtime default differs");
    Require(legacy.get_lifetime() == QStringLiteral("command"), "lifetime default differs");

    Plugin live;
    live.set_name(QStringLiteral("Live API Showcase"));
    live.set_dirname(QStringLiteral("LiveApiShowcase"));
    live.set_type(QStringLiteral("edit"));
    live.set_engine(QStringLiteral("python3"));
    live.set_api(2, QStringLiteral(" LIVE "));
    live.set_lifetime(QStringLiteral("book-session"));
    live.add_permission(QStringLiteral("editor.read"));
    live.add_permission(QStringLiteral("editor.read"));
    live.add_event(QStringLiteral("editor.contentChanged"));
    Require(live.isvalid(), "python3 live metadata is invalid");
    Require(live.get_declared_runtime() == Plugin::LiveRuntime, "live declaration was ignored");
    Require(live.get_dirname() == QStringLiteral("LiveApiShowcase"),
            "install directory name was ignored");
    Require(live.get_permissions() == QStringList { QStringLiteral("editor.read") },
            "permission de-duplication failed");

    Plugin roundtrip(live.serialize());
    Require(roundtrip.get_api_version() == 2, "serialized API version differs");
    Require(roundtrip.get_api_interface() == QStringLiteral("live"), "serialized interface differs");
    Require(roundtrip.get_lifetime() == QStringLiteral("book-session"), "serialized lifetime differs");
    Require(roundtrip.get_dirname() == QStringLiteral("LiveApiShowcase"),
            "serialized dirname differs");
    Require(roundtrip.get_permissions() == live.get_permissions(), "serialized permissions differ");
    Require(roundtrip.get_events() == live.get_events(), "serialized events differ");

    Plugin unnamed_dir;
    unnamed_dir.set_name(QStringLiteral("OnlyName"));
    Require(unnamed_dir.get_dirname() == QStringLiteral("OnlyName"),
            "dirname should fall back to name");

    live.set_api(3, QStringLiteral("live"));
    Require(live.get_declared_runtime() == Plugin::LegacyRuntime,
            "unsupported API major selected live runtime");
    return EXIT_SUCCESS;
}
