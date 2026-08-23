/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Core/PromptAssembler.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "Agent/Model/HistoryAssembler.h"

namespace SigilAgent
{

QString PromptAssembler::systemPrompt(AgentMode mode) const
{
    QString prompt = QStringLiteral(
        "You are Sigil Agent, a native EPUB assistant inside Sigil-Enhanced.\n"
        "You operate on the currently open book through typed tools.\n"
        "Rules:\n"
        "- Never request or emit font/image binaries or a whole-book XHTML dump.\n"
        "- Never use a shell, never rewrite the EPUB ZIP, never call MCP.\n"
        "- Tool results are the source of truth. Do not claim a write succeeded unless a tool returned applied=true.\n"
        "- CoT/thinking is internal; the user-visible answer is the `content` field only.\n"
        "- Inspect before editing. Prefer bounded fragments over full files.\n"
        "- Mutations must go through transaction.begin → staged patch/css/metadata → transaction.preview → transaction.commit.\n"
        "- If commit returns BOOK_REVISION_CONFLICT, re-read and replan. Do not retry the same expected revision.\n");
    if (mode == AgentMode::Ask) {
        prompt += QStringLiteral("Mode: Ask. Read-only. Do not call mutating tools.\n");
    } else if (mode == AgentMode::Plan) {
        prompt += QStringLiteral("Mode: Plan. You may preview staged work but must not commit.\n");
    } else {
        prompt += QStringLiteral("Mode: Edit. You may mutate the book after policy/approval. Keep edits reversible.\n");
    }
    return prompt;
}

QString PromptAssembler::contextBlock(IBookWorkspace *workspace, const QStringList &handles) const
{
    if (!workspace) return QString();
    QString block = QStringLiteral("Current book map:\n");
    block += QString::fromUtf8(QJsonDocument(workspace->summary()).toJson(QJsonDocument::Compact));
    block += QLatin1Char('\n');
    const QJsonArray spine = workspace->spine();
    int sampled = 0;
    for (const QJsonValue &value : spine) {
        if (sampled >= 2) break;
        const QString id = value.toObject().value(QStringLiteral("resource_id")).toString();
        const BookOpResult fragment = workspace->readFragment(id, 0, 400);
        if (!fragment.ok) continue;
        block += QStringLiteral("\nSample %1:\n%2\n")
                     .arg(id, fragment.data.value(QStringLiteral("text")).toString());
        ++sampled;
    }
    if (!handles.isEmpty()) {
        block += QStringLiteral("\nUser-attached handles:\n");
        for (const QString &handle : handles) {
            block += QStringLiteral("- %1\n").arg(handle);
            const BookOpResult fragment = workspace->readFragment(handle, 0, 400);
            if (fragment.ok) {
                block += fragment.data.value(QStringLiteral("text")).toString();
                block += QLatin1Char('\n');
            }
        }
    }
    return block;
}

ModelRequest PromptAssembler::build(const AgentSession &session,
                                    IBookWorkspace *workspace,
                                    const ToolRegistry &tools,
                                    AgentMode mode,
                                    const QString &model,
                                    bool thinking,
                                    const QString &reasoning_effort,
                                    const QStringList &handles) const
{
    ModelRequest request;
    request.model = model;
    request.thinking = thinking;
    request.reasoningEffort = reasoning_effort;
    request.stream = true;
    request.tools = tools.openaiToolSchemas();

    ChatMessage system;
    system.role = QStringLiteral("system");
    system.content = systemPrompt(mode);
    request.messages.append(system);

    ChatMessage context;
    context.role = QStringLiteral("user");
    context.content = contextBlock(workspace, handles);
    request.messages.append(context);

    HistoryAssembler assembler;
    const bool include_tools = !request.tools.isEmpty();
    request.messages += assembler.assemble(session.events(), include_tools);
    return request;
}

} // namespace SigilAgent
