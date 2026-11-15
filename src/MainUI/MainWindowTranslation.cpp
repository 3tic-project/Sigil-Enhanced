#include "MainUI/MainWindow.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include "Agent/Core/AgentController.h"
#include "Agent/Execution/SigilBookWorkspace.h"
#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/Persistence/AgentSettings.h"
#include "Agent/UI/AgentDock.h"
#include "Agent/Tools/TranslationTools.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XmlWellFormed.h"
#include "BuiltinPlugins/Translation/TranslationController.h"
#include "BuiltinPlugins/VerticalToHorizontalConverter.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/ValidationResultsView.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "ResourceObjects/OPFResource.h"

void MainWindow::CreateAgentTranslation()
{
    using namespace BuiltinPlugins::Translation;
    m_TranslationController = std::make_unique<TranslationController>(this);
    auto *controller = m_TranslationController.get();
    controller->setWorkspace(m_AgentWorkspace.get());
    controller->bindAgentSession(m_AgentController->session()->id());
    controller->setMiscFolder(m_Book->GetFolderKeeper()->GetDefaultFolderForGroup("Misc"));
    controller->setConfigurationProvider([]() {
        SigilAgent::AgentSettings settings;
        RequestConfiguration config;
        config.agent = settings.providerConfig(); config.translator = settings.translationProviderConfig();
        config.concurrency = settings.translationConcurrency();
        config.translationCharacters = settings.translationBatchCharacters(); config.reviewWithAgent = settings.translationReviewWithAgent();
        config.spacing = settings.translationSpacing();
        const auto catalog = SigilAgent::AgentModelCatalog::fromCacheJson(QJsonDocument::fromJson(settings.catalogJson().toUtf8()).object());
        config.agentContext = settings.modelContextLength() > 0 ? settings.modelContextLength() : 32768;
        if (config.translator.model == config.agent.model) config.translatorContext = config.agentContext;
        for (const auto &model : catalog.models) {
            if (model.id == config.agent.model && model.contextLength > 0) config.agentContext = model.contextLength;
            if (model.id == config.translator.model && model.contextLength > 0) config.translatorContext = model.contextLength;
        }
        return config;
    });
    controller->setFormatHandler([this](const QJsonObject &options) {
        using Result = SigilAgent::BookOpResult;
        if (!m_Book || m_AgentWorkspace->hasOpenTransaction()) return Result::error("TRANSACTION_OPEN", tr("请先结束当前书籍事务。"));
        const bool chinese = options.value("preset").toString("chinese") == "chinese";
        QStringList paths;
        for (const auto &id : options.value("resource_ids").toArray()) {
            if (auto *resource = m_Book->GetFolderKeeper()->GetResourceByIdentifier(id.toString())) paths.append(resource->GetRelativePath());
        }
        QList<ValidationResult> results;
        QStringList fixed;
        bool modified = false;
        if (chinese) {
            BuiltinPlugins::VerticalToHorizontalConverter::Options conversion;
            conversion.dryRun = false;
            conversion.selectedBookPaths = paths;
            conversion.updatePageProgression = options.value("whole_book").toBool();
            const auto converted = BuiltinPlugins::VerticalToHorizontalConverter(m_Book.data()).convert(conversion);
            results += converted.validationResults; modified = converted.modified;
            fixed = converted.before.fixedLayoutBook ? paths : converted.before.fixedLayoutBookPaths;
            if (!converted.ok) {
                m_ValidationResultsView->LoadResults(results);
                return Result::error("FORMAT_FAILED", tr("方向转换未完成，请查看校验结果。"));
            }
            if (options.value("whole_book").toBool() && !converted.before.fixedLayoutBook) {
                auto *opf = m_Book->GetOPF();
                const auto progression = BuiltinPlugins::VerticalCssTransformer::transformOpfProgression(opf->GetSourceText(), true);
                if (progression.ok && progression.changed) { opf->SetTextAsUndoableEdit(progression.text); modified = true; }
            }
            QHash<QString, QString> changes;
            for (const auto &id : options.value("resource_ids").toArray()) {
                auto *resource = m_Book->GetFolderKeeper()->GetResourceByIdentifier(id.toString());
                auto *text = qobject_cast<TextResource *>(resource);
                if (!text || fixed.contains(resource->GetRelativePath())) continue;
                QString source = text->GetText();
                if (source.contains(QStringLiteral("id=\"sigil-translation-images\""))) continue;
                static const QRegularExpression head(QStringLiteral("</(?:[A-Za-z0-9_-]+:)?head\\s*>"), QRegularExpression::CaseInsensitiveOption);
                const auto match = head.match(source);
                if (!match.hasMatch()) continue;
                // Preserve all SVG drawing/link semantics; responsive constraints
                // solve the common image-wrapper sizing problem without flattening.
                source.insert(match.capturedStart(), QStringLiteral(
                    "<style type=\"text/css\" id=\"sigil-translation-images\">\n"
                    "img { max-width:100%; height:auto; object-fit:contain; }\n"
                    "svg { max-width:100%; }\n</style>\n"));
                if (XmlWellFormed::Check(source).line < 0) changes.insert(id.toString(), source);
            }
            if (!changes.isEmpty()) {
                auto result = m_AgentWorkspace->beginTransaction(tr("翻译图片适配"));
                if (!result.ok) return result;
                for (auto it = changes.begin(); it != changes.end(); ++it) {
                    result = m_AgentWorkspace->replaceText(it.key(), it.value(), m_AgentWorkspace->resourceRevision(it.key()));
                    if (!result.ok) { m_AgentWorkspace->rollbackTransaction(); return result; }
                }
                result = m_AgentWorkspace->commitTransaction(m_AgentWorkspace->revision());
                if (!result.ok) { m_AgentWorkspace->rollbackTransaction(); return result; }
                modified = true;
            }
        }
        results += m_ValidationResultsView->validateXhtml();
        m_ValidationResultsView->LoadResults(results);
        if (!results.isEmpty()) m_ValidationResultsView->show();
        return Result::success({{"preset", chinese ? "chinese" : "preserve"}, {"fixed_layout_preserved", fixed.size()}}, modified);
    });
    connect(controller, &TranslationController::changed, this, [this, controller]() {
        if (m_AgentDock) m_AgentDock->setBackgroundTaskStatus(controller->status());
    });
    connect(m_AgentDock, &SigilAgent::AgentDock::backgroundTaskControlRequested, this, [this, controller](const QString &action) {
        if (controller->agentSessionId() != m_AgentController->session()->id()) return;
        const auto result = controller->control({{"action", action}});
        if (result.ok && action == "retry_failed" && controller->running()) {
            m_TranslationAgentSession = controller->agentSessionId(); m_TranslationAgentContext = controller->contextId();
            m_TranslationAgentContinuations = 0;
        }
    });
    connect(controller, &TranslationController::bookModified, this, [this]() {
        m_Book->SetModified(true); m_BookBrowser->Refresh(); UpdateAgentContext();
    });
    connect(controller, &TranslationController::finished, this, [this]() {
        if (m_TranslationController->status().value("phase").toString() != "format") {
            m_ValidationResultsView->LoadResults(m_ValidationResultsView->validateXhtml());
        }
        UpdateAgentContext();
        if (m_TranslationAgentContext == m_TranslationController->contextId() && !m_TranslationAgentSession.isEmpty()) {
            m_TranslationAgentNotice = m_TranslationController->status();
            QJsonArray problems, messages;
            for (const auto &value : m_TranslationAgentNotice.value("problems").toArray()) { if (problems.size() == 5) break; problems.append(value); }
            for (const auto &value : m_TranslationAgentNotice.value("messages").toArray()) { if (messages.size() == 3) break; messages.append(value); }
            m_TranslationAgentNotice["problems"] = problems; m_TranslationAgentNotice["messages"] = messages;
            QTimer::singleShot(0, this, &MainWindow::ContinueTranslationAgent);
        }
    });
    m_AgentController->setExtraTools([this, controller](SigilAgent::ToolRegistry *registry) {
        SigilAgent::registerTranslationTools(registry, controller, [this, controller]() {
            const QString session = m_AgentController->session()->id();
            const auto turns = m_AgentController->session()->eventsOf(SigilAgent::AgentEventType::UserMessage);
            const bool background = !turns.isEmpty() && turns.last().payload.value("source") == "background";
            if (!background || m_TranslationAgentSession != session) m_TranslationAgentContinuations = 0;
            m_TranslationAgentSession = session; m_TranslationAgentContext = controller->contextId();
        });
    });

}

void MainWindow::ResetAgentTranslationSession(const QString &sessionId)
{
    m_TranslationAgentNotice = {}; m_TranslationAgentSession.clear(); m_TranslationAgentContext.clear();
    m_TranslationAgentContinuations = 0;
    if (m_TranslationController) m_TranslationController->bindAgentSession(sessionId);
}

void MainWindow::ContinueTranslationAgent()
{
    if (m_TranslationAgentNotice.isEmpty() || !m_TranslationController || !m_AgentController) return;
    if (m_TranslationController->running()) { m_TranslationAgentNotice = {}; return; }
    if (m_CloseAfterAgentRun || m_TranslationController->contextId() != m_TranslationAgentContext
        || m_TranslationController->agentSessionId() != m_TranslationAgentSession
        || m_AgentController->session()->id() != m_TranslationAgentSession) { m_TranslationAgentNotice = {}; return; }
    if (m_AgentController->isRunning()) { QTimer::singleShot(100, this, &MainWindow::ContinueTranslationAgent); return; }
    const auto notice = m_TranslationAgentNotice; m_TranslationAgentNotice = {};
    if (m_AgentController->cancellation()->isCancelled()) return;
    const QString message = tr("后台翻译已结束。以下为宿主实际结果：%1\n请接续原用户目标：按需处理失败项（最多两次），仅在用户要求时保存词表，并报告实际完成/失败数量及书籍尚未保存。written/translated 表示已存在于书中的译文，reviewed 仅表示已校正，不能将未校正的译文说成未写入；stage_failures 区分扫描和校正问题。不要重新启动已完成的全书翻译。若启动补缺或校正，它会继续在后台执行；请简短说明已启动并结束本轮回复，等待下一条宿主完成通知，不要循环查询状态。")
        .arg(QString::fromUtf8(QJsonDocument(notice).toJson(QJsonDocument::Compact)));
    if (++m_TranslationAgentContinuations > 3) {
        m_AgentController->session()->append(SigilAgent::AgentEventType::AssistantMessage,
            {{"content", tr("翻译后续处理已达到重试次数，剩余问题请通过 Agent 查询翻译状态。")}, {"is_final", true}});
        return;
    }
    m_AgentController->continueAfterBackground(message, m_TranslationAgentSession, m_AgentWorkspace->bookSessionId());
    if (m_AgentDock && m_AgentController->runner()) m_AgentDock->setRunState(m_AgentController->runner()->state());
}
