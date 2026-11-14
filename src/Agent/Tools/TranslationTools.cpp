#include "Agent/Tools/TranslationTools.h"

#include <QMetaObject>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include "Agent/Tools/ToolRegistry.h"
#include "BuiltinPlugins/Translation/TranslationController.h"

namespace SigilAgent {
namespace {
using Controller = BuiltinPlugins::Translation::TranslationController;
class TranslationTool final : public IAgentTool {
public:
    AgentToolDescriptor info;
    QPointer<Controller> controller;
    QString agentSession;
    std::function<void()> started;
    AgentToolDescriptor descriptor() const override { return info; }
    ToolResult execute(const QJsonObject &arguments) override
    {
        if (!controller) return ToolResult::failure("NO_BOOK", "Translation window is closed");
        const auto properties = info.inputSchema.value("properties").toObject();
        for (auto it = arguments.begin(); it != arguments.end(); ++it) {
            const auto property = properties.value(it.key()).toObject();
            const QString type = property.value("type").toString();
            const auto value = it.value();
            const bool legacyContext = info.name == "translation.context" && it.key() == "value" && (value.isObject() || value.isArray());
            if (!properties.contains(it.key()) || (type == "string" && !value.isString() && !legacyContext)
                || (type == "array" && !value.isArray()) || (type == "boolean" && !value.isBool())
                || (type == "object" && !value.isObject())
                || (type == "integer" && (!value.isDouble() || value.toDouble() != value.toInt()))) {
                return ToolResult::failure("INVALID_ARGUMENT", "Invalid translation parameter: " + it.key());
            }
            if (property.contains("enum") && !property.value("enum").toArray().contains(value))
                return ToolResult::failure("INVALID_ARGUMENT", "Unknown translation option: " + it.key());
            if (type == "array" && property.value("items").toObject().value("type").toString() == "string") {
                for (const auto &entry : value.toArray()) if (!entry.isString()) return ToolResult::failure("INVALID_ARGUMENT", "Expected string array");
            }
        }
        QJsonObject effective = arguments;
        if (info.name == "translation.context") {
            const QString kind = arguments.value("kind").toString("glossary");
            const QString field = kind == "glossary" ? "terms" : kind;
            int supplied = arguments.contains("value");
            for (const QString &key : {QStringLiteral("terms"), QStringLiteral("plan"), QStringLiteral("memory")}) {
                if (!arguments.contains(key)) continue;
                if (key != field || ++supplied > 1) return ToolResult::failure("INVALID_ARGUMENT", "Use one typed field matching kind: glossary/terms, plan/plan, memory/memory");
                effective["value"] = arguments.value(key);
            }
            if (kind != "memory" && effective.value("value").isString()) {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(effective.value("value").toString().toUtf8(), &error);
                if (error.error != QJsonParseError::NoError || (kind == "plan" ? !document.isObject() : (!document.isArray() && !document.isObject())))
                    return ToolResult::failure("INVALID_CONTEXT", "Expected complete JSON for glossary/plan; prefer the typed terms/plan parameter");
                effective["value"] = document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
            }
        }
        Controller::Result result;
        auto call = [&]() {
            if (!controller || controller->agentSessionId() != agentSession) {
                result = Controller::Result::error("AGENT_SESSION_CHANGED", "This translation tool belongs to an ended Agent conversation"); return;
            }
            const QString contextId = arguments.value("session_id").toString();
            if (!contextId.isEmpty() && contextId != controller->contextId()) {
                result = Controller::Result::error("CONTEXT_CHANGED", "Prepare/read the translation scope in this Agent conversation first"); return;
            }
            if (info.name == "translation.prepare") result = controller->prepare(arguments);
            else if (info.name == "translation.run") result = controller->run(arguments);
            else if (info.name == "translation.status") result = Controller::Result::success(controller->status());
            else if (info.name == "translation.control") result = controller->control(arguments);
            else if (info.name == "translation.context") result = controller->context(effective);
            else if (info.name == "translation.revise") result = controller->revise(arguments);
            else if (info.name == "translation.format") result = controller->format(arguments);
            else result = controller->saveGlossary(arguments);
            if (!controller || controller->agentSessionId() != agentSession) {
                result = Controller::Result::error("AGENT_SESSION_CHANGED", "Agent conversation changed during translation"); return;
            }
            if (result.ok && started && controller->running() && (info.name == "translation.run"
                || info.name == "translation.revise" || (info.name == "translation.control" && arguments.value("action") == "retry_failed"))) started();
        };
        if (QThread::currentThread() == controller->thread()) call();
        else QMetaObject::invokeMethod(controller, call, Qt::BlockingQueuedConnection);
        return result.ok ? ToolResult::success(result.data, result.applied, result.previewOnly)
                         : ToolResult::failure(result.code, result.message, result.data);
    }
};
QJsonObject string() { return {{"type", "string"}}; }
QJsonObject array() { return {{"type", "array"}, {"items", string()}}; }
QJsonObject enumeration(const QJsonArray &values) { return {{"type", "string"}, {"enum", values}}; }
QJsonObject terms()
{
    return {{"type", "array"}, {"items", QJsonObject {{"type", "object"}, {"properties", QJsonObject {
        {"source", string()}, {"target", string()}, {"aliases", array()}, {"note", string()}, {"locked", QJsonObject {{"type", "boolean"}}}}},
        {"required", QJsonArray {"source", "target"}}}}};
}
}

void registerTranslationTools(ToolRegistry *registry, Controller *controller, std::function<void()> started)
{
    auto add = [&](const QString &name, const QString &description, bool writes, QJsonObject properties) {
        auto tool = std::make_unique<TranslationTool>();
        tool->controller = controller;
        tool->agentSession = controller->agentSessionId();
        tool->started = started;
        properties["session_id"] = QJsonObject {{"type", "string"}, {"description", "Optional prepared-scope ID from translation.prepare in this Agent conversation."}};
        tool->info = {name, description, QJsonObject {{"type", "object"}, {"properties", properties}, {"additionalProperties", false}},
            writes ? ToolRisk::Bulk : ToolRisk::Read, writes, false};
        registry->add(std::move(tool));
    };
    add("translation.prepare", "Prepare complete reading paragraphs from the live book, including unsaved text. Default whole spine; pass resource_ids for files and selection_start/end (UTF-16) for one file selection. No remote requests. Returns prepared-scope session_id and counts isolated to this Agent conversation. Reuse its existing context instead of repeatedly preparing.", false,
        {{"resource_ids", array()}, {"language", string()}, {"style", string()},
         {"selection_start", QJsonObject {{"type", "integer"}}}, {"selection_end", QJsonObject {{"type", "integer"}}}});
    add("translation.run", "Start this Agent conversation's native batch workflow: scan, aggregate, translate, review, format. OMIT stages for the complete workflow; preset:preserve changes formatting only. Full-book scan followed by translation needs the aggregate stage. Set stages only for an explicitly requested subset (sample: translate only). Returns immediately; host batches and applies all paragraphs as bilingual pairs. No per-sentence patches or HTML. Does not save EPUB.", true,
        {{"stages", QJsonObject {{"type", "array"}, {"items", enumeration({"scan", "aggregate", "translate", "review", "format"})}}},
         {"preset", enumeration({"chinese", "preserve"})}, {"retranslate", QJsonObject {{"type", "boolean"}}}});
    add("translation.status", "Read this Agent conversation's native translation progress, requests/usage, cache hits and failed locations. No model calls. Use when progress is requested; background completion notices resume the Agent, so do not busy-poll.", false, {});
    add("translation.control", "Pause, resume, stop or retry missing translations in this Agent conversation. retry_failed starts a new ASYNCHRONOUS workflow and returns immediately. End the current reply after starting it; the host sends another background completion notice. Do not poll status in a loop. Stop rejects late model writes and keeps current Book edits. No restart persistence.", true,
        {{"action", enumeration({"pause", "resume", "stop", "retry_failed"})}});
    add("translation.context", "Read/update temporary plan, memory or glossary isolated to this Agent conversation. For action:update use the typed field matching kind: glossary -> terms array, plan -> plan object, memory -> memory string. Prefer typed fields; legacy value also accepts complete JSON-encoded glossary/plan or plain memory. Read glossary pages with offset/limit. No remote calls or disk persistence.", false,
        {{"kind", enumeration({"plan", "memory", "glossary"})}, {"action", enumeration({"read", "update"})},
         {"terms", terms()}, {"plan", QJsonObject {{"type", "object"}}}, {"memory", string()},
         {"value", QJsonObject {{"type", "string"}, {"description", "Legacy compatibility field; prefer terms/plan/memory."}}},
         {"offset", QJsonObject {{"type", "integer"}}}, {"limit", QJsonObject {{"type", "integer"}}}});
    add("translation.revise", "Start ASYNCHRONOUS batch correction using instruction and/or corrected terms. Terms limit review to source paragraphs that contain them. End the current reply after starting; the host sends another completion notice. Do not poll status in a loop. Updates existing translation nodes, not sentence patches. Stop the current run first.", true,
        {{"instruction", string()}, {"terms", terms()}});
    add("translation.format", "Apply the native Chinese reading preset to the prepared scope (horizontal/LTR for whole-book scope, responsive images). Preserve preset retains original layout. Reuses Sigil converters; no model calls or approval workflow.", true,
        {{"preset", enumeration({"chinese", "preserve"})}});
    add("translation.save_glossary", "Save completed temporary terms as JSON in the current book's actual Misc folder, using native resource creation. Unique filename by default; resource_id explicitly updates an existing Misc JSON. No keys or request logs; EPUB remains unsaved until normal Save.", true,
        {{"filename", string()}, {"resource_id", string()}});
}
} // namespace SigilAgent
