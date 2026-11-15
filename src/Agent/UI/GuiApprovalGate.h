/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_GUI_APPROVAL_GATE_H
#define SIGIL_AGENT_GUI_APPROVAL_GATE_H

#include <QCoreApplication>
#include <QEventLoop>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Security/PermissionPolicy.h"

namespace SigilAgent
{

class GuiApprovalGate : public IApprovalGate
{
public:
    explicit GuiApprovalGate(AgentCancellation *cancellation = nullptr) :
        m_cancellation(cancellation)
    {
    }

    ApprovalDecision waitForApproval(const QString &toolCallId,
                                     const QString &name,
                                     const QJsonObject &arguments,
                                     const QString &impact) override
    {
        Q_UNUSED(name);
        Q_UNUSED(arguments);
        Q_UNUSED(impact);
        {
            QMutexLocker locker(&m_mutex);
            m_pendingId = toolCallId;
            m_decided = false;
            m_approved = false;
            m_argumentOverrides = QJsonObject();
        }
        if (QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread()) {
            QEventLoop loop;
            QTimer pollCancellation;
            QObject::connect(&pollCancellation, &QTimer::timeout, &loop, [&]() {
                if (m_cancellation && m_cancellation->isCancelled()) loop.quit();
            });
            pollCancellation.start(100);
            bool pending;
            {
                QMutexLocker locker(&m_mutex);
                m_loop = &loop;
                pending = !m_decided && !(m_cancellation && m_cancellation->isCancelled());
            }
            // A queued quit also covers resolution between the check and exec.
            if (pending) loop.exec();
            {
                QMutexLocker locker(&m_mutex);
                m_loop = nullptr;
            }
        } else {
            QMutexLocker locker(&m_mutex);
            while (!m_decided && !(m_cancellation && m_cancellation->isCancelled())) {
                m_cv.wait(&m_mutex, 100);
            }
        }
        QMutexLocker locker(&m_mutex);
        m_pendingId.clear();
        const bool approved = m_approved
            && !(m_cancellation && m_cancellation->isCancelled());
        const QJsonObject overrides = approved
            ? m_argumentOverrides : QJsonObject();
        m_argumentOverrides = QJsonObject();
        return ApprovalDecision { approved, overrides };
    }

    void resolve(const QString &toolCallId, bool approved,
                 const QJsonObject &argumentOverrides = QJsonObject())
    {
        QMutexLocker locker(&m_mutex);
        if (!m_pendingId.isEmpty() && toolCallId != m_pendingId) return;
        m_approved = approved;
        m_argumentOverrides = approved ? argumentOverrides : QJsonObject();
        m_decided = true;
        m_cv.wakeAll();
        if (m_loop) QMetaObject::invokeMethod(m_loop, "quit", Qt::QueuedConnection);
    }

    void cancel()
    {
        QMutexLocker locker(&m_mutex);
        m_approved = false;
        m_argumentOverrides = QJsonObject();
        m_decided = true;
        m_cv.wakeAll();
        if (m_loop) QMetaObject::invokeMethod(m_loop, "quit", Qt::QueuedConnection);
    }

private:
    AgentCancellation *m_cancellation = nullptr;
    QMutex m_mutex;
    QWaitCondition m_cv;
    QString m_pendingId;
    bool m_decided = false;
    bool m_approved = false;
    QJsonObject m_argumentOverrides;
    QEventLoop *m_loop = nullptr;
};

} // namespace SigilAgent

#endif
