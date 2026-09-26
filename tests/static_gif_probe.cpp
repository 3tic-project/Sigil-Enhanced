#include <QCoreApplication>
#include <QFile>
#include <QImage>

#include <cstdio>

#include "Misc/StaticGif.h"

// Encodes a PNG with the application's static GIF writer.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    const QImage image(QString::fromLocal8Bit(argv[1]));
    const QByteArray gif = StaticGif::Encode(image);
    if (gif.isEmpty()) {
        std::fputs("GIF encode failed\n", stderr);
        return 1;
    }
    QFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly) || output.write(gif) != gif.size()) return 1;
    return 0;
}
