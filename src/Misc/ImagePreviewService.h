#pragma once
#ifndef IMAGEPREVIEWSERVICE_H
#define IMAGEPREVIEWSERVICE_H

#include <atomic>
#include <memory>

#include <QCache>
#include <QFutureWatcher>
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
                                 qint64 maxCacheBytes = 8LL * 1024 * 1024,
                                 int maximumPreviewSide = ImagePreviewPolicy::DEFAULT_MAXIMUM_SIDE);
    ~ImagePreviewService() override;

    quint64 request(const QString& filePath, Format format);
    void cancelPending();
    void reset();
    void setMaximumPreviewSide(int maximumPreviewSide);
    int maximumPreviewSide() const;

    int cacheEntryCount() const;
    qint64 cacheBytes() const;
    qint64 cacheLimitBytes() const;
    quint64 cacheHits() const;
    int activeDecodeCount() const;
    int queuedRequestCount() const;

    static ImagePreviewData decode(
        const QString& filePath,
        Format format,
        int maximumPreviewSide = ImagePreviewPolicy::DEFAULT_MAXIMUM_SIDE);

signals:
    void previewReady(quint64 requestId, const ImagePreviewData& preview);

private:
    struct PreviewRequest {
        quint64 requestId = 0;
        QString key;
        QString filePath;
        Format format = Format::Bitmap;
        int maximumPreviewSide = ImagePreviewPolicy::DEFAULT_MAXIMUM_SIDE;
    };

    static QString cacheKey(const QString& filePath, Format format, int maximumPreviewSide);
    void startRequest(const PreviewRequest& request);
    void finishRequest(const PreviewRequest& request,
                       QFutureWatcher<ImagePreviewData>* watcher,
                       const std::shared_ptr<std::atomic_bool>& cancelled);
    void startQueuedRequest();

    QCache<QString, ImagePreviewData> m_Cache;
    QSet<quint64> m_ActiveRequests;
    QFutureWatcher<ImagePreviewData>* m_RunningWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_RunningCancelled;
    PreviewRequest m_QueuedRequest;
    bool m_HasQueuedRequest = false;
    quint64 m_NextRequestId = 0;
    quint64 m_CacheHits = 0;
    qint64 m_CacheLimitBytes;
    int m_MaximumPreviewSide;
};

#endif // IMAGEPREVIEWSERVICE_H
