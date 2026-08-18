/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "Misc/WebpSupport.h"

#include <QFile>
#include <QImageReader>
#include <QCoreApplication>
#include <QIODevice>
#include <QStringList>

namespace
{

quint32 readU32(const char *bytes)
{
    const auto *data = reinterpret_cast<const unsigned char *>(bytes);
    return static_cast<quint32>(data[0]) |
           (static_cast<quint32>(data[1]) << 8) |
           (static_cast<quint32>(data[2]) << 16) |
           (static_cast<quint32>(data[3]) << 24);
}

void writeU32(QByteArray &out, quint32 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
    out.append(static_cast<char>((value >> 16) & 0xff));
    out.append(static_cast<char>((value >> 24) & 0xff));
}

bool keepWebpChunk(const QByteArray &fourcc)
{
    static const char *const kKept[] = {
        "VP8X", "VP8 ", "VP8L", "ALPH", "ANIM", "ANMF", "ICCP", "EXIF", "XMP "
    };
    for (const char *name : kKept) {
        if (fourcc == QByteArray::fromRawData(name, 4)) {
            return true;
        }
    }
    return false;
}

QString supportedFormats()
{
    QStringList names;
    const auto formats = QImageReader::supportedImageFormats();
    for (const QByteArray &format : formats) {
        names.append(QString::fromLatin1(format));
    }
    names.sort();
    return names.join(QStringLiteral(", "));
}

}

bool IsWebpPayload(const QByteArray &data)
{
    return data.size() >= 12 &&
           data.startsWith("RIFF") &&
           data.mid(8, 4) == "WEBP";
}

QByteArray SanitizeWebpPayload(const QByteArray &data)
{
    if (!IsWebpPayload(data)) {
        return data;
    }

    QByteArray body;
    int pos = 12;
    bool dropped = false;
    while (pos + 8 <= data.size()) {
        const QByteArray fourcc = data.mid(pos, 4);
        const quint32 size = readU32(data.constData() + pos + 4);
        const int padded = static_cast<int>(size) + (size & 1);
        if (pos + 8 + padded > data.size()) {
            break;
        }
        if (keepWebpChunk(fourcc)) {
            body.append(data.mid(pos, 8 + padded));
        } else {
            dropped = true;
        }
        pos += 8 + padded;
    }
    if (!dropped || body.isEmpty()) {
        return data;
    }

    QByteArray out("RIFF");
    writeU32(out, static_cast<quint32>(4 + body.size()));
    out.append("WEBP");
    out.append(body);
    return out;
}

QImage LoadRasterImage(const QString &path, QString *error_out)
{
    QImage image(path);
    if (!image.isNull()) {
        return image;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_out) {
            *error_out = QCoreApplication::translate("AdjustImage", "Cannot open %1.").arg(path);
        }
        return QImage();
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QImageReader reader(path);
    image = reader.read();
    QString reader_error = reader.errorString();

    if (image.isNull() && IsWebpPayload(bytes)) {
        const QByteArray cleaned = SanitizeWebpPayload(bytes);
        image = QImage::fromData(cleaned, "WEBP");
        if (image.isNull()) {
            image = QImage::fromData(bytes, "WEBP");
        }
    }

    if (image.isNull() && error_out) {
        const bool webp_supported =
            QImageReader::supportedImageFormats().contains("webp");
        QString detail = reader_error;
        if (!webp_supported &&
            (path.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive) ||
             IsWebpPayload(bytes))) {
            detail = QCoreApplication::translate(
                "AdjustImage", "The Qt WebP plugin is not available.");
        }
        if (detail.isEmpty()) {
            detail = QCoreApplication::translate(
                "AdjustImage", "Unsupported or damaged image data.");
        }
        *error_out = QCoreApplication::translate(
                         "AdjustImage",
                         "Cannot load %1 (%2). Supported formats: %3.")
                         .arg(path, detail, supportedFormats());
    }
    return image;
}
