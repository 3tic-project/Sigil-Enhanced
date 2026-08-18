#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include "Misc/WebpSupport.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QByteArray FourCC(const char *name)
{
    return QByteArray::fromRawData(name, 4);
}

void AppendU32(QByteArray &out, quint32 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
    out.append(static_cast<char>((value >> 16) & 0xff));
    out.append(static_cast<char>((value >> 24) & 0xff));
}

QByteArray Chunk(const char *name, const QByteArray &payload)
{
    QByteArray chunk = FourCC(name);
    AppendU32(chunk, static_cast<quint32>(payload.size()));
    chunk.append(payload);
    if (payload.size() & 1) {
        chunk.append('\0');
    }
    return chunk;
}

}

int main()
{
    QByteArray vp8x(10, '\0');
    vp8x[0] = 0x10; // alpha
    QByteArray vp8("VP8DATA");
    QByteArray alph("ALPHDATA");
    QByteArray psai("8BIM");

    QByteArray body;
    body += Chunk("VP8X", vp8x);
    body += Chunk("ALPH", alph);
    body += Chunk("VP8 ", vp8);
    body += Chunk("PSAI", psai);

    QByteArray webp("RIFF");
    AppendU32(webp, static_cast<quint32>(4 + body.size()));
    webp += FourCC("WEBP");
    webp += body;

    Require(IsWebpPayload(webp), "synthetic payload should be recognized as WebP");
    const QByteArray cleaned = SanitizeWebpPayload(webp);
    Require(IsWebpPayload(cleaned), "sanitized payload should remain WebP");
    Require(!cleaned.contains("PSAI"), "Photoshop PSAI chunk must be dropped");
    Require(cleaned.contains("VP8X") && cleaned.contains("ALPH") && cleaned.contains("VP8 "),
            "VP8X/ALPH/VP8 chunks must be kept");
    Require(SanitizeWebpPayload(cleaned) == cleaned, "a second sanitize is a no-op");

    const char *samples[] = {
        "/Users/parsle/Downloads/line1.webp",
        "/Users/parsle/Downloads/logo.webp"
    };
    for (const char *sample : samples) {
        const QString path = QString::fromUtf8(sample);
        if (!QFileInfo::exists(path)) {
            continue;
        }
        QFile file(path);
        Require(file.open(QIODevice::ReadOnly), "sample WebP must be readable");
        const QByteArray raw = file.readAll();
        file.close();
        Require(IsWebpPayload(raw), "sample must be a RIFF/WEBP container");

        const QByteArray cleaned = SanitizeWebpPayload(raw);
        Require(IsWebpPayload(cleaned), "sanitized sample must stay WebP");
        Require(!cleaned.contains("PSAI"),
                "Photoshop PSAI must be stripped from real samples");

        QString error;
        const QImage image = LoadRasterImage(path, &error);
        Require(!image.isNull(),
                "sample WebP with VP8X alpha must decode on this host");
        Require(error.isEmpty(), "successful load must not set an error string");

        const QImage from_cleaned = QImage::fromData(cleaned, "WEBP");
        Require(!from_cleaned.isNull(),
                "sample must still decode after unknown chunks are dropped");
        Require(from_cleaned.size() == image.size(),
                "sanitized decode must keep the original canvas size");
    }
    std::cout << "webp_sanitize: ok\n";
    return 0;
}
