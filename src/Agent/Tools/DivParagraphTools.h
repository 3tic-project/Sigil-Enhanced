/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_DIV_PARAGRAPH_TOOLS_H
#define SIGIL_AGENT_DIV_PARAGRAPH_TOOLS_H

namespace SigilAgent
{

class AgentCancellation;
class IBookWorkspace;
class ToolRegistry;

void registerDivParagraphTools(ToolRegistry *registry,
                               IBookWorkspace *workspace,
                               AgentCancellation *cancellation = nullptr);

} // namespace SigilAgent

#endif
