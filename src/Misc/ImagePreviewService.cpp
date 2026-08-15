/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include <limits>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QtSvg/QSvgRenderer>

#include "Misc/ImagePreviewService.h"

namespace
{

constexpr qint64 MAX_SOURCE_FILE_BYTES = 128LL * 1024 * 1024;

int cacheLimitKiB(qint64 bytes)
{
    const qint64 limit = qMax<qint64>(1, qMax<qint64>(1024, bytes) / 1024);
    return static_cast<int>(qMin<qint64>(limit, std::numeric_limits<int>::max()));
}

int imageCostKiB(const QImage& image)
{
    return qMax(1, static_cast<int>((image.sizeInBytes() + 1023) / 1024));
}

QSize previewSize(const QSize& source, int maximumSide)
{
    if (source.isEmpty()) {
        return QSize();
    }
    if (source.width() <= maximumSide && source.height() <= maximumSide) {
        return source;
    }
    QSize scaled = source;
    scaled.scale(QSize(maximumSide, maximumSide), Qt::KeepAspectRatio);
    return scaled;
}

ImagePreviewData decodeBitmap(const QString& filePath, int maximumSide)
{
    ImagePreviewData preview;
    preview.fileSize = QFileInfo(filePath).size();
    if (preview.fileSize < 0 || preview.fileSize > MAX_SOURCE_FILE_BYTES) {
        return preview;
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    preview.pixelSize = reader.size();
    const QSize targetSize = previewSize(preview.pixelSize, maximumSide);
    if (targetSize.isEmpty()) {
        return preview;
    }
    reader.setScaledSize(targetSize);
    preview.image = reader.read();
    if (!preview.image.isNull() &&
        (preview.image.width() > maximumSide ||
         preview.image.height() > maximumSide)) {
        preview.image = preview.image.scaled(targetSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
    }
    return preview;
}

ImagePreviewData decodeSvg(const QString& filePath, int maximumSide)
{
    ImagePreviewData preview;
    preview.fileSize = QFileInfo(filePath).size();
    if (preview.fileSize < 0 || preview.fileSize > MAX_SOURCE_FILE_BYTES) {
        return preview;
    }

    QSvgRenderer renderer(filePath);
    if (!renderer.isValid()) {
        return preview;
    }
    preview.pixelSize = renderer.defaultSize();
    QSize targetSize = previewSize(preview.pixelSize, maximumSide);
    if (targetSize.isEmpty()) {
        targetSize = QSize(maximumSide, maximumSide);
    }

    QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    preview.image = image;
    return preview;
}

} // namespace

ImagePreviewService::ImagePreviewService(QObject* parent,
                                         qint64 maxCacheBytes,
                                         int maximumPreviewSide)
    : QObject(parent),
      m_Cache(cacheLimitKiB(maxCacheBytes)),
      m_CacheLimitBytes(static_cast<qint64>(cacheLimitKiB(maxCacheBytes)) * 1024),
      m_MaximumPreviewSide(ImagePreviewPolicy::normalizedMaximumSide(maximumPreviewSide))
{
}

ImagePreviewService::~ImagePreviewService()
{
    cancelPending();
}

quint64 ImagePreviewService::request(const QString& filePath, Format format)
{
    const quint64 requestId = ++m_NextRequestId;
    m_ActiveRequests.insert(requestId);
    const int maximumPreviewSide = m_MaximumPreviewSide;
    const QString key = cacheKey(filePath, format, maximumPreviewSide);
    if (const ImagePreviewData* cached = m_Cache.object(key)) {
        const ImagePreviewData preview = *cached;
        ++m_CacheHits;
        QTimer::singleShot(0, this, [this, requestId, preview]() {
            if (m_ActiveRequests.remove(requestId)) {
                emit previewReady(requestId, preview);
            }
        });
        return requestId;
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto* watcher = new QFutureWatcher<ImagePreviewData>(this);
    m_Pending.insert(requestId, {watcher, cancelled});
    connect(watcher, &QFutureWatcher<ImagePreviewData>::finished, this,
            [this, requestId, key, watcher, cancelled]() {
                finishRequest(requestId, key, watcher, cancelled);
            });
    watcher->setFuture(QtConcurrent::run([filePath, format, maximumPreviewSide, cancelled]() {
        if (cancelled->load()) {
            return ImagePreviewData();
        }
        ImagePreviewData preview = decode(filePath, format, maximumPreviewSide);
        if (cancelled->load()) {
            preview.image = QImage();
        }
        return preview;
    }));
    return requestId;
}

void ImagePreviewService::setMaximumPreviewSide(int maximumPreviewSide)
{
    const int normalized = ImagePreviewPolicy::normalizedMaximumSide(maximumPreviewSide);
    if (m_MaximumPreviewSide == normalized) {
        return;
    }
    cancelPending();
    m_Cache.clear();
    m_MaximumPreviewSide = normalized;
}

int ImagePreviewService::maximumPreviewSide() const
{
    return m_MaximumPreviewSide;
}

void ImagePreviewService::cancelPending()
{
    m_ActiveRequests.clear();
    for (auto it = m_Pending.begin(); it != m_Pending.end(); ++it) {
        it->cancelled->store(true);
    }
}

int ImagePreviewService::cacheEntryCount() const
{
    return m_Cache.size();
}

qint64 ImagePreviewService::cacheBytes() const
{
    return static_cast<qint64>(m_Cache.totalCost()) * 1024;
}

qint64 ImagePreviewService::cacheLimitBytes() const
{
    return m_CacheLimitBytes;
}

quint64 ImagePreviewService::cacheHits() const
{
    return m_CacheHits;
}

ImagePreviewData ImagePreviewService::decode(const QString& filePath,
                                             Format format,
                                             int maximumPreviewSide)
{
    const int normalized = ImagePreviewPolicy::normalizedMaximumSide(maximumPreviewSide);
    return format == Format::Svg ?
           decodeSvg(filePath, normalized) : decodeBitmap(filePath, normalized);
}

QString ImagePreviewService::cacheKey(const QString& filePath,
                                      Format format,
                                      int maximumPreviewSide)
{
    const QFileInfo info(filePath);
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(QDir::cleanPath(info.absoluteFilePath()))
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch())
        .arg(format == Format::Svg ? QLatin1String("svg") : QLatin1String("bitmap"))
        .arg(maximumPreviewSide);
}

void ImagePreviewService::finishRequest(
    quint64 requestId,
    const QString& key,
    QFutureWatcher<ImagePreviewData>* watcher,
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    const ImagePreviewData preview = watcher->result();
    m_Pending.remove(requestId);
    watcher->deleteLater();
    if (cancelled->load() || !m_ActiveRequests.remove(requestId)) {
        return;
    }
    if (!preview.image.isNull()) {
        m_Cache.insert(key, new ImagePreviewData(preview), imageCostKiB(preview.image));
    }
    emit previewReady(requestId, preview);
}
