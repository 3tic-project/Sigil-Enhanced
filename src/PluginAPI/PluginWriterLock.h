/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINWRITERLOCK_H
#define PLUGINWRITERLOCK_H

#include <QUuid>

namespace PluginApi
{

class WriterLock
{
public:
    bool Acquire(const QUuid &session_id)
    {
        if (session_id.isNull() || (!m_Owner.isNull() && m_Owner != session_id)) return false;
        m_Owner = session_id;
        return true;
    }

    void Release(const QUuid &session_id)
    {
        if (m_Owner == session_id) m_Owner = QUuid();
    }

    void Clear() { m_Owner = QUuid(); }
    bool IsHeld() const { return !m_Owner.isNull(); }

private:
    QUuid m_Owner;
};

}

#endif // PLUGINWRITERLOCK_H
