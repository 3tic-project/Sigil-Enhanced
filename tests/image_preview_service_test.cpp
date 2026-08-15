#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "Misc/ImagePreviewService.h"

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

bool writeBitmap(const QString& path, const QColor& color, const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path, "PNG");
}

bool writeSvg(const QString& path, const QSize& declaredSize)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QByteArray svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" "
        "viewBox=\"0 0 %1 %2\"><rect width=\"%1\" height=\"%2\" fill=\"#f8fafc\"/>")
        .arg(declaredSize.width())
        .arg(declaredSize.height())
        .toUtf8();
    for (int i = 0; i < 8000; ++i) {
        svg += QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"18\" fill=\"#2f855a\"/>")
                   .arg((i * 97) % declaredSize.width())
                   .arg((i * 53) % declaredSize.height())
                   .toUtf8();
    }
    svg += "</svg>";
    return file.write(svg) == svg.size();
}

bool waitForPreview(ImagePreviewService& service,
                    const QString& path,
                    ImagePreviewService::Format format,
                    ImagePreviewData* preview,
                    qint64* requestElapsedNs = nullptr)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(5000);
    bool received = false;
    bool callbackOnOwnerThread = false;
    quint64 expectedId = 0;
    const auto connection = QObject::connect(
        &service, &ImagePreviewService::previewReady, &loop,
        [&](quint64 requestId, const ImagePreviewData& result) {
            if (requestId != expectedId) {
                return;
            }
            received = true;
            callbackOnOwnerThread = QThread::currentThread() == service.thread();
            *preview = result;
            loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QElapsedTimer requestTimer;
    requestTimer.start();
    expectedId = service.request(path, format);
    if (requestElapsedNs) {
        *requestElapsedNs = requestTimer.nsecsElapsed();
    }
    loop.exec();
    QObject::disconnect(connection);
    return received && callbackOnOwnerThread;
}

bool testScaledBitmapAndCache(const QString& root)
{
    const QString path = root + "/large.png";
    if (!expect(writeBitmap(path, QColor("#2563eb"), QSize(1200, 800)),
                "could not create bitmap fixture")) {
        return false;
    }

    ImagePreviewService service;
    ImagePreviewData preview;
    if (!expect(waitForPreview(service, path, ImagePreviewService::Format::Bitmap,
                               &preview),
                "bitmap preview timed out or used wrong callback thread")) {
        return false;
    }
    if (!expect(preview.pixelSize == QSize(1200, 800),
                "bitmap source dimensions changed") ||
        !expect(preview.image.width() <= 300 && preview.image.height() <= 300,
                "bitmap preview was not scaled during decode") ||
        !expect(service.cacheEntryCount() == 1, "bitmap preview was not cached")) {
        return false;
    }

    ImagePreviewData cached;
    const quint64 hitsBefore = service.cacheHits();
    return expect(waitForPreview(service, path, ImagePreviewService::Format::Bitmap,
                                 &cached),
                  "cached bitmap preview was not delivered") &&
           expect(service.cacheHits() == hitsBefore + 1, "cache hit was not recorded") &&
           expect(cached.image == preview.image, "cached preview content changed");
}

bool testNaturalBitmapSizeAndConfiguredLimit(const QString& root)
{
    const QString smallPath = root + "/small.png";
    if (!expect(writeBitmap(smallPath, QColor("#9333ea"), QSize(48, 32)),
                "could not create small bitmap fixture")) {
        return false;
    }

    ImagePreviewService service(nullptr, 4LL * 1024 * 1024, 150);
    ImagePreviewData smallPreview;
    if (!expect(waitForPreview(service, smallPath, ImagePreviewService::Format::Bitmap,
                               &smallPreview),
                "small bitmap preview timed out") ||
        !expect(smallPreview.image.size() == QSize(48, 32),
                "small bitmap was enlarged")) {
        return false;
    }

    const QString largePath = root + "/configured-limit.png";
    if (!expect(writeBitmap(largePath, QColor("#0f766e"), QSize(1200, 800)),
                "could not create configured-size fixture")) {
        return false;
    }
    ImagePreviewData limitedPreview;
    if (!expect(waitForPreview(service, largePath, ImagePreviewService::Format::Bitmap,
                               &limitedPreview),
                "configured-size preview timed out") ||
        !expect(limitedPreview.image.size() == QSize(150, 100),
                "configured preview limit was not applied")) {
        return false;
    }

    service.setMaximumPreviewSide(500);
    if (!expect(service.maximumPreviewSide() == 500,
                "configured preview limit was not stored") ||
        !expect(service.cacheEntryCount() == 0,
                "changing preview size did not invalidate cached variants")) {
        return false;
    }
    ImagePreviewData enlargedLimitPreview;
    return expect(waitForPreview(service, largePath, ImagePreviewService::Format::Bitmap,
                                 &enlargedLimitPreview),
                  "updated-size preview timed out") &&
           expect(enlargedLimitPreview.image.size() == QSize(500, 333),
                  "updated preview limit was not applied");
}

bool testLargeSvgRequestAndCancellation(const QString& root)
{
    const QString path = root + "/large.svg";
    if (!expect(writeSvg(path, QSize(20000, 20000)),
                "could not create SVG fixture")) {
        return false;
    }

    ImagePreviewService service;
    ImagePreviewData preview;
    QElapsedTimer synchronousTimer;
    synchronousTimer.start();
    const ImagePreviewData synchronous =
        ImagePreviewService::decode(path, ImagePreviewService::Format::Svg);
    const qint64 synchronousElapsedNs = synchronousTimer.nsecsElapsed();
    qint64 requestElapsedNs = 0;
    if (!expect(waitForPreview(service, path, ImagePreviewService::Format::Svg,
                               &preview, &requestElapsedNs),
                "SVG preview timed out or used wrong callback thread")) {
        return false;
    }
    if (!expect(requestElapsedNs < 16LL * 1000 * 1000,
                "20k SVG request blocked the caller for 16ms or more") ||
        !expect(preview.pixelSize == QSize(20000, 20000),
                "SVG declared dimensions changed") ||
        !expect(preview.image.width() <= 300 && preview.image.height() <= 300,
                "SVG preview exceeded its pixel budget")) {
        return false;
    }
    if (!expect(!synchronous.image.isNull(), "synchronous benchmark decode failed")) {
        return false;
    }
    fprintf(stdout, "IMAGE_PREVIEW_BENCH sync_decode_ms=%.3f request_ms=%.3f\n",
            synchronousElapsedNs / 1000000.0, requestElapsedNs / 1000000.0);

    bool cancelledResultDelivered = false;
    QObject::connect(&service, &ImagePreviewService::previewReady, &service,
                     [&](quint64, const ImagePreviewData&) {
                         cancelledResultDelivered = true;
                     });
    const QString cancelPath = root + "/cancel.svg";
    if (!expect(writeSvg(cancelPath, QSize(20000, 20000)),
                "could not create cancellation fixture")) {
        return false;
    }
    service.request(cancelPath, ImagePreviewService::Format::Svg);
    service.cancelPending();
    QEventLoop loop;
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();
    return expect(!cancelledResultDelivered, "cancelled preview result was delivered");
}

bool testBoundedLru(const QString& root)
{
    constexpr qint64 cacheLimit = 700LL * 1024;
    ImagePreviewService service(nullptr, cacheLimit);
    const QList<QColor> colors = {
        QColor("#dc2626"), QColor("#16a34a"), QColor("#2563eb"), QColor("#ca8a04")
    };
    for (int i = 0; i < colors.size(); ++i) {
        const QString path = QStringLiteral("%1/cache-%2.png").arg(root).arg(i);
        if (!expect(writeBitmap(path, colors.at(i), QSize(900, 900)),
                    "could not create LRU fixture")) {
            return false;
        }
        ImagePreviewData preview;
        if (!expect(waitForPreview(service, path, ImagePreviewService::Format::Bitmap,
                                   &preview),
                    "LRU preview timed out")) {
            return false;
        }
    }
    if (!expect(service.cacheBytes() <= service.cacheLimitBytes(),
                "LRU cache exceeded its byte budget") ||
        !expect(service.cacheEntryCount() < colors.size(),
                "LRU cache did not evict old previews")) {
        return false;
    }
    service.reset();
    return expect(service.cacheEntryCount() == 0,
                  "reset did not release cached previews") &&
           expect(service.cacheBytes() == 0,
                  "reset did not release cached preview bytes");
}

bool testCoalescedDecodeQueue(const QString& root)
{
    const QString firstPath = root + "/queue-first.svg";
    const QString skippedPath = root + "/queue-skipped.svg";
    const QString latestPath = root + "/queue-latest.svg";
    if (!expect(writeSvg(firstPath, QSize(20000, 20000)),
                "could not create first queue fixture") ||
        !expect(writeSvg(skippedPath, QSize(20000, 20000)),
                "could not create skipped queue fixture") ||
        !expect(writeSvg(latestPath, QSize(20000, 20000)),
                "could not create latest queue fixture")) {
        return false;
    }

    ImagePreviewService service;
    QList<quint64> delivered;
    if (!expect(service.cacheLimitBytes() == 32LL * 1024 * 1024,
                "default cache limit is not 32 MiB")) {
        return false;
    }
    QObject::connect(&service, &ImagePreviewService::previewReady, &service,
                     [&](quint64 requestId, const ImagePreviewData&) {
                         delivered.append(requestId);
                     });

    service.request(firstPath, ImagePreviewService::Format::Svg);
    if (!expect(service.activeDecodeCount() == 1,
                "first decode did not start")) {
        return false;
    }
    service.cancelPending();
    service.request(skippedPath, ImagePreviewService::Format::Svg);
    if (!expect(service.activeDecodeCount() == 1,
                "cancelled decode was allowed to run concurrently") ||
        !expect(service.queuedRequestCount() == 1,
                "replacement decode was not queued")) {
        return false;
    }
    service.cancelPending();
    const quint64 latestId =
        service.request(latestPath, ImagePreviewService::Format::Svg);
    if (!expect(service.queuedRequestCount() == 1,
                "decode queue retained more than the latest request")) {
        return false;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(5000);
    QObject::connect(&service, &ImagePreviewService::previewReady, &loop,
                     [&](quint64 requestId, const ImagePreviewData&) {
                         if (requestId == latestId) {
                             loop.quit();
                         }
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    return expect(delivered == QList<quint64>{latestId},
                  "a stale queued preview was delivered") &&
           expect(service.activeDecodeCount() == 0,
                  "decode remained active after the latest result") &&
           expect(service.queuedRequestCount() == 0,
                  "decode queue was not drained") &&
           expect(service.cacheEntryCount() == 1,
                  "cancelled previews remained in the cache");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir workspace;
    const bool ok = expect(workspace.isValid(), "temporary directory unavailable") &&
                    testScaledBitmapAndCache(workspace.path()) &&
                    testNaturalBitmapSizeAndConfiguredLimit(workspace.path()) &&
                    testLargeSvgRequestAndCancellation(workspace.path()) &&
                    testBoundedLru(workspace.path()) &&
                    testCoalescedDecodeQueue(workspace.path());
    if (ok) {
        fprintf(stdout, "All image preview service tests passed.\n");
    }
    return ok ? 0 : 1;
}
