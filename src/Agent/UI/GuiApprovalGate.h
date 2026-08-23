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

    bool waitForApproval(const QString &toolCallId,
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
        }
        if (QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread()) {
            QEventLoop loop;
            m_loop = &loop;
            while (!m_decided && !(m_cancellation && m_cancellation->isCancelled())) {
                loop.processEvents(QEventLoop::AllEvents, 100);
            }
            m_loop = nullptr;
        } else {
            QMutexLocker locker(&m_mutex);
            while (!m_decided && !(m_cancellation && m_cancellation->isCancelled())) {
                m_cv.wait(&m_mutex, 100);
            }
        }
        QMutexLocker locker(&m_mutex);
        m_pendingId.clear();
        return m_approved && !(m_cancellation && m_cancellation->isCancelled());
    }

    void resolve(const QString &toolCallId, bool approved)
    {
        QMutexLocker locker(&m_mutex);
        if (!m_pendingId.isEmpty() && toolCallId != m_pendingId) return;
        m_approved = approved;
        m_decided = true;
        m_cv.wakeAll();
        if (m_loop) m_loop->quit();
    }

    void cancel()
    {
        QMutexLocker locker(&m_mutex);
        m_approved = false;
        m_decided = true;
        m_cv.wakeAll();
        if (m_loop) m_loop->quit();
    }

private:
    AgentCancellation *m_cancellation = nullptr;
    QMutex m_mutex;
    QWaitCondition m_cv;
    QString m_pendingId;
    bool m_decided = false;
    bool m_approved = false;
    QEventLoop *m_loop = nullptr;
};

} // namespace SigilAgent

#endif
