/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_PATCH_RANGE_H
#define SIGIL_AGENT_PATCH_RANGE_H

#include <QJsonObject>
#include <QString>

namespace SigilAgent
{

struct PatchRangeResolution {
    bool ok = false;
    QString code;
    QString message;
    int start = 0;
    int end = 0;
    bool rangeCorrected = false;
    QJsonObject data;
};

bool rangeSplitsMarkup(const QString &source, int start, int end);
PatchRangeResolution resolvePatchRange(const QString &source,
                                       int start,
                                       int end,
                                       const QString &expected_text);

} // namespace SigilAgent

#endif
