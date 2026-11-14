#include "BuiltinPlugins/Translation/TranslationPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include "Agent/Model/AgentModelCatalog.h"
#include "Agent/Persistence/AgentSettings.h"
#include "BuiltinPlugins/Translation/TranslationController.h"

namespace BuiltinPlugins::Translation {

TranslationPanel::TranslationPanel(TranslationController *controller, SigilAgent::IBookWorkspace *workspace, QWidget *parent)
    : QDockWidget(tr("翻译"), parent), m_controller(controller), m_workspace(workspace)
{
    setObjectName(QStringLiteral("translationDock"));
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    auto *form = new QFormLayout();
    m_scope = new QComboBox(content); m_scope->setObjectName("translationScope");
    m_scope->addItem(tr("全书"), "book"); m_scope->addItem(tr("当前文件"), "current");
    m_scope->addItem(tr("所选文件"), "files"); m_scope->addItem(tr("编辑器选区（完整段落）"), "selection");
    m_model = new QComboBox(content); m_model->setObjectName("translationModel");
    m_model->setEditable(true); m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->setMinimumContentsLength(16); m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_thinking = new QComboBox(content); m_thinking->setObjectName("translationThinking");
    m_concurrency = new QSpinBox(content); m_concurrency->setRange(1, 8); m_concurrency->setObjectName("translationConcurrency");
    m_preset = new QComboBox(content); m_preset->setObjectName("translationPreset");
    m_preset->addItem(tr("中文阅读：横排、LTR、图片适配"), "chinese");
    m_preset->addItem(tr("保留原书格式"), "preserve");
    m_preset->setMinimumContentsLength(16); m_preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    form->addRow(tr("范围"), m_scope); form->addRow(tr("翻译模型"), m_model);
    form->addRow(tr("翻译思考"), m_thinking); form->addRow(tr("并发"), m_concurrency); form->addRow(tr("格式"), m_preset);
    layout->addLayout(form);
    auto *settings = new QPushButton(tr("Agent 连接设置…"), content);
    layout->addWidget(settings);
    m_scan = new QCheckBox(tr("先扫描全文、聚合人物术语与记忆"), content); m_scan->setChecked(true);
    m_review = new QCheckBox(tr("翻译后批量校正全部范围"), content); m_review->setChecked(true);
    layout->addWidget(m_scan); layout->addWidget(m_review);
    auto *buttons = new QHBoxLayout();
    m_start = new QPushButton(tr("开始双语翻译"), content); m_start->setObjectName("translationStart");
    m_pause = new QPushButton(tr("暂停"), content); m_pause->setObjectName("translationPause");
    m_stop = new QPushButton(tr("停止"), content); m_stop->setObjectName("translationStop");
    buttons->addWidget(m_start); buttons->addWidget(m_pause); buttons->addWidget(m_stop); layout->addLayout(buttons);
    auto *retry = new QPushButton(tr("只补缺译并校正"), content); layout->addWidget(retry);
    m_progress = new QLabel(content); m_progress->setObjectName("translationProgress"); m_progress->setWordWrap(true);
    layout->addWidget(m_progress);
    auto *tabs = new QTabWidget(content);
    m_terms = new QPlainTextEdit(tabs); m_terms->setObjectName("translationGlossary");
    m_memory = new QPlainTextEdit(tabs); m_memory->setObjectName("translationMemory");
    m_plan = new QPlainTextEdit(tabs); m_plan->setObjectName("translationPlan");
    tabs->addTab(m_terms, tr("术语 JSON")); tabs->addTab(m_memory, tr("临时记忆")); tabs->addTab(m_plan, tr("计划 JSON"));
    layout->addWidget(tabs, 1);
    auto *apply = new QPushButton(tr("应用上方临时资料"), content); layout->addWidget(apply);
    auto *imports = new QHBoxLayout();
    m_misc = new QComboBox(content); m_misc->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto *load = new QPushButton(tr("读取词表"), content);
    imports->addWidget(m_misc, 1); imports->addWidget(load); layout->addLayout(imports);
    auto *save = new QPushButton(tr("保存术语表到 Misc"), content); save->setObjectName("translationSaveGlossary"); layout->addWidget(save);
    m_notice = new QTextBrowser(content); m_notice->setObjectName("translationNotice");
    m_notice->setOpenLinks(false); m_notice->setMaximumHeight(120);
    connect(m_notice, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "sigil-translation") emit locationRequested(url.path(), url.fragment().toInt());
    });
    layout->addWidget(m_notice);
    auto *help = new QLabel(tr("批次直接修改当前打开的书，默认原文后加译文。使用 Sigil 保存 EPUB；计划和记忆只在本窗口有效。"), content);
    help->setWordWrap(true); layout->addWidget(help); setWidget(content);
    connect(settings, &QPushButton::clicked, this, &TranslationPanel::settingsRequested);
    connect(m_start, &QPushButton::clicked, this, &TranslationPanel::start);
    connect(m_stop, &QPushButton::clicked, this, [this]() { m_controller->control({{"action", "stop"}}); });
    connect(m_pause, &QPushButton::clicked, this, [this]() {
        const bool paused = m_controller->status().value("state").toString() == "paused";
        auto result = m_controller->control({{"action", paused ? "resume" : "pause"}}); showResult(result.ok, result.message);
    });
    connect(retry, &QPushButton::clicked, this, [this]() {
        auto result = m_controller->control({{"action", "retry_failed"}}); showResult(result.ok, result.message);
    });
    connect(apply, &QPushButton::clicked, this, &TranslationPanel::applyContext);
    connect(load, &QPushButton::clicked, this, &TranslationPanel::importGlossary);
    connect(save, &QPushButton::clicked, this, [this]() {
        auto result = m_controller->saveGlossary();
        showResult(result.ok, result.ok ? tr("词表已加入当前书：%1；请正常保存 EPUB。").arg(result.data.value("book_path").toString()) : result.message);
    });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this]() { m_preset->setCurrentIndex(m_scope->currentData() == "book" ? 0 : 1); });
    connect(m_model, &QComboBox::currentTextChanged, this, [this]() { if (!m_loading) { updateThinking(); saveOptions(); } });
    connect(m_thinking, &QComboBox::currentIndexChanged, this, [this]() { if (!m_loading) saveOptions(); });
    connect(m_concurrency, &QSpinBox::valueChanged, this, [this]() { if (!m_loading) saveOptions(); });
    connect(controller, &TranslationController::changed, this, [this]() {
        if (m_refreshPending) return;
        m_refreshPending = true;
        QTimer::singleShot(100, this, [this]() { m_refreshPending = false; refresh(); });
    });
    refreshModels(); refresh();
}

void TranslationPanel::setScopeProvider(std::function<QJsonObject(const QString &)> provider) { m_scopeProvider = std::move(provider); }

void TranslationPanel::refreshModels()
{
    m_loading = true;
    SigilAgent::AgentSettings settings;
    const auto catalog = SigilAgent::AgentModelCatalog::fromCacheJson(QJsonDocument::fromJson(settings.catalogJson().toUtf8()).object());
    m_model->clear(); m_model->addItem(tr("沿用 Agent 模型"), QString());
    for (const auto &model : catalog.models) m_model->addItem(model.id, model.id);
    const int index = m_model->findData(settings.translationModel());
    if (index >= 0) m_model->setCurrentIndex(index); else m_model->setEditText(settings.translationModel());
    m_concurrency->setValue(settings.translationConcurrency());
    updateThinking();
    m_loading = false;
}

void TranslationPanel::updateThinking()
{
    const QSignalBlocker blocker(m_thinking);
    SigilAgent::AgentSettings settings;
    const QString previous = m_thinking->currentData().toString().isEmpty() ? settings.translationThinking() : m_thinking->currentData().toString();
    m_thinking->clear(); m_thinking->addItem(tr("自动：优先关闭思考"), "auto"); m_thinking->addItem(tr("沿用 Agent 思考"), "inherit");
    const QString selected = m_model->currentIndex() == 0 && m_model->currentText() == m_model->itemText(0)
        ? settings.model() : m_model->currentText().trimmed();
    const auto catalog = SigilAgent::AgentModelCatalog::fromCacheJson(QJsonDocument::fromJson(settings.catalogJson().toUtf8()).object());
    for (const auto &model : catalog.models) if (model.id == selected) {
        for (const auto &effort : model.supportedReasoningEfforts) if (effort != "none") m_thinking->addItem(effort, effort);
    }
    const int index = m_thinking->findData(previous); m_thinking->setCurrentIndex(qMax(0, index));
}

void TranslationPanel::saveOptions()
{
    SigilAgent::AgentSettings settings;
    settings.setTranslationModel(m_model->currentIndex() == 0 && m_model->currentText() == m_model->itemText(0) ? QString() : m_model->currentText());
    settings.setTranslationThinking(m_thinking->currentData().toString()); settings.setTranslationConcurrency(m_concurrency->value());
}

void TranslationPanel::start()
{
    saveOptions();
    const auto scope = m_scopeProvider ? m_scopeProvider(m_scope->currentData().toString()) : QJsonObject {};
    if (scope.contains("error")) { showResult(false, scope.value("error").toString()); return; }
    auto result = m_controller->prepare(scope);
    if (!result.ok) { showResult(false, result.message); return; }
    QJsonArray stages;
    if (m_scan->isChecked()) { stages.append("scan"); stages.append("aggregate"); }
    stages.append("translate"); if (m_review->isChecked()) stages.append("review"); stages.append("format");
    result = m_controller->run({{"stages", stages}, {"preset", m_preset->currentData().toString()}});
    showResult(result.ok, result.ok ? tr("已开始；成功批次将直接写入当前书。") : result.message);
}

void TranslationPanel::showResult(bool ok, const QString &message)
{
    m_notice->setPlainText((ok ? QString() : tr("未执行：")) + message);
}

void TranslationPanel::refreshContext(bool force)
{
    if (m_controller->contextId().isEmpty()) return;
    if (force || !m_terms->document()->isModified()) {
        QJsonArray terms; int offset = 0;
        while (true) {
            auto result = m_controller->context({{"kind", "glossary"}, {"offset", offset}, {"limit", 200}});
            if (!result.ok) break;
            for (const auto &term : result.data.value("terms").toArray()) terms.append(term);
            if (!result.data.value("has_more").toBool()) break;
            offset = result.data.value("next_offset").toInt();
        }
        const QString text = QString::fromUtf8(QJsonDocument(terms).toJson(QJsonDocument::Indented));
        if (text != m_terms->toPlainText()) m_terms->setPlainText(text);
        m_terms->document()->setModified(false);
    }
    if (force || !m_memory->document()->isModified()) {
        const QString text = m_controller->context({{"kind", "memory"}}).data.value("memory").toString();
        if (text != m_memory->toPlainText()) m_memory->setPlainText(text);
        m_memory->document()->setModified(false);
    }
    if (force || !m_plan->document()->isModified()) {
        const QString text = QString::fromUtf8(QJsonDocument(m_controller->context({{"kind", "plan"}}).data.value("plan").toObject()).toJson(QJsonDocument::Indented));
        if (text != m_plan->toPlainText()) m_plan->setPlainText(text);
        m_plan->document()->setModified(false);
    }
}

void TranslationPanel::refresh()
{
    const auto state = m_controller->status();
    const QString name = state.value("state").toString();
    m_start->setEnabled(!m_controller->running()); m_scope->setEnabled(!m_controller->running());
    m_pause->setEnabled(m_controller->running()); m_stop->setEnabled(m_controller->running());
    m_pause->setText(name == "paused" ? tr("继续") : tr("暂停"));
    m_progress->setText(tr("%1 · %2\n扫描 %3/%4 · 已译 %5 · 校正 %6 · 失败 %7\n请求 %8 · 在途 %9 · 输入/输出 token %10/%11 · 本地复用 %12\n用量未完整返回 %13 次 · 供应商缓存输入 %14 token")
        .arg(name, state.value("phase").toString()).arg(state.value("scanned").toInt()).arg(state.value("paragraphs").toInt())
        .arg(state.value("translated").toInt()).arg(state.value("reviewed").toInt()).arg(state.value("failed").toInt())
        .arg(state.value("requests").toInt()).arg(state.value("in_flight").toInt())
        .arg(state.value("input_tokens").toInteger()).arg(state.value("output_tokens").toInteger()).arg(state.value("local_cache_hits").toInt())
        .arg(state.value("unknown_usage_requests").toInt()).arg(state.value("cached_input_tokens").toInteger()));
    const QString id = m_controller->contextId();
    if (id.isEmpty() && !m_contextId.isEmpty()) { m_terms->clear(); m_memory->clear(); m_plan->clear(); }
    m_contextId = id;
    refreshContext();
    const QString previous = m_misc->currentData().toString();
    m_misc->clear(); m_misc->addItem(tr("选择 JSON 文件…"), QString());
    for (const auto &row : m_workspace->resources()) {
        const auto resource = row.toObject(); const QString path = resource.value("book_path").toString();
        if (path.endsWith(".json", Qt::CaseInsensitive)) m_misc->addItem(path, resource.value("resource_id").toString());
    }
    const int index = m_misc->findData(previous); if (index >= 0) m_misc->setCurrentIndex(index);
    QStringList errors;
    for (const auto &message : state.value("messages").toArray()) errors.append(message.toString().toHtmlEscaped());
    for (const auto &problem : state.value("problems").toArray()) {
        const auto issue = problem.toObject();
        QUrl link; link.setScheme("sigil-translation"); link.setPath(issue.value("resource_id").toString()); link.setFragment(QString::number(issue.value("line").toInt()));
        errors.append(QStringLiteral("<a href=\"%1\">%2</a>: %3").arg(link.toString().toHtmlEscaped(),
            issue.value("book_path").toString().toHtmlEscaped(), issue.value("message").toString().toHtmlEscaped()));
    }
    if (!errors.isEmpty()) m_notice->setHtml(errors.join(QStringLiteral("<br/>")));
}

void TranslationPanel::applyContext()
{
    if (m_controller->contextId().isEmpty()) {
        const auto scope = m_scopeProvider ? m_scopeProvider(m_scope->currentData().toString()) : QJsonObject {};
        if (scope.contains("error")) { showResult(false, scope.value("error").toString()); return; }
        auto result = m_controller->prepare(scope); if (!result.ok) { showResult(false, result.message); return; }
    }
    const QString termsText = m_terms->toPlainText(), memoryText = m_memory->toPlainText(), planText = m_plan->toPlainText();
    QJsonParseError error;
    auto terms = QJsonDocument::fromJson(termsText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || (!terms.isArray() && !terms.isObject())) { showResult(false, tr("术语必须是合法 JSON 数组或含 terms 的对象。")); return; }
    QJsonDocument plan;
    if (!planText.trimmed().isEmpty()) {
        plan = QJsonDocument::fromJson(planText.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !plan.isObject()) { showResult(false, tr("计划必须是合法 JSON 对象。")); return; }
    }
    auto result = m_controller->context({{"kind", "glossary"}, {"action", "update"}, {"value", terms.isArray() ? QJsonValue(terms.array()) : QJsonValue(terms.object())}});
    if (!result.ok) { showResult(false, result.message); return; }
    m_controller->context({{"kind", "memory"}, {"action", "update"}, {"value", memoryText}});
    if (!plan.isNull()) {
        m_controller->context({{"kind", "plan"}, {"action", "update"}, {"value", plan.object()}});
    }
    refreshContext(true); showResult(true, tr("已更新当前窗口临时资料。"));
}

void TranslationPanel::importGlossary()
{
    QByteArray bytes;
    const QString resource = m_misc->currentData().toString();
    if (!resource.isEmpty()) bytes = m_workspace->resourceText(resource).toUtf8();
    else {
        const QString path = QFileDialog::getOpenFileName(this, tr("读取词表"), {}, tr("JSON (*.json)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) { showResult(false, tr("无法读取词表，或文件超过 8 MiB。")); return; }
        bytes = file.readAll();
    }
    QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject())) { showResult(false, tr("词表 JSON 无效。")); return; }
    m_terms->setPlainText(QString::fromUtf8(document.toJson(QJsonDocument::Indented))); m_terms->document()->setModified(true);
    if (m_controller->contextId().isEmpty()) {
        const auto scope = m_scopeProvider ? m_scopeProvider(m_scope->currentData().toString()) : QJsonObject {};
        if (scope.contains("error")) { showResult(false, scope.value("error").toString()); return; }
        auto result = m_controller->prepare(scope); if (!result.ok) { showResult(false, result.message); return; }
    }
    auto result = m_controller->context({{"kind", "glossary"}, {"action", "update"},
        {"value", document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object())}});
    showResult(result.ok, result.ok ? tr("词表已载入当前窗口。") : result.message);
    if (result.ok) refreshContext(true);
}

} // namespace BuiltinPlugins::Translation
