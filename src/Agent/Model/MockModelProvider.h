/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_MOCK_MODEL_PROVIDER_H
#define SIGIL_AGENT_MOCK_MODEL_PROVIDER_H

#include <functional>

#include "Agent/Model/IModelProvider.h"

namespace SigilAgent
{

class MockModelProvider : public IModelProvider
{
public:
    using Script = std::function<ModelTurn(const ModelRequest &)>;

    void setScript(Script script);
    void addTurn(const ModelTurn &turn);
    int requestCount() const;
    ModelRequest lastRequest() const;
    ModelCapabilities capabilities() const override;
    ModelTurn stream(const ModelRequest &request, ModelStreamSink &sink) override;

private:
    Script m_script;
    QList<ModelTurn> m_turns;
    int m_index = 0;
    int m_requestCount = 0;
    ModelRequest m_lastRequest;
};

} // namespace SigilAgent

#endif
