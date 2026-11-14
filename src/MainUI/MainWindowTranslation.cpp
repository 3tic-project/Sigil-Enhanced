#include "MainUI/MainWindow.h"

#include <QAction>
#include <QJsonDocument>
#include <QMenu>
#include <QRegularExpression>
#include "Agent/Core/AgentController.h"
#include "Agent/Execution/SigilBookWorkspace.h"
#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/Persistence/AgentSettings.h"
#include "Agent/Tools/TranslationTools.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XmlWellFormed.h"
#include "BuiltinPlugins/Translation/TranslationController.h"
#include "BuiltinPlugins/Translation/TranslationPanel.h"
#include "BuiltinPlugins/VerticalToHorizontalConverter.h"
#include "MainUI/BookBrowser.h"
#include "MainUI/PreviewWindow.h"
#include "MainUI/ValidationResultsView.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/ContentTab.h"

void MainWindow::CreateTranslationPanel()
{
    using namespace BuiltinPlugins::Translation;
    m_TranslationController = std::make_unique<TranslationController>(this);
    auto *controller = m_TranslationController.get();
    controller->setWorkspace(m_AgentWorkspace.get());
    controller->setMiscFolder(m_Book->GetFolderKeeper()->GetDefaultFolderForGroup("Misc"));
    controller->setConfigurationProvider([]() {
        SigilAgent::AgentSettings settings;
        RequestConfiguration config;
        config.agent = settings.providerConfig(); config.translator = settings.translationProviderConfig();
        config.concurrency = settings.translationConcurrency();
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
    m_TranslationPanel = new TranslationPanel(controller, m_AgentWorkspace.get(), this);
    connect(controller, &TranslationController::changed, this, [this, wasRunning = false]() mutable {
        const bool now = m_TranslationController->running();
        if (now && !wasRunning) { m_TranslationPanel->show(); m_TranslationPanel->raise(); }
        wasRunning = now;
    });
    m_TranslationPanel->setScopeProvider([this](const QString &scope) {
        QJsonObject args;
        if (scope == "book") return args;
        QJsonArray ids;
        if (scope == "files") {
            for (auto *resource : m_BookBrowser->ValidSelectedHTMLResources()) ids.append(resource->GetIdentifier());
        } else {
            ContentTab *tab = GetCurrentContentTab();
            Resource *resource = tab ? tab->GetLoadedResource() : nullptr;
            if (resource) ids.append(resource->GetIdentifier());
            if (scope == "selection" && tab) {
                const int a = tab->GetSelectionStart(), b = tab->GetSelectionEnd();
                if (a < 0 || b <= a) return QJsonObject {{"error", tr("请先在源码编辑器选择文本。")}};
                args["selection_start"] = a; args["selection_end"] = b;
            }
        }
        if (ids.isEmpty()) return QJsonObject {{"error", tr("请选择 XHTML 文件或正文选区。")}};
        args["resource_ids"] = ids;
        return args;
    });
    connect(m_TranslationPanel, &TranslationPanel::settingsRequested, this, [this]() {
        PreferencesDialog(); m_TranslationPanel->refreshModels();
    });
    connect(m_TranslationPanel, &TranslationPanel::locationRequested, this, [this](const QString &id, int line) {
        if (auto *resource = m_Book->GetFolderKeeper()->GetResourceByIdentifier(id)) OpenFile(resource->GetRelativePath(), line);
    });
    connect(controller, &TranslationController::bookModified, this, [this]() {
        m_Book->SetModified(true); m_BookBrowser->Refresh(); UpdateAgentContext();
    });
    connect(controller, &TranslationController::finished, this, [this]() {
        if (m_TranslationController->status().value("phase").toString() != "format") {
            m_ValidationResultsView->LoadResults(m_ValidationResultsView->validateXhtml());
        }
        UpdateAgentContext();
    });
    m_AgentController->setExtraTools([controller](SigilAgent::ToolRegistry *registry) { SigilAgent::registerTranslationTools(registry, controller); });
    addDockWidget(Qt::RightDockWidgetArea, m_TranslationPanel);
    tabifyDockWidget(m_PreviewWindow, m_TranslationPanel);
    auto *action = ui.menuEnhancement->addAction(tr("翻译…"));
    action->setObjectName(QStringLiteral("actionTranslateBook"));
    connect(action, &QAction::triggered, this, [this]() { m_TranslationPanel->refreshModels(); m_TranslationPanel->show(); m_TranslationPanel->raise(); });
    ui.menuView->addAction(m_TranslationPanel->toggleViewAction());
    m_TranslationPanel->hide();
}
