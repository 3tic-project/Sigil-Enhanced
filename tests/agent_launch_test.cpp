#include <cstdlib>
#include <iostream>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Agent/Core/AgentCancellation.h"
#include "Agent/Core/AgentRunner.h"
#include "Agent/Core/AgentSession.h"
#include "Agent/Execution/MemoryBookWorkspace.h"
#include "Agent/Model/MockModelProvider.h"
#include "Agent/Security/PermissionPolicy.h"
#include "Agent/Tools/BookTools.h"
#include "Agent/Tools/ToolRegistry.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QString jsonField(const QString &blob, const QString &key)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(blob.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) return QString();
    QJsonObject object = document.object();
    if (object.contains(QStringLiteral("data")) && object.value(QStringLiteral("data")).isObject()) {
        object = object.value(QStringLiteral("data")).toObject();
    }
    if (object.contains(key)) return object.value(key).toString();
    if (key == QLatin1String("family")) {
        const QJsonArray families = object.value(QStringLiteral("declared_families")).toArray();
        if (!families.isEmpty()) return families.first().toString();
        const QJsonArray css = object.value(QStringLiteral("css_families")).toArray();
        if (!css.isEmpty()) return css.first().toObject().value(QStringLiteral("family")).toString();
    }
    return QString();
}

SigilAgent::AgentRunResult launchOnce()
{
    using namespace SigilAgent;
    MemoryBookWorkspace book = MemoryBookWorkspace::samplePhysicsBook();
    ToolRegistry registry;
    registerBookTools(&registry, &book);
    AgentSession session;
    AgentCancellation cancellation;
    PermissionPolicy policy;
    AutoApprovalGate gate(true);
    MockModelProvider provider;
    provider.setScript([](const ModelRequest &request) {
        QString summary_blob;
        QString font_blob;
        for (const ChatMessage &message : request.messages) {
            if (message.role != QLatin1String("tool")) continue;
            if (message.content.contains(QStringLiteral("spine_count"))) summary_blob = message.content;
            if (message.content.contains(QStringLiteral("embedded_fonts"))
                || message.content.contains(QStringLiteral("declared_families"))) {
                font_blob = message.content;
            }
        }
        ModelTurn turn;
        if (summary_blob.isEmpty()) {
            ToolCall call;
            call.id = QStringLiteral("t-summary");
            call.name = QStringLiteral("book.summary");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        if (font_blob.isEmpty()) {
            ToolCall call;
            call.id = QStringLiteral("t-fonts");
            call.name = QStringLiteral("font.inventory");
            call.argumentsJson = QStringLiteral("{}");
            turn.toolCalls.append(call);
            return turn;
        }
        const QString title = jsonField(summary_blob, QStringLiteral("title"));
        turn.content = QStringLiteral("Summary title=%1 fonts=%2").arg(title, font_blob);
        return turn;
    });
    AgentRunner runner(&session, &provider, &registry, &book, &policy, &gate, &cancellation);
    runner.setMode(AgentMode::Ask);
    runner.setModel(QStringLiteral("mock"));
    return runner.runTurn(QStringLiteral("summarize this book / list fonts"));
}

} // namespace

int main()
{
    const SigilAgent::AgentRunResult first = launchOnce();
    const SigilAgent::AgentRunResult second = launchOnce();
    auto check = [](const SigilAgent::AgentRunResult &result, const char *label) {
        Require(result.state == SigilAgent::AgentRunState::Completed, label);
        Require(result.toolNames.size() == 2, "launch must invoke exactly the inspect tools");
        Require(result.toolNames.at(0) == QStringLiteral("book.summary"),
                "first tool must be the shipped book.summary");
        Require(result.toolNames.at(1) == QStringLiteral("font.inventory"),
                "second tool must be the shipped font.inventory");
        Require(!result.finalText.isEmpty(), "final assistant text must be non-empty");
        Require(result.finalText.contains(QStringLiteral("Junior Physics")),
                "final text must include the title from the tool result");
        Require(result.finalText.contains(QStringLiteral("SerifFace")),
                "final text must include a font family from the tool result");
        Require(result.finalText.startsWith(QStringLiteral("Summary title=")),
                "final text must be assembled from tool JSON, not a canned sentence");
        Require(result.finalText.contains(QStringLiteral("embedded_fonts")),
                "final text must embed the font.inventory tool JSON");
    };
    check(first, "first launch failed");
    check(second, "second launch failed");
    Require(first.finalText == second.finalText, "repeated launches must be consistent");
    return EXIT_SUCCESS;
}
