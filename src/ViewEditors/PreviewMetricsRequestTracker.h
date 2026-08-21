/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PREVIEWMETRICSREQUESTTRACKER_H
#define PREVIEWMETRICSREQUESTTRACKER_H

#include <QString>

#include <array>
#include <optional>

enum class PreviewMetricsRequestPurpose {
    Status = 0,
    Calibration,
    Inspection,
    Settings,
    Count
};

struct PreviewMetricsRequestToken
{
    quint64 id = 0;
    PreviewMetricsRequestPurpose purpose = PreviewMetricsRequestPurpose::Status;
    QString elementKey;

    bool isValid() const { return id != 0; }
};

class PreviewMetricsRequestTracker
{
public:
    void begin(quint64 id,
               PreviewMetricsRequestPurpose purpose,
               const QString &elementKey = QString());
    std::optional<PreviewMetricsRequestToken> take(quint64 id);
    void cancel(PreviewMetricsRequestPurpose purpose);
    void clear();
    bool hasActive(PreviewMetricsRequestPurpose purpose) const;

private:
    static std::size_t indexFor(PreviewMetricsRequestPurpose purpose);

    std::array<PreviewMetricsRequestToken,
               static_cast<std::size_t>(PreviewMetricsRequestPurpose::Count)> m_active;
};

#endif // PREVIEWMETRICSREQUESTTRACKER_H
