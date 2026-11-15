#pragma once

#include <QEventLoop>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <functional>
#include "Agent/AgentTypes.h"

namespace SigilAgent {
constexpr int MODEL_NETWORK_RETRIES = 5;

inline bool retryableModelError(const ModelTurn &turn)
{
    if (turn.error.isEmpty() || turn.finishReason == QLatin1String("cancelled")) return false;
    if (turn.retryableError) return true;
    // Compatibility for mock/other providers. Native HTTP uses typed network
    // codes so system language never affects classification.
    static const QRegularExpression http(QStringLiteral("\\bHTTP\\s+(408|429|5[0-9]{2})\\b"), QRegularExpression::CaseInsensitiveOption);
    const QString error = turn.error.toLower();
    return http.match(turn.error).hasMatch() || error.contains("connection closed")
        || error.contains("connection reset") || error.contains(QStringLiteral("连接已关闭"))
        || error.contains("timed out") || error.contains("stream stalled");
}

inline int modelRetryDelay(int attempt, int retryAfterMs = 0)
{
    const int base = 200 * (1 << qBound(0, attempt - 1, 4));
    const int jitter = QRandomGenerator::global()->bounded(qMax(1, base / 5));
    return qBound(0, qMax(base - base / 10 + jitter, retryAfterMs), 60000);
}

inline bool waitForModelRetry(int milliseconds, const std::function<bool()> &cancelled)
{
    if (cancelled()) return false;
    QEventLoop loop;
    QTimer deadline, poll;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (cancelled()) loop.quit(); });
    deadline.start(milliseconds); poll.start(25); loop.exec();
    return !cancelled();
}
} // namespace SigilAgent
