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

    require(KfxImportProtocol::parseLine(
                QByteArrayLiteral("Missing font family names: JA"),
                &event, &error),
            "converter log lines must not be treated as protocol failures");
    require(event.type == KfxWorkerEvent::Ignored
                && event.message.contains(QStringLiteral("Missing font family names")),
            "stray converter logs must be preserved as ignored diagnostics");

    QByteArray dirty_json = QByteArrayLiteral(
        "{\"protocol\":1,\"event\":\"warning\",\"code\":\"KFX-W-CONVERTER\",\"message\":\"JA\"}");
    dirty_json[dirty_json.lastIndexOf('J')] = static_cast<char>('\xff');
    require(KfxImportProtocol::parseLine(dirty_json, &event, &error),
            "invalid UTF-8 inside a JSON event must be replaced and parsed");
    require(event.type == KfxWorkerEvent::Warning, "sanitized JSON warning must parse");
    return 0;
}
