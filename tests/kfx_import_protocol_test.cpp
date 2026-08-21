#include <cstdlib>
#include <iostream>

#include "BuiltinPlugins/KfxImportProtocol.h"

using BuiltinPlugins::KfxImportProtocol;
using BuiltinPlugins::KfxWorkerEvent;

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
    require(KfxImportProtocol::isKfxPath(QStringLiteral("Book.KFX")),
            "standalone KFX extension must be recognized case-insensitively");
    require(KfxImportProtocol::isKfxPath(QStringLiteral("Book.KfX-ZiP")),
            "KFX-ZIP extension must be recognized case-insensitively");
    require(!KfxImportProtocol::isKfxPath(QStringLiteral("Book.epub")),
            "EPUB must not be classified as KFX");
    require(KfxImportProtocol::suggestedEpubName(QStringLiteral("/tmp/Book.kfx-zip"))
                == QStringLiteral("Book.epub"),
            "KFX-ZIP suffix must be removed from the suggested output name");

    KfxWorkerEvent event;
    QString error;
    require(KfxImportProtocol::parseLine(
                QByteArrayLiteral("{\"protocol\":1,\"event\":\"progress\",\"phase\":\"convert\",\"current\":42,\"total\":100}"),
                &event, &error),
            "valid progress event must parse");
    require(event.type == KfxWorkerEvent::Progress && event.current == 42 && event.total == 100,
            "progress fields must be preserved");

    require(KfxImportProtocol::parseLine(
                QByteArrayLiteral("{\"protocol\":1,\"event\":\"result\",\"status\":\"success\",\"output\":\"/tmp/out.epub\",\"summary\":{\"spineItems\":3}}"),
                &event, &error),
            "valid success event must parse");
    require(event.type == KfxWorkerEvent::Success
                && event.summary.value(QStringLiteral("spineItems")).toInt() == 3,
            "success summary must be preserved");

    require(!KfxImportProtocol::parseLine(
                QByteArrayLiteral("{\"protocol\":2,\"event\":\"started\"}"),
                &event, &error),
            "unsupported protocol versions must be rejected");
    require(!error.isEmpty(), "protocol rejection must include a diagnostic");
    return 0;
}
