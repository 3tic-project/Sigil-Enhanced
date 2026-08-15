#pragma once
#ifndef IMAGEPREVIEWSERVICE_H
#define IMAGEPREVIEWSERVICE_H

#include <atomic>
#include <memory>

#include <QCache>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

#include "Misc/ImagePreviewPolicy.h"

struct ImagePreviewData {
    QImage image;
    QSize pixelSize;
    qint64 fileSize = 0;
};

class ImagePreviewService : public QObject
{
    Q_OBJECT

public:
    enum class Format {
        Bitmap,
        Svg
    };

    explicit ImagePreviewService(QObject* parent = nullptr,
                                 qint64 maxCacheBytes = 32LL * 1024 * 1024,
                                 int maximumPreviewSide = ImagePreviewPolicy::DEFAULT_MAXIMUM_SIDE);
    ~ImagePreviewService() override;

    quint64 request(const QString& filePath, Format format);
    void cancelPending();
    void setMaximumPreviewSide(int maximumPreviewSide);
    int maximumPreviewSide() const;

    int cacheEntryCount() const;
    qint64 cacheBytes() const;
    qint64 cacheLimitBytes() const;
    quint64 cacheHits() const;

    static ImagePreviewData decode(
        const QString& filePath,
        Format format,
        int maximumPreviewSide = ImagePreviewPolicy::DEFAULT_MAXIMUM_SIDE);

signals:
    void previewReady(quint64 requestId, const ImagePreviewData& preview);

private:
    struct PendingRequest {
        QFutureWatcher<ImagePreviewData>* watcher = nullptr;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    static QString cacheKey(const QString& filePath, Format format, int maximumPreviewSide);
    void finishRequest(quint64 requestId,
                       const QString& key,
                       QFutureWatcher<ImagePreviewData>* watcher,
                       const std::shared_ptr<std::atomic_bool>& cancelled);

    QCache<QString, ImagePreviewData> m_Cache;
    QHash<quint64, PendingRequest> m_Pending;
    QSet<quint64> m_ActiveRequests;
    quint64 m_NextRequestId = 0;
    quint64 m_CacheHits = 0;
    qint64 m_CacheLimitBytes;
    int m_MaximumPreviewSide;
};

#endif // IMAGEPREVIEWSERVICE_H
