/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ViewEditors/PreviewMetricsRequestTracker.h"

#include <QtGlobal>

std::size_t PreviewMetricsRequestTracker::indexFor(PreviewMetricsRequestPurpose purpose)
{
    const std::size_t index = static_cast<std::size_t>(purpose);
    Q_ASSERT(index < static_cast<std::size_t>(PreviewMetricsRequestPurpose::Count));
    return index;
}

void PreviewMetricsRequestTracker::begin(
    quint64 id,
    PreviewMetricsRequestPurpose purpose,
    const QString &elementKey)
{
    PreviewMetricsRequestToken token;
    token.id = id;
    token.purpose = purpose;
    token.elementKey = elementKey;
    m_active[indexFor(purpose)] = token;
}

std::optional<PreviewMetricsRequestToken> PreviewMetricsRequestTracker::take(quint64 id)
{
    if (id == 0) {
        return std::nullopt;
    }
    for (PreviewMetricsRequestToken &token : m_active) {
        if (token.id == id) {
            const PreviewMetricsRequestToken result = token;
            token = PreviewMetricsRequestToken();
            return result;
        }
    }
    return std::nullopt;
}

void PreviewMetricsRequestTracker::cancel(PreviewMetricsRequestPurpose purpose)
{
    m_active[indexFor(purpose)] = PreviewMetricsRequestToken();
}

void PreviewMetricsRequestTracker::clear()
{
    for (PreviewMetricsRequestToken &token : m_active) {
        token = PreviewMetricsRequestToken();
    }
}

bool PreviewMetricsRequestTracker::hasActive(PreviewMetricsRequestPurpose purpose) const
{
    return m_active[indexFor(purpose)].isValid();
}
