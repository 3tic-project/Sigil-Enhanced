#include <QCoreApplication>
#include <QImage>
#include <QImageReader>

#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        return 2;
    }
    QImageReader reader(QString::fromLocal8Bit(argv[1]), "GIF");
    const QImage image = reader.read();
    if (image.isNull()) {
        std::fprintf(stderr, "GIF read failed: %s\n", reader.errorString().toUtf8().constData());
        return 1;
    }
    std::printf("%d %d %d", image.width(), image.height(), image.hasAlphaChannel() ? 1 : 0);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            std::printf(" %08X", image.pixel(x, y));
        }
    }
    std::puts("");
    return 0;
}
