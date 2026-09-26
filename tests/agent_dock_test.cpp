#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include "Agent/UI/AgentDock.h"
#include "Agent/UI/AgentMarkdown.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void ProcessEventsFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QString VisibleText(const QLabel *label)
{
    if (!label) return QString();
    if (label->textFormat() != Qt::RichText) return label->text();
    QTextDocument document;
    document.setHtml(label->text());
    return document.toPlainText();
}

class FakeLocationSource final : public SigilAgent::AgentLocationSource
{
public:
    QString session = QStringLiteral("nav-book-session");
    QList<SigilAgent::AgentLocationResource> resources;
    QHash<QString, QString> texts;

    QString bookSessionId() const override { return session; }
    QList<SigilAgent::AgentLocationResource> textResources() const override { return resources; }
    bool resourceText(const QString &resource_id, QString *book_path, QString *text) const override
    {
        for (const SigilAgent::AgentLocationResource &resource : resources) {
            if (resource.resourceId != resource_id || !texts.contains(resource_id)) continue;
            if (book_path) *book_path = resource.bookPath;
            if (text) *text = texts.value(resource_id);
            return true;
        }
        return false;
    }
};

QString NumberedLines(int count)
{
    QStringList lines;
    for (int i = 1; i <= count; ++i) lines.append(QStringLiteral("<p>source line %1</p>").arg(i));
    return lines.join(QLatin1Char('\n'));
}

QString HrefForLabel(const QLabel *label, const QString &text)
{
    const QRegularExpression pattern(
        QStringLiteral("href=\"(sigil-agent://location/[0-9a-f]{32})\"[^>]*>(?:<span[^>]*>)?%1<")
            .arg(QRegularExpression::escape(text)));
    return pattern.match(label ? label->text() : QString()).captured(1);
}

struct OpenRequest {
    QString bookSessionId;
    QString resourceId;
    int line = 0;
};

void TestMarkdownNavigation(QApplication &application)
{
    FakeLocationSource source;
    source.resources = {
        { QStringLiteral("res-ch1"), QStringLiteral("OEBPS/Text/Section001.xhtml") },
        { QStringLiteral("res-ch2"), QStringLiteral("OEBPS/Text/Section002.xhtml") }
    };
    source.texts.insert(QStringLiteral("res-ch1"), NumberedLines(2300));
    source.texts.insert(QStringLiteral("res-ch2"), NumberedLines(40));

    SigilAgent::AgentDock dock;
    dock.setSessionId(QStringLiteral("nav-session"));
    dock.setBookContext(QStringLiteral("Book"), QStringLiteral("book.epub"), 2, false, 1,
                        source.session);
    dock.setLocationSource(&source);
    dock.resize(360, 700);
    dock.show();
    application.processEvents();

    QList<OpenRequest> requests;
    QObject::connect(&dock, &SigilAgent::AgentDock::openLocationRequested, &dock,
                     [&requests](const QString &book_session_id, const QString &resource_id,
                                 int line) {
                         requests.append({ book_session_id, resource_id, line });
                     });

    SigilAgent::AgentEvent user;
    user.type = SigilAgent::AgentEventType::UserMessage;
    user.payload = QJsonObject { { QStringLiteral("text"),
                                   QStringLiteral("Please **proofread** `chapter 1` <b>now</b>") } };
    dock.appendEvent(user);
    QWidget *user_card = dock.findChild<QWidget *>(QStringLiteral("agentUserCard"));
    auto *user_body = user_card
        ? user_card->findChild<QLabel *>(QStringLiteral("agentUserCardBody")) : nullptr;
    Require(user_body && user_card->property("markdownState").toString() == QStringLiteral("rendered")
                && VisibleText(user_body).contains(QStringLiteral("Please proofread chapter 1 <b>now</b>"))
                && user_card->property("rawMarkdown").toString()
                    == QStringLiteral("Please **proofread** `chapter 1` <b>now</b>"),
            "user text must render as Markdown with raw HTML shown literally");

    SigilAgent::AgentEvent step;
    step.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step);
    const QString answer = QString::fromUtf8(
        "已通读第一章全文。**目前尚未写入任何修改。**\n\n"
        "**范围**：`OEBPS/Text/Section001.xhtml`（第一章）\n"
        "**自动审计**：`proof.audit` 默认规则 **0 项**；章节以 `<p><br /></p>` 分隔。\n\n"
        "## 一、建议修改（6 项）\n\n"
        "| # | 行 | 原文 | 建议 | 依据 |\n|---|---|---|---|---|\n"
        "| 1 | L25 | 也**帮忙我**发传单 | **帮我**发传单 | 同章 L75、L27 |\n"
        "| 2 | L477 | 「**暗椿**」 | 「**暗桩**」 | 错字 |\n"
        "| 3 | L925 | **指是**什么 | 是什么 | 衍字 |\n"
        "| 4 | L1409 | **并下无意识** | **并下意识** | 错字 |\n"
        "| 5 | L1897 | **小事一椿** | 小事一**桩** | 错字 |\n"
        "| 6 | L2219 | **向我地**道歉 | **向我**道歉 | 衍字 |\n\n"
        "## 二、需你决定\n\n"
        "7. **L1899**「帮忙成香的演讲」\n8. **L789**「显着」\n"
        "9. **L1513 / L1833 / L1895 / L2031**「想像」\n10. **L1161**「能在」\n\n"
        "## 三、其他观察\n\n- L2043 用「•」表示并列。\n\n---\n\n"
        "**下一步**：`transaction.begin → patch → preview → commit`。\n");
    const QStringList deltas = { answer.left(40), answer.mid(40, 200), answer.mid(240) };
    for (const QString &delta : deltas) {
        SigilAgent::AgentEvent event;
        event.type = SigilAgent::AgentEventType::AssistantDelta;
        event.payload = QJsonObject { { QStringLiteral("kind"), QStringLiteral("content") },
                                      { QStringLiteral("text"), delta } };
        dock.appendEvent(event);
    }
    ProcessEventsFor(80);
    QWidget *answer_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    auto *answer_body = answer_card
        ? answer_card->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody")) : nullptr;
    Require(answer_body && answer_body->textFormat() == Qt::PlainText
                && answer_body->text() == answer
                && answer_card->property("markdownState").toString() == QStringLiteral("streaming"),
            "streamed deltas must stay plain selectable source until the final message");

    SigilAgent::AgentEvent final_answer;
    final_answer.type = SigilAgent::AgentEventType::AssistantMessage;
    final_answer.payload = QJsonObject { { QStringLiteral("content"), answer } };
    dock.appendEvent(final_answer);
    application.processEvents();
    const QString html = answer_body->text();
    const QString visible = VisibleText(answer_body);
    Require(answer_body->textFormat() == Qt::RichText
                && answer_card->property("markdownState").toString() == QStringLiteral("rendered")
                && answer_card->property("rawMarkdown").toString() == answer
                && html.contains(QStringLiteral("<table"))
                && visible.contains(QString::fromUtf8("一、建议修改"))
                && visible.contains(QString::fromUtf8("帮忙我"))
                && visible.contains(QStringLiteral("<p><br /></p>"))
                && !visible.contains(QStringLiteral("**")),
            "the final answer must render headings, tables, emphasis and code once");
    const QString screenshot = qEnvironmentVariable("SIGIL_AGENT_DOCK_SCREENSHOT");
    if (!screenshot.isEmpty()) dock.grab().save(screenshot);
    Require(answer_card->property("locationFileLinks").toInt() == 1
                && answer_card->property("locationLineLinks").toInt() == 16
                && answer_card->property("locationUnboundLineRefs").toInt() == 0
                && answer_card->property("locationOutOfRangeLineRefs").toInt() == 0,
            "the sample-shaped answer must link its one file and all 16 source-line references");
    Require(answer_body->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard)
                && answer_body->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)
                && !answer_body->openExternalLinks()
                && answer_body->accessibleName() == QStringLiteral("Answer"),
            "rendered answers must stay selectable, keyboard reachable and never open external links");

    auto *copy = answer_card->findChild<QToolButton *>(QStringLiteral("agentAnswerCardCopy"));
    Require(copy, "rendered cards must offer Copy for the original Markdown");
    copy->click();
    Require(QGuiApplication::clipboard()->text() == answer,
            "Copy must place the original Markdown, not rendered HTML, on the clipboard");

    const QString l477 = HrefForLabel(answer_body, QStringLiteral("L477"));
    const QString file_link = HrefForLabel(answer_body, QStringLiteral("OEBPS/Text/Section001.xhtml"));
    Require(!l477.isEmpty() && !file_link.isEmpty(), "rendered HTML must carry the location links");
    emit answer_body->linkHovered(l477);
    Require(answer_body->toolTip().contains(QStringLiteral("OEBPS/Text/Section001.xhtml"))
                && answer_body->toolTip().contains(QStringLiteral("477")),
            "hovering a line link must say which file and source line will open");
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 1 && requests.last().bookSessionId == source.session
                && requests.last().resourceId == QStringLiteral("res-ch1")
                && requests.last().line == 477
                && dock.property("lastLocationStatus").toString() == QStringLiteral("exact"),
            "an unchanged source-line link must request that resource and line");
    emit answer_body->linkActivated(file_link);
    Require(requests.size() == 2 && requests.last().resourceId == QStringLiteral("res-ch1")
                && requests.last().line == -1,
            "a file link must open the file without inventing a line");

    auto *notice = dock.findChild<QWidget *>(QStringLiteral("agentLocationNotice"));
    auto *notice_text = dock.findChild<QLabel *>(QStringLiteral("agentLocationNoticeText"));
    auto *open_file = dock.findChild<QPushButton *>(QStringLiteral("agentLocationOpenFileButton"));
    Require(notice && notice_text && open_file && !notice->isVisible(),
            "successful navigation must not show a location notice");

    const QString original = source.texts.value(QStringLiteral("res-ch1"));
    source.texts[QStringLiteral("res-ch2")] = NumberedLines(41);
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 3 && requests.last().line == 477,
            "editing another resource must not invalidate this link");
    source.texts[QStringLiteral("res-ch1")] = QStringLiteral("<p>new</p>\n") + original;
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 3 && notice->isVisible() && open_file->isVisible()
                && notice->property("locationStatus").toString() == QStringLiteral("content_changed")
                && notice_text->text().contains(QStringLiteral("477")),
            "a changed target must not jump to the stale line and must explain why");
    open_file->click();
    Require(requests.size() == 4 && requests.last().resourceId == QStringLiteral("res-ch1")
                && requests.last().line == -1 && !notice->isVisible(),
            "the user may still open only the changed file");
    source.texts[QStringLiteral("res-ch1")] = original;
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 5 && requests.last().line == 477,
            "undoing the change must restore exact navigation");

    source.resources[0].bookPath = QStringLiteral("OEBPS/Text/Renamed.xhtml");
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 6 && requests.last().resourceId == QStringLiteral("res-ch1"),
            "a renamed resource must still resolve by identity");
    source.texts.remove(QStringLiteral("res-ch1"));
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 6 && notice->isVisible() && !open_file->isVisible()
                && notice->property("locationStatus").toString() == QStringLiteral("resource_missing"),
            "a deleted resource must not open anything");
    source.texts.insert(QStringLiteral("res-ch1"), original);
    source.resources[0].bookPath = QStringLiteral("OEBPS/Text/Section001.xhtml");

    emit answer_body->linkActivated(QStringLiteral("sigil-agent://location/0123456789abcdef0123456789abcdef"));
    emit answer_body->linkActivated(QStringLiteral("file:///etc/passwd"));
    Require(requests.size() == 6 && dock.property("lastLocationStatus").toString() == QStringLiteral("unknown"),
            "IDs the host did not issue and foreign schemes must never navigate");

    SigilAgent::AgentEvent next_step;
    next_step.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(next_step);
    SigilAgent::AgentEvent multi;
    multi.type = SigilAgent::AgentEventType::AssistantMessage;
    multi.payload = QJsonObject { { QStringLiteral("content"), QStringLiteral(
        "`OEBPS/Text/Section001.xhtml` and `OEBPS/Text/Section002.xhtml`: L3 is ambiguous.\n\n"
        "| file | line |\n|---|---|\n| OEBPS/Text/Section002.xhtml | L40 |\n\n"
        "```\nOEBPS/Text/Section001.xhtml L5\n```\n") } };
    dock.appendEvent(multi);
    QWidget *multi_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard1-2"));
    auto *multi_body = multi_card
        ? multi_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-2Body")) : nullptr;
    Require(multi_card && multi_card->property("locationFileLinks").toInt() == 3
                && multi_card->property("locationLineLinks").toInt() == 1
                && multi_card->property("locationUnboundLineRefs").toInt() == 1,
            "multi-file answers must bind L numbers only through their own table row");
    emit multi_body->linkActivated(HrefForLabel(multi_body, QStringLiteral("L40")));
    Require(requests.size() == 7 && requests.last().resourceId == QStringLiteral("res-ch2")
                && requests.last().line == 40,
            "a row-bound L number must open the path named in that row");

    SigilAgent::AgentEvent third_step;
    third_step.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(third_step);
    SigilAgent::AgentEvent unsafe;
    unsafe.type = SigilAgent::AgentEventType::AssistantMessage;
    unsafe.payload = QJsonObject { { QStringLiteral("content"), QStringLiteral(
        "<img src=\"file:///etc/hosts\"> ![x](https://example.com/x.png) "
        "[web](https://example.com) [js](javascript:alert(1))") } };
    dock.appendEvent(unsafe);
    QWidget *unsafe_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard1-3"));
    auto *unsafe_body = unsafe_card
        ? unsafe_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-3Body")) : nullptr;
    Require(unsafe_body && !unsafe_body->text().contains(QStringLiteral("<img"))
                && !unsafe_body->text().contains(QStringLiteral("href=\"http"))
                && !unsafe_body->text().contains(QStringLiteral("javascript:"))
                && unsafe_card->property("markdownBlockedImages").toInt() == 1
                && unsafe_card->property("markdownBlockedLinks").toInt() == 2
                && VisibleText(unsafe_body).contains(QStringLiteral("<img src=")),
            "model HTML, images and external links must not become live rich text");

    SigilAgent::AgentEvent tool;
    tool.type = SigilAgent::AgentEventType::ToolCompleted;
    tool.payload = QJsonObject { { QStringLiteral("tool_call_id"), QStringLiteral("html-tool") },
                                 { QStringLiteral("name"), QStringLiteral("resource.read_fragment") },
                                 { QStringLiteral("message"), QStringLiteral("<b>not bold</b>") } };
    dock.appendEvent(tool);
    auto *tool_body = dock.findChild<QLabel *>(QStringLiteral("agentToolCard-html-toolBody"));
    Require(tool_body && tool_body->textFormat() == Qt::PlainText
                && tool_body->text() == QStringLiteral("<b>not bold</b>"),
            "tool cards must stay plain text");

    SigilAgent::AgentEvent fourth_step;
    fourth_step.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(fourth_step);
    SigilAgent::AgentEvent huge;
    huge.type = SigilAgent::AgentEventType::AssistantMessage;
    const QString huge_text = QStringLiteral("**x** ").repeated(SigilAgent::AGENT_MARKDOWN_RENDER_BUDGET / 6 + 10)
        + QStringLiteral(" OEBPS/Text/Section001.xhtml L1");
    huge.payload = QJsonObject { { QStringLiteral("content"), huge_text } };
    dock.appendEvent(huge);
    QWidget *huge_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard1-4"));
    auto *huge_body = huge_card
        ? huge_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-4Body")) : nullptr;
    auto *huge_note = huge_card
        ? huge_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-4PlainNote")) : nullptr;
    Require(huge_body && huge_note && huge_body->textFormat() == Qt::PlainText
                && huge_body->text() == huge_text && !huge_note->isHidden()
                && huge_card->property("markdownState").toString() == QStringLiteral("plain_over_budget")
                && huge_card->property("locationLineLinks").toInt() == 0
                && (huge_body->textInteractionFlags() & Qt::TextSelectableByKeyboard),
            "an answer above the render budget must stay copyable plain text with a notice");

    source.session = QStringLiteral("another-book-session");
    dock.setBookContext(QStringLiteral("Other"), QStringLiteral("other.epub"), 2, false, 1,
                        source.session);
    emit answer_body->linkActivated(l477);
    Require(requests.size() == 7 && notice->isVisible() && !open_file->isVisible()
                && notice->property("locationStatus").toString() == QStringLiteral("other_book"),
            "after the book changes, old links must not open the new book");

    dock.resetTranscript();
    Require(!notice->isVisible(), "a new transcript must clear the location notice");
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication application(argc, argv);
    SigilAgent::AgentDock dock;
    dock.setSessionId(QStringLiteral("session-full-id"));
    dock.show();
    application.processEvents();

    auto *mode = dock.findChild<QComboBox *>(QStringLiteral("agentModeCombo"));
    auto *stop = dock.findChild<QPushButton *>(QStringLiteral("agentStopButton"));
    auto *fresh = dock.findChild<QPushButton *>(QStringLiteral("agentNewSessionButton"));
    auto *composer = dock.findChild<QPlainTextEdit *>(QStringLiteral("agentComposer"));
    auto *transcript = dock.findChild<QScrollArea *>(QStringLiteral("agentTranscript"));
    Require(!dock.findChild<QLineEdit *>(QStringLiteral("agentModelEdit")),
            "Agent dock must not let the user type a model name");
    Require(dock.findChild<QLabel *>(QStringLiteral("agentModelLabel")),
            "Agent dock must show the model chosen in Preferences");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentExportButton")),
            "Agent dock must offer export");
    dock.setModelName(QStringLiteral("deepseek-chat"));
    auto *model_label = dock.findChild<QLabel *>(QStringLiteral("agentModelLabel"));
    Require(model_label && model_label->text().contains(QStringLiteral("deepseek-chat")),
            "dock model label must show the settings model");
    auto *provider_status = dock.findChild<QLabel *>(QStringLiteral("agentProviderStatus"));
    Require(provider_status, "dock must expose provider setup and request status");
    auto *retry_button = dock.findChild<QPushButton *>(QStringLiteral("agentRetryButton"));
    Require(!provider_status->isVisible()
                && !dock.findChild<QWidget *>(QStringLiteral("agentProviderRow"))
                && retry_button && retry_button->isVisible(),
            "provider status must only appear in technical details while Retry stays visible");
    const auto provider_status_in_details = [&dock, provider_status]() {
        auto *details = dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
        return details && !provider_status->text().isEmpty()
            && details->text().endsWith(provider_status->text());
    };
    SigilAgent::AgentProviderReadiness setup_required;
    setup_required.kind = SigilAgent::AgentProviderKind::DeepSeek;
    setup_required.displayName = QStringLiteral("DeepSeek");
    setup_required.model = QStringLiteral("deepseek-chat");
    setup_required.endpointHost = QStringLiteral("api.deepseek.com");
    setup_required.issue = SigilAgent::AgentProviderSetupIssue::ApiKey;
    dock.setProviderConfiguration(setup_required);
    Require(provider_status->text().contains(QStringLiteral("Setup required: API key"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("setup_required"),
            "missing provider settings must be explicit without claiming connectivity");
    Require(provider_status_in_details(),
            "technical details must show the provider setup status");

    SigilAgent::AgentProviderReadiness configured = setup_required;
    configured.issue = SigilAgent::AgentProviderSetupIssue::None;
    dock.setProviderConfiguration(configured);
    Require(provider_status->text().contains(QStringLiteral("DeepSeek"))
                && provider_status->text().contains(QStringLiteral("deepseek-chat"))
                && provider_status->text().contains(QStringLiteral("api.deepseek.com"))
                && provider_status->text().contains(QStringLiteral("Configured · not tested"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("configured"),
            "complete settings must say configured but not tested");
    Require(provider_status_in_details(),
            "technical details must include the configured provider, model, and endpoint");
    configured.verifiedAtMs = 1700000000000;
    dock.setProviderConfiguration(configured);
    Require(provider_status->text().contains(QStringLiteral("Chat tested successfully"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("verified")
                && provider_status->property("connectionVerifiedAtMs").toLongLong()
                    == 1700000000000,
            "a matching saved probe must be shown as historical verification");
    Require(provider_status_in_details(),
            "technical details must show the provider verification status");
    SigilAgent::AgentEvent run_started;
    run_started.type = SigilAgent::AgentEventType::RunStateChanged;
    run_started.timestampMs = 1699999999900;
    run_started.payload = QJsonObject {
        { QStringLiteral("run_id"), QStringLiteral("run-full-id") },
        { QStringLiteral("state"), QStringLiteral("preparing_context") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("max_model_steps"), 24 },
        { QStringLiteral("max_tool_calls"), 128 },
        { QStringLiteral("usage_requested"), true }
    };
    dock.appendEvent(run_started);
    auto *run_details =
        dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    Require(run_details
                && run_details->text().contains(QStringLiteral("Whole run: in progress"))
                && run_details->text().contains(
                    QStringLiteral("Model-step budget: limit 24 per run"))
                && run_details->text().contains(
                    QStringLiteral("Tool-call budget: limit 128 per run"))
                && run_details->text().contains(
                    QStringLiteral("Run token usage: awaiting completed requests"))
                && run_details->property("runId").toString()
                    == QStringLiteral("run-full-id")
                && run_details->property("runMaxModelSteps").toInt() == 24
                && run_details->property("runMaxToolCalls").toInt() == 128
                && run_details->property("runDurationMs").toLongLong() == -1,
            "a preparing run must expose its identity without inventing a final duration");
    SigilAgent::AgentEvent provider_started;
    provider_started.type = SigilAgent::AgentEventType::ModelRequestStarted;
    provider_started.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("session_id"), QStringLiteral("session-full-id") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("book_revision"), 6 },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("mode"), QStringLiteral("ask") },
        { QStringLiteral("usage_requested"), true },
        { QStringLiteral("history_context"), QJsonObject {
              { QStringLiteral("limit_enabled"), true },
              { QStringLiteral("budget_bytes"), 32768 },
              { QStringLiteral("total_turn_count"), 4 },
              { QStringLiteral("included_turn_count"), 3 },
              { QStringLiteral("omitted_turn_count"), 1 },
              { QStringLiteral("included_previous_turn_bytes"), 20480 },
              { QStringLiteral("current_turn_bytes"), 5120 },
              { QStringLiteral("included_current_turn_bytes"), 4096 },
              { QStringLiteral("current_turn_budget_bytes"), 131072 },
              { QStringLiteral("omitted_current_turn_messages"), 2 } } },
        { QStringLiteral("tool_context"), QJsonObject {
              { QStringLiteral("mode"), QStringLiteral("ask") },
              { QStringLiteral("policy_applied"), true },
              { QStringLiteral("total_tool_count"), 44 },
              { QStringLiteral("exposed_tool_count"), 17 },
              { QStringLiteral("hidden_tool_count"), 27 },
              { QStringLiteral("hidden_tools"), QJsonArray {
                    QStringLiteral("resource.patch_fragment"),
                    QStringLiteral("transaction.commit") } },
              { QStringLiteral("unfiltered_schema_bytes"), 32768 },
              { QStringLiteral("exposed_schema_bytes"), 12288 },
              { QStringLiteral("saved_schema_bytes"), 20480 } } },
        { QStringLiteral("context_handles"), QJsonArray {
              QStringLiteral("chapter-1:12-34"), QStringLiteral("book-css") } }
    };
    dock.appendEvent(provider_started);
    Require(provider_status->text().contains(QStringLiteral("Contacting provider"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("requesting"),
            "a real model request must move provider status to requesting");
    Require(provider_status_in_details(),
            "technical details must update when a provider request starts");
    SigilAgent::AgentEvent provider_completed;
    provider_completed.type = SigilAgent::AgentEventType::ModelRequestCompleted;
    provider_completed.timestampMs = 1700000000000;
    provider_completed.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("duration_ms"), 27 },
        { QStringLiteral("response_timing"), QJsonObject {
              { QStringLiteral("first_byte_ms"), 9 },
              { QStringLiteral("first_model_event_ms"), 14 } } },
        { QStringLiteral("usage"), QJsonObject {
              { QStringLiteral("input_tokens"), 120 },
              { QStringLiteral("output_tokens"), 35 },
              { QStringLiteral("total_tokens"), 155 },
              { QStringLiteral("cached_input_tokens"), 80 },
              { QStringLiteral("reasoning_tokens"), 12 } } }
    };
    dock.appendEvent(provider_completed);
    Require(provider_status->text().contains(QStringLiteral("Last request succeeded"))
                && provider_status->text().contains(QStringLiteral("27 ms"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("succeeded")
                && provider_status->property("requestId").toString()
                    == QStringLiteral("request-full-id")
                && provider_status->property("durationMs").toLongLong() == 27,
            "only a completed model request may report success with measured timing");
    Require(provider_status_in_details(),
            "technical details must include the provider request result, duration, and time");
    auto *usage_details =
        dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    const QString completion_time = QDateTime::fromMSecsSinceEpoch(
        provider_completed.timestampMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    Require(usage_details && usage_details->text().contains(QStringLiteral("27 ms"))
                && usage_details->text().contains(completion_time),
            "technical details must include the completed provider request timing");
    Require(usage_details
                && usage_details->text().contains(
                    QStringLiteral("Token usage: input 120 · output 35 · total 155"))
                && usage_details->text().contains(
                    QStringLiteral("cached input 80 · reasoning 12"))
                && usage_details->property("usageRequested").toBool()
                && usage_details->property("usageReported").toBool()
                && usage_details->property("totalTokens").toLongLong() == 155
                && usage_details->text().contains(
                    QStringLiteral("Response latency: first byte 9 ms · first model event 14 ms"))
                && usage_details->property("firstByteMs").toLongLong() == 9
                && usage_details->property("firstModelEventMs").toLongLong() == 14
                && usage_details->text().contains(
                    QStringLiteral("Request history: 3/4 turns sent · 1 omitted · previous 20/32 KiB"))
                && usage_details->text().contains(
                    QStringLiteral("Current run history: 4/5 KiB sent · budget 128 KiB · messages omitted: 2"))
                && usage_details->property("historyBudgetBytes").toInt() == 32768
                && usage_details->property("historyIncludedTurns").toInt() == 3
                && usage_details->property("historyOmittedTurns").toInt() == 1
                && usage_details->property("historyIncludedPreviousBytes").toInt()
                    == 20480
                && usage_details->property("historyCurrentTurnBytes").toInt()
                    == 5120
                && usage_details->property("historyIncludedCurrentTurnBytes").toInt()
                    == 4096
                && usage_details->property("historyCurrentTurnBudgetBytes").toInt()
                    == 131072
                && usage_details->property("historyOmittedCurrentTurnMessages").toInt()
                    == 2
                && usage_details->text().contains(
                    QStringLiteral("Request tools: 17/44 exposed · 27 hidden by mode policy · schema 12/32 KiB"))
                && usage_details->property("toolTotalCount").toInt() == 44
                && usage_details->property("toolExposedCount").toInt() == 17
                && usage_details->property("toolHiddenCount").toInt() == 27
                && usage_details->property("toolSchemaBytes").toInt() == 12288
                && usage_details->property("toolSavedSchemaBytes").toInt() == 20480
                && usage_details->property("hiddenToolNames").toList().size() == 2,
            "technical details must expose token, history, and mode tool request budgets");
    SigilAgent::AgentEvent run_completed;
    run_completed.type = SigilAgent::AgentEventType::RunStateChanged;
    run_completed.timestampMs = 1700000000100;
    run_completed.payload = QJsonObject {
        { QStringLiteral("run_id"), QStringLiteral("run-full-id") },
        { QStringLiteral("state"), QStringLiteral("completed") },
        { QStringLiteral("book_session_id"), QStringLiteral("request-book-id") },
        { QStringLiteral("duration_ms"), 91 },
        { QStringLiteral("model_steps"), 2 },
        { QStringLiteral("max_model_steps"), 24 },
        { QStringLiteral("tool_calls"), 1 },
        { QStringLiteral("max_tool_calls"), 128 },
        { QStringLiteral("usage_requested"), true },
        { QStringLiteral("usage_summary"), QJsonObject {
              { QStringLiteral("request_count"), 2 },
              { QStringLiteral("reported_request_count"), 2 },
              { QStringLiteral("missing_request_count"), 0 },
              { QStringLiteral("all_requests_reported"), true },
              { QStringLiteral("input_tokens"), 250 },
              { QStringLiteral("input_request_count"), 2 },
              { QStringLiteral("output_tokens"), 30 },
              { QStringLiteral("output_request_count"), 2 },
              { QStringLiteral("total_tokens"), 280 },
              { QStringLiteral("total_request_count"), 2 },
              { QStringLiteral("cached_input_tokens"), 160 },
              { QStringLiteral("cached_input_request_count"), 2 },
              { QStringLiteral("reasoning_tokens"), 10 },
              { QStringLiteral("reasoning_request_count"), 2 } } }
    };
    dock.appendEvent(run_completed);
    Require(run_details->text().contains(
                QStringLiteral("Whole run: 91 ms · model requests 2 · tool calls 1"))
                && run_details->text().contains(
                    QStringLiteral("Model-step budget: 2/24 used"))
                && run_details->text().contains(
                    QStringLiteral("Tool-call budget: 1/128 used"))
                && run_details->text().contains(
                    QStringLiteral("Run token usage: input 250 · output 30 · total 280"))
                && run_details->text().contains(
                    QStringLiteral("Run usage details: cached input 160 · reasoning 10"))
                && run_details->property("runStatus").toString()
                    == QStringLiteral("completed")
                && run_details->property("runDurationMs").toLongLong() == 91
                && run_details->property("runModelSteps").toInt() == 2
                && run_details->property("runMaxModelSteps").toInt() == 24
                && run_details->property("runToolCalls").toInt() == 1
                && run_details->property("runMaxToolCalls").toInt() == 128
                && run_details->property("runUsageComplete").toBool()
                && run_details->property("runUsageRequestCount").toInt() == 2
                && run_details->property("runTotalTokens").toLongLong() == 280,
            "terminal technical details must distinguish whole-run timing and counts");

    SigilAgent::AgentEvent legacy_run_started = run_started;
    legacy_run_started.payload.insert(QStringLiteral("run_id"),
                                      QStringLiteral("legacy-run-id"));
    legacy_run_started.payload.remove(QStringLiteral("usage_requested"));
    dock.appendEvent(legacy_run_started);
    Require(run_details->text().contains(QStringLiteral("Run token usage: not requested"))
                && !run_details->property("runUsageRequested").toBool(),
            "a new run without usage metadata must not inherit the prior run setting");

    SigilAgent::AgentEvent partial_run_started = run_started;
    partial_run_started.payload.insert(QStringLiteral("run_id"),
                                       QStringLiteral("partial-run-id"));
    dock.appendEvent(partial_run_started);
    SigilAgent::AgentEvent partial_run_completed = run_completed;
    partial_run_completed.payload.insert(QStringLiteral("run_id"),
                                         QStringLiteral("partial-run-id"));
    partial_run_completed.payload.insert(QStringLiteral("usage_summary"), QJsonObject {
        { QStringLiteral("request_count"), 2 },
        { QStringLiteral("reported_request_count"), 1 },
        { QStringLiteral("missing_request_count"), 1 },
        { QStringLiteral("all_requests_reported"), false },
        { QStringLiteral("input_tokens"), 40 },
        { QStringLiteral("input_request_count"), 1 },
        { QStringLiteral("output_tokens"), 6 },
        { QStringLiteral("output_request_count"), 1 },
        { QStringLiteral("total_tokens"), 46 },
        { QStringLiteral("total_request_count"), 1 }
    });
    dock.appendEvent(partial_run_completed);
    Require(run_details->text().contains(
                QStringLiteral("Run token usage (1 of 2 requests reported): input 40 · output 6 · total 46"))
                && !run_details->property("runUsageComplete").toBool()
                && run_details->property("runUsageReportedRequests").toInt() == 1,
            "partial run usage must disclose request coverage beside exact known sums");

    SigilAgent::AgentEvent no_usage_completed = provider_completed;
    no_usage_completed.payload.remove(QStringLiteral("usage"));
    dock.appendEvent(provider_started);
    dock.appendEvent(no_usage_completed);
    Require(usage_details->text().contains(
                QStringLiteral("Token usage: not reported by provider"))
                && usage_details->property("usageRequested").toBool()
                && !usage_details->property("usageReported").toBool()
                && usage_details->property("totalTokens").toLongLong() == -1,
            "missing provider usage must stay unavailable instead of becoming zero");

    SigilAgent::AgentEvent usage_disabled_started = provider_started;
    usage_disabled_started.payload.insert(QStringLiteral("usage_requested"), false);
    dock.appendEvent(usage_disabled_started);
    dock.appendEvent(no_usage_completed);
    Require(usage_details->text().contains(QStringLiteral("Token usage: not requested"))
                && !usage_details->property("usageRequested").toBool(),
            "technical details must distinguish a disabled usage request");
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent provider_failed;
    provider_failed.type = SigilAgent::AgentEventType::ModelRequestFailed;
    provider_failed.timestampMs = 1700000001000;
    provider_failed.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("duration_ms"), 31 },
        { QStringLiteral("message"),
          QStringLiteral("HTTP 401: rejected sk-private-provider-key") }
    };
    dock.appendEvent(provider_failed);
    Require(provider_status->text().contains(QStringLiteral("Authentication failed (HTTP 401)"))
                && !provider_status->text().contains(QStringLiteral("sk-private-provider-key"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("failed"),
            "provider failures must use a readable summary without echoing response details");
    Require(provider_status_in_details()
                && !usage_details->text().contains(QStringLiteral("sk-private-provider-key")),
            "technical details must show the sanitized provider failure");
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent request_cancelled;
    request_cancelled.type = SigilAgent::AgentEventType::ModelRequestCancelled;
    request_cancelled.timestampMs = 1700000002000;
    request_cancelled.payload = QJsonObject {
        { QStringLiteral("request_id"), QStringLiteral("request-full-id") },
        { QStringLiteral("step"), 1 },
        { QStringLiteral("model"), QStringLiteral("deepseek-chat") },
        { QStringLiteral("duration_ms"), 44 }
    };
    dock.appendEvent(request_cancelled);
    SigilAgent::AgentEvent provider_cancelled;
    provider_cancelled.type = SigilAgent::AgentEventType::SessionCancelled;
    dock.appendEvent(provider_cancelled);
    Require(provider_status->text().contains(QStringLiteral("Last request cancelled"))
                && provider_status->text().contains(QStringLiteral("44 ms"))
                && provider_status->property("requestState").toString()
                    == QStringLiteral("cancelled"),
            "cancelled requests must not leave provider status stuck on contacting");
    Require(provider_status_in_details(),
            "technical details must show a cancelled provider request");
    dock.appendEvent(provider_started);
    dock.appendEvent(provider_cancelled);
    Require(provider_status->property("requestState").toString()
                == QStringLiteral("cancelled")
                && provider_status_in_details(),
            "session cancellation must update technical details even without a request cancellation event");
    dock.resetTranscript();
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, true, 7,
                        QStringLiteral("12345678-abcd"));
    auto *book_status = dock.findChild<QLabel *>(QStringLiteral("agentBookStatus"));
    Require(book_status && book_status->text().contains(QStringLiteral("physics.epub"))
                && book_status->text().contains(QStringLiteral("Junior Physics"))
                && book_status->text().contains(QStringLiteral("42 resources"))
                && book_status->text().contains(QStringLiteral("Unsaved changes"))
                && book_status->text().contains(QStringLiteral("Book session 12345678"))
                && book_status->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && book_status->text().contains(QStringLiteral("Agent rev 7")),
            "book status must identify the file, title, resources, session, save state, and revision");
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, false, 7,
                        QStringLiteral("12345678-abcd"));
    Require(book_status->text().contains(QStringLiteral("Saved"))
                && !book_status->text().contains(QStringLiteral("Unsaved changes")),
            "book status must update after saving");
    auto *technical_toggle = dock.findChild<QToolButton *>(
        QStringLiteral("agentTechnicalDetailsToggle"));
    auto *technical = dock.findChild<QLabel *>(QStringLiteral("agentTechnicalDetails"));
    Require(technical_toggle && technical && !technical->isVisible(),
            "technical request details must exist and start collapsed");
    technical_toggle->click();
    Require(technical->isVisible()
                && technical->text().contains(QStringLiteral("session-full-id"))
                && technical->text().contains(QStringLiteral("request-full-id"))
                && technical->text().contains(QStringLiteral("request-book-id"))
                && technical->text().contains(QStringLiteral("chapter-1:12-34"))
                && technical->property("requestBookRevision").toLongLong() == 6
                && technical->property("requestHandles").toStringList()
                    == QStringList { QStringLiteral("chapter-1:12-34"),
                                     QStringLiteral("book-css") },
            "expanded details must expose full immutable request identity and scope");
    technical_toggle->click();
    Require(!technical->isVisible(), "technical details must collapse again");
    Require(mode && mode->count() == 4, "mode combo must offer Ask/Plan/Edit/Auto");
    Require(mode->currentData().toString() == QStringLiteral("auto"),
            "Agent mode must default to Auto");
    Require(mode->itemData(0).toString() == QStringLiteral("ask")
                && mode->itemData(1).toString() == QStringLiteral("plan")
                && mode->itemData(2).toString() == QStringLiteral("edit")
                && mode->itemData(3).toString() == QStringLiteral("auto"),
            "mode combo values must be ask/plan/edit/auto");
    Require(stop && stop->text().contains(QStringLiteral("Stop")), "Stop control is missing");
    Require(fresh && fresh->text().contains(QStringLiteral("New Session")),
            "New Session control is missing");
    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    Require(!mode->isEnabled() && !fresh->isEnabled() && stop->isEnabled(),
            "mode and New Session must be locked while a run owns the Runner stack");
    dock.setRunState(SigilAgent::AgentRunState::Completed);
    Require(mode->isEnabled() && fresh->isEnabled() && !stop->isEnabled(),
            "terminal run state must restore mode and New Session controls");
    dock.resetTranscript();
    SigilAgent::AgentEvent target_changed;
    target_changed.type = SigilAgent::AgentEventType::BookTargetChanged;
    dock.appendEvent(target_changed);
    Require(dock.findChild<QWidget *>(QStringLiteral("agentBookTargetChangedCard")),
            "a defensive book-session mismatch must have a dedicated visible result card");
    dock.resetTranscript();
    SigilAgent::AgentEvent book_cancelled;
    book_cancelled.type = SigilAgent::AgentEventType::SessionCancelled;
    book_cancelled.payload = QJsonObject {
        { QStringLiteral("reason"), QStringLiteral("book_changed") }
    };
    dock.appendEvent(book_cancelled);
    Require(dock.findChild<QWidget *>(QStringLiteral("agentBookChangedCard")),
            "switching books must explain why the prior run stopped");
    dock.resetTranscript();
    Require(composer, "composer is missing");
    Require(transcript, "transcript surface is missing");
    auto *whole_book_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipBook"));
    auto *file_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipFile"));
    auto *selected_files_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipSelectedFiles"));
    Require(whole_book_chip,
            "whole-book context chip is missing");
    Require(file_chip,
            "file context chip is missing");
    Require(selected_files_chip,
            "Book Browser selected-files context chip is missing");
    Require(dock.findChild<QToolButton *>(QStringLiteral("agentChipSelection")),
            "selection context chip is missing");
    auto *selection_chip =
        dock.findChild<QToolButton *>(QStringLiteral("agentChipSelection"));
    dock.setCurrentFile(QStringLiteral("OEBPS/Text/ch1.xhtml"), QStringLiteral("chapter-1"));
    Require(file_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("chapter-1") },
            "current file must be the default scope when there is no editor selection");
    dock.setSelection(QStringLiteral("chapter-1"), 12, 34,
                      QStringLiteral("<ruby>字<rt>じ</rt></ruby>"));
    Require(selection_chip && selection_chip->isEnabled() && selection_chip->isChecked()
                && selection_chip->text().contains(QStringLiteral("12–34")),
            "a live editor selection must enable and select the context chip");
    Require(selection_chip->property("resourceId").toString() == QStringLiteral("chapter-1")
                && selection_chip->property("selectionStart").toInt() == 12
                && selection_chip->property("selectionEnd").toInt() == 34,
            "selection chip must expose its exact resource and UTF-16 range");
    Require(dock.contextHandles().contains(QStringLiteral("chapter-1:12-34")),
            "selected context must emit a bounded resource range handle");
    dock.setSelection(QStringLiteral("chapter-1"), 34, 34, QString());
    Require(!selection_chip->isEnabled() && file_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("chapter-1") }
                && !dock.contextHandles().contains(QStringLiteral("chapter-1:34-34")),
            "a collapsed cursor must fall back to current file without a fake selection");

    dock.setSelectedFiles(
        QStringList { QStringLiteral("OEBPS/Text/ch1.xhtml"),
                      QStringLiteral("OEBPS/Styles/book.css"),
                      QStringLiteral("OEBPS/Styles/book.css") },
        QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css"),
                      QStringLiteral("book-css") });
    Require(selected_files_chip->isEnabled()
                && selected_files_chip->property("resourceIds").toStringList()
                    == QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css") },
            "selected-files scope must preserve Book Browser order and remove duplicates");
    selected_files_chip->click();
    Require(selected_files_chip->isChecked()
                && !file_chip->isChecked() && !whole_book_chip->isChecked()
                && dock.contextHandles()
                    == QStringList { QStringLiteral("chapter-1"), QStringLiteral("book-css") },
            "selected files must be an explicit exclusive scope");
    dock.setSelection(QStringLiteral("chapter-1"), 4, 10, QStringLiteral("source"));
    Require(selected_files_chip->isChecked(),
            "a manually selected valid scope must survive editor selection changes");
    whole_book_chip->click();
    Require(whole_book_chip->isChecked()
                && dock.contextHandles() == QStringList { QStringLiteral("book") },
            "whole book must be explicit and must not also attach file scopes");

    SigilAgent::AgentEvent thinking;
    thinking.type = SigilAgent::AgentEventType::AssistantDelta;
    thinking.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("reasoning") },
        { QStringLiteral("text"), QStringLiteral("I should inspect the spine.") }
    };
    dock.appendEvent(thinking);

    SigilAgent::AgentEvent answer;
    answer.type = SigilAgent::AgentEventType::AssistantDelta;
    answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("This book has two chapters.") }
    };
    dock.appendEvent(answer);
    ProcessEventsFor(100);

    QWidget *thinking_card = dock.findChild<QWidget *>(QStringLiteral("agentThinkingCard"));
    QWidget *answer_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    Require(thinking_card && answer_card, "thinking and answer cards must both exist");
    Require(thinking_card != answer_card, "thinking must be a distinct card from the answer");
    auto *thinking_body = thinking_card->findChild<QLabel *>(QStringLiteral("agentThinkingCardBody"));
    auto *answer_body = answer_card->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    Require(thinking_body && thinking_body->text().contains(QStringLiteral("spine")),
            "thinking card must show reasoning text");
    Require(answer_body && answer_body->text().contains(QStringLiteral("two chapters")),
            "answer card must show the user-visible content");
    Require(!thinking_body->isVisible(), "thinking card must start collapsed");
    Require(answer_body->isVisible(), "answer card must be visible");
    Require(transcript->property("streamFlushIntervalMs").toInt() == 33
                && transcript->property("streamRenderBatches").toULongLong() == 1,
            "reasoning and answer deltas in one frame must share one render batch");

    dock.resetTranscript();
    SigilAgent::AgentEvent tiny_delta;
    tiny_delta.type = SigilAgent::AgentEventType::AssistantDelta;
    tiny_delta.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("x") }
    };
    constexpr int streamed_chunks = 2000;
    for (int i = 0; i < streamed_chunks; ++i) dock.appendEvent(tiny_delta);
    Require(!dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"))
                && transcript->property("streamRenderBatches").toULongLong() == 0,
            "a burst of stream chunks must not trigger per-chunk card rendering");
    SigilAgent::AgentEvent complete_answer;
    complete_answer.type = SigilAgent::AgentEventType::AssistantMessage;
    complete_answer.payload = QJsonObject {
        { QStringLiteral("content"), QString(streamed_chunks, QLatin1Char('x')) }
    };
    dock.appendEvent(complete_answer);
    auto *coalesced_body = dock.findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    QWidget *coalesced_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    Require(coalesced_body && coalesced_card
                && coalesced_card->property("rawMarkdown").toString()
                    == QString(streamed_chunks, QLatin1Char('x'))
                && VisibleText(coalesced_body) == QString(streamed_chunks, QLatin1Char('x'))
                && coalesced_card->property("markdownState").toString() == QStringLiteral("rendered")
                && transcript->property("streamRenderBatches").toULongLong() == 1,
            "a terminal event must synchronously flush every queued stream character once");

    dock.resetTranscript();
    dock.appendEvent(tiny_delta);
    dock.resetTranscript();
    ProcessEventsFor(100);
    Require(!dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"))
                && transcript->property("streamRenderBatches").toULongLong() == 0,
            "transcript reset must cancel a pending stream flush from the old session");

    SigilAgent::AgentEvent first_user;
    first_user.type = SigilAgent::AgentEventType::UserMessage;
    first_user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("one") } };
    dock.appendEvent(first_user);
    SigilAgent::AgentEvent first_answer;
    first_answer.type = SigilAgent::AgentEventType::AssistantDelta;
    first_answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("first answer") }
    };
    dock.appendEvent(first_answer);
    SigilAgent::AgentEvent second_user;
    second_user.type = SigilAgent::AgentEventType::UserMessage;
    second_user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("two") } };
    dock.appendEvent(second_user);
    SigilAgent::AgentEvent second_answer;
    second_answer.type = SigilAgent::AgentEventType::AssistantDelta;
    second_answer.payload = QJsonObject {
        { QStringLiteral("kind"), QStringLiteral("content") },
        { QStringLiteral("text"), QStringLiteral("second answer") }
    };
    dock.appendEvent(second_answer);
    SigilAgent::AgentEvent final_answer;
    final_answer.type = SigilAgent::AgentEventType::AssistantMessage;
    final_answer.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("second answer") }
    };
    dock.appendEvent(final_answer);
    application.processEvents();

    QWidget *answer1 = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
    QWidget *answer2 = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard2"));
    Require(answer1 && answer2 && answer1 != answer2,
            "each user turn must keep its own answer card");
    auto *body1 = answer1->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    auto *body2 = answer2->findChild<QLabel *>(QStringLiteral("agentAnswerCard2Body"));
    Require(body1 && body1->text().contains(QStringLiteral("first answer")),
            "the first answer card must not be overwritten by a later turn");
    Require(body2 && body2->text().contains(QStringLiteral("second answer")),
            "the second turn must render on a new answer card");

    dock.resetTranscript();
    SigilAgent::AgentEvent user;
    user.type = SigilAgent::AgentEventType::UserMessage;
    user.payload = QJsonObject { { QStringLiteral("text"), QStringLiteral("fix title") } };
    dock.appendEvent(user);
    SigilAgent::AgentEvent step1;
    step1.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step1);
    SigilAgent::AgentEvent first_content;
    first_content.type = SigilAgent::AgentEventType::AssistantMessage;
    first_content.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("I'll patch the title.") }
    };
    dock.appendEvent(first_content);
    SigilAgent::AgentEvent step2;
    step2.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step2);
    SigilAgent::AgentEvent second_content;
    second_content.type = SigilAgent::AgentEventType::AssistantMessage;
    second_content.payload = QJsonObject {
        { QStringLiteral("content"), QStringLiteral("长度保持不变（216→216），符合预期。提交修复：") }
    };
    dock.appendEvent(second_content);
    application.processEvents();
    auto *step1_body = dock.findChild<QLabel *>(QStringLiteral("agentAnswerCardBody"));
    auto *step2_card = dock.findChild<QWidget *>(QStringLiteral("agentAnswerCard1-2"));
    auto *step2_body = step2_card
        ? step2_card->findChild<QLabel *>(QStringLiteral("agentAnswerCard1-2Body"))
        : nullptr;
    Require(step1_body && VisibleText(step1_body).contains(QStringLiteral("I'll patch the title.")),
            "later model steps must not overwrite the earlier answer card");
    Require(step2_body && VisibleText(step2_body).contains(QStringLiteral("提交修复")),
            "each model step must keep its own answer card");

    SigilAgent::AgentEvent approval;
    approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("call-1") },
        { QStringLiteral("name"), QStringLiteral("transaction.commit") },
        { QStringLiteral("impact"), QStringLiteral("Commit staged edits") }
    };
    dock.appendEvent(approval);
    application.processEvents();
    auto *approve = dock.findChild<QPushButton *>(QStringLiteral("agentApproveButton-call-1"));
    auto *deny = dock.findChild<QPushButton *>(QStringLiteral("agentDenyButton-call-1"));
    Require(approve && deny && approve->isEnabled() && deny->isEnabled(),
            "approval buttons must start enabled");
    bool approved = false;
    QObject::connect(&dock, &SigilAgent::AgentDock::approvalResponded,
                     [&approved](const QString &id, bool ok,
                                 const QJsonObject &overrides) {
                         if (id != QLatin1String("call-1")) return;
                         Require(overrides.isEmpty(),
                                 "ordinary approval must not add argument overrides");
                         approved = ok;
                     });
    approve->click();
    application.processEvents();
    Require(approved, "Approve must emit approvalResponded");
    Require(!approve->isEnabled() && !deny->isEnabled(),
            "Approve/Deny must disable after a decision");
    Require(approve->text().contains(QStringLiteral("Approved")),
            "Approve must show it was accepted");

    SigilAgent::AgentEvent paragraph_plan;
    paragraph_plan.type = SigilAgent::AgentEventType::PlanCreated;
    paragraph_plan.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("plan-call") },
        { QStringLiteral("name"), QStringLiteral("paragraphs.plan") },
        { QStringLiteral("plan_kind"), QStringLiteral("paragraph_normalization") },
        { QStringLiteral("plan_id"), QStringLiteral("paragraph-plan-id") },
        { QStringLiteral("plan_digest"), QStringLiteral("paragraph-plan-digest") },
        { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
        { QStringLiteral("book_revision"), 7 },
        { QStringLiteral("applied_to_book"), false },
        { QStringLiteral("summary"), QJsonObject {
            { QStringLiteral("ready_files"), 2 },
            { QStringLiteral("review_only_files"), 0 },
            { QStringLiteral("skipped_files"), 1 },
            { QStringLiteral("error_files"), 0 },
            { QStringLiteral("conversion_count"), 20 },
            { QStringLiteral("protected_count"), 3 }
        } },
        { QStringLiteral("changes_css"), false },
        { QStringLiteral("changes_opf"), false },
        { QStringLiteral("adds_resources"), false },
        { QStringLiteral("changes"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-1.xhtml") },
                { QStringLiteral("conversion_count"), 12 },
                { QStringLiteral("protected_count"), 2 },
                { QStringLiteral("source_diff"), QJsonObject {
                    { QStringLiteral("before"), QStringLiteral("<div>Original paragraph</div>") },
                    { QStringLiteral("after"), QStringLiteral("<p>Original paragraph</p>") },
                    { QStringLiteral("prefix_truncated"), true },
                    { QStringLiteral("suffix_truncated"), false }
                } }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("conversion_count"), 8 },
                { QStringLiteral("protected_count"), 1 },
                { QStringLiteral("source_diff"), QJsonObject {
                    { QStringLiteral("before"), QStringLiteral("<div>Second paragraph</div>") },
                    { QStringLiteral("after"), QStringLiteral("<p>Second paragraph</p>") }
                } }
            }
        } },
        { QStringLiteral("operation_groups_independent"), true },
        { QStringLiteral("operation_groups"), QJsonArray {
            QJsonObject {
                { QStringLiteral("group_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("label"), QStringLiteral("Text/chapter-1.xhtml") },
                { QStringLiteral("resource_ids"), QJsonArray {
                    QStringLiteral("chapter-1") } },
                { QStringLiteral("conversion_count"), 12 },
                { QStringLiteral("protected_count"), 2 },
                { QStringLiteral("independently_applicable"), true }
            },
            QJsonObject {
                { QStringLiteral("group_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("label"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("resource_ids"), QJsonArray {
                    QStringLiteral("chapter-2") } },
                { QStringLiteral("conversion_count"), 8 },
                { QStringLiteral("protected_count"), 1 },
                { QStringLiteral("independently_applicable"), true }
            }
        } },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    int opened_plan_resources = 0;
    QString opened_plan_path;
    QObject::connect(&dock, &SigilAgent::AgentDock::openPlanResourceRequested,
                     [&opened_plan_resources, &opened_plan_path](
                         const QString &path, const QString &book_session_id) {
        if (book_session_id == QLatin1String("12345678-abcd")) {
            ++opened_plan_resources;
            opened_plan_path = path;
        }
    });
    dock.appendEvent(paragraph_plan);
    application.processEvents();
    auto *paragraph_plan_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-plan-call"));
    auto *paragraph_plan_body = paragraph_plan_card
        ? paragraph_plan_card->findChild<QLabel *>(
              QStringLiteral("agentPlanReviewCard-plan-callBody"))
        : nullptr;
    auto *open_paragraph = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanOpenResourceButton-plan-call-0"));
    auto *compare_paragraph = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanCompareButton-plan-call-0"));
    Require(paragraph_plan_card && paragraph_plan_body && paragraph_plan_body->isVisible()
                && paragraph_plan_body->textFormat() == Qt::PlainText
                && paragraph_plan_card->property("planId").toString()
                    == QStringLiteral("paragraph-plan-id")
                && paragraph_plan_card->property("planDigest").toString()
                    == QStringLiteral("paragraph-plan-digest")
                && paragraph_plan_card->property("bookRevision").toLongLong() == 7,
            "paragraph plan card must expose a visible, plain-text reviewed binding");
    Require(paragraph_plan_body->text().contains(QStringLiteral("12 conversion(s)"))
                && paragraph_plan_body->text().contains(QStringLiteral("2 protected item(s)"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("<div>Original paragraph</div>"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("<p>Original paragraph</p>"))
                && paragraph_plan_body->text().contains(QStringLiteral("XHTML only"))
                && paragraph_plan_body->text().contains(
                    QStringLiteral("Full EPUBCheck: not run")),
            "paragraph review must show scope, bounded source excerpts, and validation limits");
    Require(open_paragraph && open_paragraph->isEnabled()
                && open_paragraph->property("bookPath").toString()
                    == QStringLiteral("Text/chapter-1.xhtml"),
            "paragraph review must offer a book-bound resource navigation action");
    Require(compare_paragraph && compare_paragraph->isEnabled()
                && compare_paragraph->property("bookPath").toString()
                    == QStringLiteral("Text/chapter-1.xhtml")
                && compare_paragraph->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && compare_paragraph->accessibleName().contains(
                    QStringLiteral("Text/chapter-1.xhtml")),
            "paragraph review must offer a book-bound side-by-side comparison");
    compare_paragraph->click();
    application.processEvents();
    QPointer<QDialog> paragraph_comparison = dock.findChild<QDialog *>(
        QStringLiteral("agentPlanComparisonDialog-plan-call-0"));
    auto *paragraph_before = paragraph_comparison
        ? paragraph_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonBefore"))
        : nullptr;
    auto *paragraph_after = paragraph_comparison
        ? paragraph_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonAfter"))
        : nullptr;
    Require(paragraph_comparison && paragraph_comparison->isVisible()
                && paragraph_comparison->windowTitle().contains(
                    QStringLiteral("Text/chapter-1.xhtml"))
                && paragraph_comparison->property("planId").toString()
                    == QStringLiteral("paragraph-plan-id")
                && paragraph_comparison->property("planDigest").toString()
                    == QStringLiteral("paragraph-plan-digest")
                && paragraph_comparison->property("bookRevision").toLongLong() == 7
                && paragraph_comparison->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd"),
            "paragraph comparison must preserve the reviewed plan and book binding");
    Require(paragraph_before && paragraph_after
                && paragraph_before->isReadOnly() && paragraph_after->isReadOnly()
                && paragraph_before->toPlainText()
                    == QStringLiteral("<div>Original paragraph</div>")
                && paragraph_after->toPlainText()
                    == QStringLiteral("<p>Original paragraph</p>")
                && paragraph_comparison->property("displayTruncated").toBool(),
            "paragraph comparison must render literal bounded source in distinct panes");
    compare_paragraph->click();
    Require(dock.findChildren<QDialog *>(
                QStringLiteral("agentPlanComparisonDialog-plan-call-0")).size() == 1,
            "reopening one plan comparison must reuse its existing dialog");

    SigilAgent::AgentEvent matching_plan_approval;
    matching_plan_approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    matching_plan_approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("matching-plan-apply") },
        { QStringLiteral("name"), QStringLiteral("paragraphs.apply") },
        { QStringLiteral("impact"), QStringLiteral("Stage reviewed paragraph plan") },
        { QStringLiteral("arguments"), QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("paragraph-plan-id") },
            { QStringLiteral("plan_digest"), QStringLiteral("paragraph-plan-digest") },
            { QStringLiteral("expected_book_revision"), 7 }
        } }
    };
    dock.appendEvent(matching_plan_approval);
    application.processEvents();
    auto *matching_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-matching-plan-apply"));
    auto *matching_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-matching-plan-apply"));
    auto *matching_binding = matching_approval_card
        ? matching_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanBinding"))
        : nullptr;
    auto *group_list = matching_approval_card
        ? matching_approval_card->findChild<QListWidget *>(
              QStringLiteral("agentPlanGroupList-matching-plan-apply"))
        : nullptr;
    auto *select_all_groups = matching_approval_card
        ? matching_approval_card->findChild<QPushButton *>(
              QStringLiteral("agentPlanGroupsSelectAll-matching-plan-apply"))
        : nullptr;
    auto *clear_groups = matching_approval_card
        ? matching_approval_card->findChild<QPushButton *>(
              QStringLiteral("agentPlanGroupsClear-matching-plan-apply"))
        : nullptr;
    Require(matching_approve && matching_approve->isEnabled()
                && matching_approval_card->property("planReviewRequired").toBool()
                && matching_approval_card->property("reviewedPlanMatched").toBool()
                && matching_binding
                && matching_binding->text().contains(QStringLiteral("2 of 2")),
            "native apply approval must enable only for its displayed plan binding");
    Require(group_list && group_list->count() == 2
                && group_list->property("totalGroups").toInt() == 2
                && group_list->item(0)->checkState() == Qt::Checked
                && group_list->item(1)->checkState() == Qt::Checked
                && select_all_groups && clear_groups,
            "paragraph approval must expose every independent XHTML group as checked");
    clear_groups->click();
    Require(!matching_approve->isEnabled()
                && matching_binding->text().contains(
                    QStringLiteral("select at least one")),
            "clearing every paragraph group must visibly block approval");
    select_all_groups->click();
    Require(matching_approve->isEnabled()
                && matching_binding->text().contains(QStringLiteral("2 of 2")),
            "Select all must restore the complete reviewed group selection");
    group_list->item(0)->setCheckState(Qt::Unchecked);
    Require(matching_approve->isEnabled()
                && matching_binding->text().contains(QStringLiteral("1 of 2"))
                && matching_approve->property("selectedResourceIds").toStringList()
                    == QStringList { QStringLiteral("chapter-2") },
            "an independent plan group must be removable without invalidating the other group");
    QString approved_group_call;
    QJsonObject approved_group_overrides;
    QObject::connect(&dock, &SigilAgent::AgentDock::approvalResponded,
                     [&approved_group_call, &approved_group_overrides](
                         const QString &id, bool ok,
                         const QJsonObject &overrides) {
        if (id == QLatin1String("matching-plan-apply") && ok) {
            approved_group_call = id;
            approved_group_overrides = overrides;
        }
    });
    matching_approve->click();
    Require(approved_group_call == QStringLiteral("matching-plan-apply")
                && approved_group_overrides.value(
                    QStringLiteral("selected_resource_ids")).toArray()
                    == QJsonArray { QStringLiteral("chapter-2") }
                && !group_list->isEnabled()
                && !select_all_groups->isEnabled()
                && !clear_groups->isEnabled(),
            "approval must freeze and emit exactly the checked paragraph groups");

    SigilAgent::AgentEvent paged_plan_first = paragraph_plan;
    paged_plan_first.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("paged-plan-0"));
    paged_plan_first.payload.insert(
        QStringLiteral("plan_id"), QStringLiteral("paged-plan-id"));
    paged_plan_first.payload.insert(
        QStringLiteral("plan_digest"), QStringLiteral("paged-plan-digest"));
    paged_plan_first.payload.insert(
        QStringLiteral("analysis_id"), QStringLiteral("paged-analysis-id"));
    paged_plan_first.payload.insert(QStringLiteral("total_count"), 3);
    paged_plan_first.payload.insert(QStringLiteral("offset"), 0);
    paged_plan_first.payload.insert(QStringLiteral("limit"), 2);
    paged_plan_first.payload.insert(QStringLiteral("returned_count"), 2);
    paged_plan_first.payload.insert(QStringLiteral("has_more"), true);
    paged_plan_first.payload.insert(QStringLiteral("next_offset"), 2);
    paged_plan_first.payload.insert(QStringLiteral("reviewed_count"), 2);
    paged_plan_first.payload.insert(QStringLiteral("review_complete"), false);
    paged_plan_first.payload.insert(QStringLiteral("review_next_offset"), 2);
    dock.appendEvent(paged_plan_first);
    application.processEvents();
    auto *paged_plan_first_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-paged-plan-0"));
    auto *paged_plan_first_body = paged_plan_first_card
        ? paged_plan_first_card->findChild<QLabel *>(
              QStringLiteral("agentPlanReviewCard-paged-plan-0Body"))
        : nullptr;
    Require(paged_plan_first_card && paged_plan_first_body
                && !paged_plan_first_card->property(
                    "planReviewComplete").toBool()
                && paged_plan_first_body->text().contains(
                    QStringLiteral("2 of 3 XHTML file(s)"))
                && paged_plan_first_body->text().contains(
                    QStringLiteral("offset 2 before applying")),
            "an incomplete paragraph plan page must disclose required continuation");

    SigilAgent::AgentEvent incomplete_paged_approval = matching_plan_approval;
    incomplete_paged_approval.payload.insert(
        QStringLiteral("tool_call_id"),
        QStringLiteral("incomplete-paged-plan-apply"));
    QJsonObject incomplete_paged_arguments = incomplete_paged_approval.payload.value(
        QStringLiteral("arguments")).toObject();
    incomplete_paged_arguments.insert(
        QStringLiteral("plan_id"), QStringLiteral("paged-plan-id"));
    incomplete_paged_arguments.insert(
        QStringLiteral("plan_digest"), QStringLiteral("paged-plan-digest"));
    incomplete_paged_approval.payload.insert(
        QStringLiteral("arguments"), incomplete_paged_arguments);
    dock.appendEvent(incomplete_paged_approval);
    application.processEvents();
    auto *incomplete_paged_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-incomplete-paged-plan-apply"));
    Require(incomplete_paged_approve && !incomplete_paged_approve->isEnabled(),
            "apply approval must remain blocked until every paragraph plan page is reviewed");

    const QJsonObject third_change {
        { QStringLiteral("resource_id"), QStringLiteral("chapter-3") },
        { QStringLiteral("book_path"), QStringLiteral("Text/chapter-3.xhtml") },
        { QStringLiteral("conversion_count"), 4 },
        { QStringLiteral("protected_count"), 0 },
        { QStringLiteral("source_diff"), QJsonObject {
            { QStringLiteral("before"), QStringLiteral("<div>Third paragraph</div>") },
            { QStringLiteral("after"), QStringLiteral("<p>Third paragraph</p>") }
        } }
    };
    const QJsonObject third_group {
        { QStringLiteral("group_id"), QStringLiteral("chapter-3") },
        { QStringLiteral("label"), QStringLiteral("Text/chapter-3.xhtml") },
        { QStringLiteral("resource_ids"), QJsonArray {
            QStringLiteral("chapter-3") } },
        { QStringLiteral("conversion_count"), 4 },
        { QStringLiteral("protected_count"), 0 },
        { QStringLiteral("independently_applicable"), true }
    };
    SigilAgent::AgentEvent paged_plan_final = paged_plan_first;
    paged_plan_final.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("paged-plan-2"));
    paged_plan_final.payload.insert(
        QStringLiteral("changes"), QJsonArray { third_change });
    paged_plan_final.payload.insert(
        QStringLiteral("operation_groups"), QJsonArray { third_group });
    paged_plan_final.payload.insert(QStringLiteral("offset"), 2);
    paged_plan_final.payload.insert(QStringLiteral("returned_count"), 1);
    paged_plan_final.payload.insert(QStringLiteral("has_more"), false);
    paged_plan_final.payload.remove(QStringLiteral("next_offset"));
    paged_plan_final.payload.insert(QStringLiteral("reviewed_count"), 3);
    paged_plan_final.payload.insert(QStringLiteral("review_complete"), true);
    paged_plan_final.payload.remove(QStringLiteral("review_next_offset"));
    dock.appendEvent(paged_plan_final);
    application.processEvents();
    auto *paged_plan_final_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-paged-plan-2"));
    Require(paged_plan_final_card
                && paged_plan_final_card->property(
                    "planReviewComplete").toBool(),
            "the final contiguous paragraph plan page must complete review");

    SigilAgent::AgentEvent complete_paged_approval = incomplete_paged_approval;
    complete_paged_approval.payload.insert(
        QStringLiteral("tool_call_id"),
        QStringLiteral("complete-paged-plan-apply"));
    dock.appendEvent(complete_paged_approval);
    application.processEvents();
    auto *complete_paged_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-complete-paged-plan-apply"));
    auto *complete_paged_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-complete-paged-plan-apply"));
    auto *complete_paged_groups = complete_paged_approval_card
        ? complete_paged_approval_card->findChild<QListWidget *>(
              QStringLiteral("agentPlanGroupList-complete-paged-plan-apply"))
        : nullptr;
    Require(complete_paged_approval_card && complete_paged_approve
                && complete_paged_approve->isEnabled()
                && complete_paged_approval_card->property(
                    "reviewedPlanMatched").toBool()
                && complete_paged_groups
                && complete_paged_groups->count() == 3
                && complete_paged_groups->item(2)->data(
                    Qt::UserRole).toString() == QStringLiteral("chapter-3"),
            "contiguous plan pages must aggregate every group before approval");

    SigilAgent::AgentEvent skipped_plan_page = paged_plan_final;
    skipped_plan_page.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("skipped-plan-page"));
    skipped_plan_page.payload.insert(
        QStringLiteral("plan_id"), QStringLiteral("skipped-plan-id"));
    skipped_plan_page.payload.insert(
        QStringLiteral("plan_digest"), QStringLiteral("skipped-plan-digest"));
    dock.appendEvent(skipped_plan_page);
    SigilAgent::AgentEvent skipped_plan_approval = incomplete_paged_approval;
    skipped_plan_approval.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("skipped-plan-apply"));
    QJsonObject skipped_plan_arguments = skipped_plan_approval.payload.value(
        QStringLiteral("arguments")).toObject();
    skipped_plan_arguments.insert(
        QStringLiteral("plan_id"), QStringLiteral("skipped-plan-id"));
    skipped_plan_arguments.insert(
        QStringLiteral("plan_digest"), QStringLiteral("skipped-plan-digest"));
    skipped_plan_approval.payload.insert(
        QStringLiteral("arguments"), skipped_plan_arguments);
    dock.appendEvent(skipped_plan_approval);
    application.processEvents();
    auto *skipped_plan_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-skipped-plan-apply"));
    Require(skipped_plan_approve && !skipped_plan_approve->isEnabled(),
            "a final plan page without its prefix must not unlock approval");

    SigilAgent::AgentEvent mismatched_plan_approval = matching_plan_approval;
    mismatched_plan_approval.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("mismatched-plan-apply"));
    QJsonObject mismatched_arguments =
        mismatched_plan_approval.payload.value(QStringLiteral("arguments")).toObject();
    mismatched_arguments.insert(QStringLiteral("plan_digest"), QStringLiteral("other-digest"));
    mismatched_plan_approval.payload.insert(QStringLiteral("arguments"), mismatched_arguments);
    dock.appendEvent(mismatched_plan_approval);
    application.processEvents();
    auto *mismatched_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-mismatched-plan-apply"));
    auto *mismatched_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-mismatched-plan-apply"));
    auto *mismatched_deny = dock.findChild<QPushButton *>(
        QStringLiteral("agentDenyButton-mismatched-plan-apply"));
    auto *mismatched_binding = mismatched_approval_card
        ? mismatched_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanBinding"))
        : nullptr;
    Require(mismatched_approve && !mismatched_approve->isEnabled()
                && mismatched_deny && mismatched_deny->isEnabled()
                && !mismatched_approval_card->property("reviewedPlanMatched").toBool()
                && mismatched_binding
                && mismatched_binding->text().contains(QStringLiteral("blocked")),
            "an unreviewed digest must be visible and blocked while remaining deniable");

    SigilAgent::AgentEvent malformed_group_plan = paragraph_plan;
    malformed_group_plan.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("malformed-group-plan"));
    malformed_group_plan.payload.insert(
        QStringLiteral("plan_id"), QStringLiteral("malformed-group-plan-id"));
    malformed_group_plan.payload.insert(
        QStringLiteral("plan_digest"), QStringLiteral("malformed-group-plan-digest"));
    QJsonObject duplicate_group = paragraph_plan.payload.value(
        QStringLiteral("operation_groups")).toArray().first().toObject();
    malformed_group_plan.payload.insert(
        QStringLiteral("operation_groups"),
        QJsonArray { duplicate_group, duplicate_group });
    dock.appendEvent(malformed_group_plan);
    SigilAgent::AgentEvent malformed_group_approval = matching_plan_approval;
    malformed_group_approval.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("malformed-group-apply"));
    QJsonObject malformed_group_arguments = malformed_group_approval.payload.value(
        QStringLiteral("arguments")).toObject();
    malformed_group_arguments.insert(
        QStringLiteral("plan_id"), QStringLiteral("malformed-group-plan-id"));
    malformed_group_arguments.insert(
        QStringLiteral("plan_digest"),
        QStringLiteral("malformed-group-plan-digest"));
    malformed_group_approval.payload.insert(
        QStringLiteral("arguments"), malformed_group_arguments);
    dock.appendEvent(malformed_group_approval);
    application.processEvents();
    auto *malformed_group_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-malformed-group-apply"));
    auto *malformed_group_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-malformed-group-apply"));
    auto *malformed_group_deny = dock.findChild<QPushButton *>(
        QStringLiteral("agentDenyButton-malformed-group-apply"));
    Require(malformed_group_card && malformed_group_approve
                && !malformed_group_approve->isEnabled()
                && malformed_group_deny && malformed_group_deny->isEnabled()
                && !malformed_group_card->property(
                    "reviewedPlanMatched").toBool(),
            "duplicate reviewed operation groups must fail closed while remaining deniable");

    open_paragraph->click();
    Require(opened_plan_resources == 1
                && opened_plan_path == QStringLiteral("Text/chapter-1.xhtml"),
            "plan navigation must emit the reviewed resource and book session");
    dock.setBookContext(QStringLiteral("Another Book"), QStringLiteral("other.epub"),
                        3, false, 1, QStringLiteral("other-book-session"));
    Require(!open_paragraph->isEnabled() && !compare_paragraph->isEnabled(),
            "plan navigation and comparison must disable as soon as the open book changes");
    Require(!paragraph_comparison || !paragraph_comparison->isVisible(),
            "an open comparison must close when the dock binds another book");
    open_paragraph->click();
    Require(opened_plan_resources == 1,
            "a stale plan card must not navigate after the open book changes");
    dock.setBookContext(QStringLiteral("Junior Physics"),
                        QStringLiteral("physics.epub"), 42, false, 7,
                        QStringLiteral("12345678-abcd"));
    Require(open_paragraph->isEnabled() && compare_paragraph->isEnabled(),
            "returning to the plan-bound book session must restore review actions");

    SigilAgent::AgentEvent toc_plan;
    toc_plan.type = SigilAgent::AgentEventType::PlanCreated;
    toc_plan.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("toc-plan-call") },
        { QStringLiteral("name"), QStringLiteral("toc.plan_transform") },
        { QStringLiteral("plan_kind"), QStringLiteral("toc_hierarchy") },
        { QStringLiteral("plan_id"), QStringLiteral("toc-plan-id") },
        { QStringLiteral("plan_digest"), QStringLiteral("toc-plan-digest") },
        { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
        { QStringLiteral("book_revision"), 7 },
        { QStringLiteral("affected_count"), 2 },
        { QStringLiteral("adopted_count"), 1 },
        { QStringLiteral("preorder_preserved"), true },
        { QStringLiteral("changes_xhtml_headings"), false },
        { QStringLiteral("changes"), QJsonArray { QJsonObject {
            { QStringLiteral("label"), QStringLiteral("Chapter C") },
            { QStringLiteral("target"), QStringLiteral("Text/c.xhtml#one") },
            { QStringLiteral("from_depth"), 2 },
            { QStringLiteral("to_depth"), 1 },
            { QStringLiteral("from_parent_id"), 1 },
            { QStringLiteral("to_parent_id"), 0 }
        } } },
        { QStringLiteral("changes_truncated"), true },
        { QStringLiteral("local_validation"), QStringLiteral("passed") },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } }
    };
    dock.appendEvent(toc_plan);
    application.processEvents();
    auto *toc_plan_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPlanReviewCard-toc-plan-call"));
    auto *toc_plan_body = toc_plan_card
        ? toc_plan_card->findChild<QLabel *>(
              QStringLiteral("agentPlanReviewCard-toc-plan-callBody"))
        : nullptr;
    auto *open_toc = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanOpenResourceButton-toc-plan-call-0"));
    auto *compare_toc = dock.findChild<QPushButton *>(
        QStringLiteral("agentPlanCompareButton-toc-plan-call-toc"));
    Require(toc_plan_body && toc_plan_body->text().contains(
                QStringLiteral("2 affected node(s)"))
                && toc_plan_body->text().contains(QStringLiteral("Chapter C"))
                && toc_plan_body->text().contains(QStringLiteral("Depth: 2 → 1"))
                && toc_plan_body->text().contains(QStringLiteral("Preorder preserved: Yes"))
                && toc_plan_body->text().contains(QStringLiteral("omitted")),
            "TOC review must show bounded reparenting and structural invariants");
    Require(open_toc && open_toc->property("bookPath").toString()
                    == QStringLiteral("Text/c.xhtml"),
            "TOC review navigation must strip the target fragment");
    Require(compare_toc && compare_toc->isEnabled()
                && compare_toc->accessibleName().contains(
                    QStringLiteral("TOC hierarchy")),
            "TOC review must offer a book-bound hierarchy comparison");
    compare_toc->click();
    application.processEvents();
    QPointer<QDialog> toc_comparison = dock.findChild<QDialog *>(
        QStringLiteral("agentPlanComparisonDialog-toc-plan-call-toc"));
    auto *toc_before = toc_comparison
        ? toc_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonBefore"))
        : nullptr;
    auto *toc_after = toc_comparison
        ? toc_comparison->findChild<QPlainTextEdit *>(
              QStringLiteral("agentPlanComparisonAfter"))
        : nullptr;
    Require(toc_comparison && toc_comparison->isVisible()
                && toc_comparison->property("planId").toString()
                    == QStringLiteral("toc-plan-id")
                && toc_comparison->property("comparisonSubject").toString()
                    == QStringLiteral("TOC hierarchy")
                && toc_comparison->property("displayTruncated").toBool(),
            "TOC comparison must preserve the reviewed plan and truncation boundary");
    Require(toc_before && toc_after
                && toc_before->toPlainText().contains(
                    QStringLiteral("Depth: 2 · Parent: 1"))
                && toc_after->toPlainText().contains(
                    QStringLiteral("Depth: 1 · Parent: 0")),
            "TOC comparison must place old and new hierarchy values in separate panes");
    SigilAgent::AgentEvent toc_approval;
    toc_approval.type = SigilAgent::AgentEventType::ToolApprovalRequested;
    toc_approval.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("toc-plan-apply") },
        { QStringLiteral("name"), QStringLiteral("toc.apply_transform") },
        { QStringLiteral("impact"), QStringLiteral("Stage reviewed TOC plan") },
        { QStringLiteral("arguments"), QJsonObject {
            { QStringLiteral("plan_id"), QStringLiteral("toc-plan-id") },
            { QStringLiteral("plan_digest"), QStringLiteral("toc-plan-digest") },
            { QStringLiteral("expected_book_revision"), 7 }
        } }
    };
    dock.appendEvent(toc_approval);
    application.processEvents();
    auto *toc_approval_card = dock.findChild<QWidget *>(
        QStringLiteral("agentApprovalCard-toc-plan-apply"));
    auto *toc_approve = dock.findChild<QPushButton *>(
        QStringLiteral("agentApproveButton-toc-plan-apply"));
    auto *toc_dependency = toc_approval_card
        ? toc_approval_card->findChild<QLabel *>(
              QStringLiteral("agentApprovalPlanDependency"))
        : nullptr;
    Require(toc_approve && toc_approve->isEnabled() && toc_dependency
                && toc_dependency->text().contains(QStringLiteral("dependent group"))
                && !toc_approval_card->findChild<QListWidget *>(),
            "dependent TOC hierarchy changes must remain one visible inseparable group");

    dock.resetTranscript();
    application.processEvents();
    Require(!toc_comparison || !toc_comparison->isVisible(),
            "resetting the transcript must close detached plan comparisons");
    SigilAgent::AgentEvent preview;
    preview.type = SigilAgent::AgentEventType::TransactionPreviewed;
    preview.payload = QJsonObject {
        { QStringLiteral("changes"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-1") },
                { QStringLiteral("original_length"), 80 },
                { QStringLiteral("staged_length"), 96 },
                { QStringLiteral("changed"), true }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("staged-cover") },
                { QStringLiteral("book_path"), QStringLiteral("Images/cover.jpg") },
                { QStringLiteral("added"), true }
            },
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("from"), QStringLiteral("Text/ch2.xhtml") },
                { QStringLiteral("book_path"), QStringLiteral("Text/chapter-2.xhtml") },
                { QStringLiteral("renamed"), true }
            }
        } },
        { QStringLiteral("metadata_changed"), true },
        { QStringLiteral("spine_changed"), true },
        { QStringLiteral("toc_changed"), true },
        { QStringLiteral("removed"), QJsonArray { QStringLiteral("old-style") } },
        { QStringLiteral("total_counts"), QJsonObject {
            { QStringLiteral("changes"), 8 },
            { QStringLiteral("removed"), 4 }
        } },
        { QStringLiteral("returned_counts"), QJsonObject {
            { QStringLiteral("changes"), 3 },
            { QStringLiteral("removed"), 1 }
        } },
        { QStringLiteral("offset"), 0 },
        { QStringLiteral("has_more"), true },
        { QStringLiteral("next_offset"), 3 },
        { QStringLiteral("applied_to_book"), false }
    };
    dock.appendEvent(preview);
    application.processEvents();
    auto *preview_card = dock.findChild<QWidget *>(QStringLiteral("agentPreviewCard"));
    auto *preview_body = preview_card
        ? preview_card->findChild<QLabel *>(QStringLiteral("agentPreviewCardBody"))
        : nullptr;
    Require(preview_body && preview_body->isVisible(), "preview status card must be visible");
    Require(preview_body->text().contains(QStringLiteral("live book is unchanged"))
                && preview_body->text().contains(QStringLiteral("Text: chapter-1 (80 → 96)")),
            "preview card must distinguish staging from a live text edit");
    Require(preview_body->text().contains(QStringLiteral("Added: Images/cover.jpg"))
                && preview_body->text().contains(QStringLiteral("Renamed: Text/ch2.xhtml → Text/chapter-2.xhtml"))
                && preview_body->text().contains(QStringLiteral("Removed: old-style")),
            "preview card must describe structural resource changes");
    Require(preview_body->text().contains(QStringLiteral("Metadata changes"))
                && preview_body->text().contains(QStringLiteral("Reading order changes"))
                && preview_body->text().contains(QStringLiteral("TOC hierarchy changes")),
            "preview card must expose metadata, spine, and TOC changes");
    Require(preview_body->text().contains(
                QStringLiteral("3 of 8 changes and 1 of 4 removals"))
                && preview_body->text().contains(
                    QStringLiteral("offset 3 before committing")),
            "preview card must disclose partial pages and the next required offset");

    SigilAgent::AgentEvent committed;
    committed.type = SigilAgent::AgentEventType::TransactionCommitted;
    committed.payload = QJsonObject {
        { QStringLiteral("applied_to_book"), true },
        { QStringLiteral("save_status"), QStringLiteral("not_saved") },
        { QStringLiteral("applied_changes"), 4 },
        { QStringLiteral("book_revision"), 17 },
        { QStringLiteral("resource_outcomes"), QJsonObject {
            { QStringLiteral("scope_available"), true },
            { QStringLiteral("all_or_nothing"), true },
            { QStringLiteral("status"), QStringLiteral("all_applied") },
            { QStringLiteral("transaction_state"), QStringLiteral("committed") },
            { QStringLiteral("resource_count"), 2 },
            { QStringLiteral("successful_resource_count"), 2 },
            { QStringLiteral("failed_resource_count"), 0 },
            { QStringLiteral("structural_operation_count"), 1 },
            { QStringLiteral("successful_structural_operation_count"), 1 },
            { QStringLiteral("failed_structural_operation_count"), 0 }
        } },
        { QStringLiteral("full_epubcheck"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("not_run") }
        } },
        { QStringLiteral("recovery"), QJsonObject {
            { QStringLiteral("sigil_undo"), QStringLiteral("where_available") },
            { QStringLiteral("task_restore_point"), QStringLiteral("available") },
            { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
            { QStringLiteral("book_session_id"), QStringLiteral("12345678-abcd") },
            { QStringLiteral("affected_resources"), QJsonArray {
                QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } }
        } }
    };
    dock.setRunState(SigilAgent::AgentRunState::Completed);
    dock.appendEvent(committed);
    application.processEvents();
    auto *applied_card = dock.findChild<QWidget *>(QStringLiteral("agentAppliedCard"));
    auto *applied_body = applied_card
        ? applied_card->findChild<QLabel *>(QStringLiteral("agentAppliedCardBody"))
        : nullptr;
    Require(applied_body && applied_body->isVisible(), "applied status card must be visible");
    Require(applied_body->text().contains(QStringLiteral("EPUB file has not been saved"))
                && applied_body->text().contains(QStringLiteral("Applied changes: 4"))
                && applied_body->text().contains(QStringLiteral("Book revision: 17"))
                && applied_body->text().contains(
                    QStringLiteral("Resources: 2 succeeded · 0 failed"))
                && applied_body->text().contains(
                    QStringLiteral("Structural operations: 1 succeeded · 0 failed"))
                && applied_body->text().contains(
                    QStringLiteral("all staged targets were applied"))
                && applied_card->property("successfulResourceCount").toInt() == 2
                && applied_card->property("failedResourceCount").toInt() == 0
                && applied_card->property("transactionState").toString()
                    == QStringLiteral("committed"),
            "applied card must show save state and exact atomic resource outcomes");
    Require(applied_body->text().contains(QStringLiteral("Full EPUBCheck: not run"))
                && applied_body->text().contains(QStringLiteral("Undo where available"))
                && applied_body->text().contains(QStringLiteral("2 text resource(s)"))
                && applied_body->text().contains(QStringLiteral("conflict check")),
            "applied card must state validation and recovery boundaries");
    auto *restore_button = dock.findChild<QPushButton *>(
        QStringLiteral("agentTaskRestoreButton-restore-17"));
    Require(restore_button && restore_button->isEnabled(),
            "a guarded text commit must offer task restoration");

    SigilAgent::AgentEvent partial_terminal;
    partial_terminal.type = SigilAgent::AgentEventType::RunStateChanged;
    partial_terminal.payload = QJsonObject {
        { QStringLiteral("run_id"), QStringLiteral("partial-after-commit") },
        { QStringLiteral("state"), QStringLiteral("failed") },
        { QStringLiteral("partial_outcome"), QJsonObject {
            { QStringLiteral("status"), QStringLiteral("partial_applied") },
            { QStringLiteral("commit_count"), 1 },
            { QStringLiteral("resource_ids"), QJsonArray { QStringLiteral("chapter-1") } },
            { QStringLiteral("book_revision"), 17 },
            { QStringLiteral("latest_restore_point"), QStringLiteral("restore-17") }
        } }
    };
    dock.appendEvent(partial_terminal);
    application.processEvents();
    auto *partial_card = dock.findChild<QWidget *>(
        QStringLiteral("agentPartialOutcomeCard"));
    auto *partial_body = partial_card
        ? partial_card->findChild<QLabel *>(QStringLiteral("agentPartialOutcomeCardBody"))
        : nullptr;
    Require(partial_body && partial_body->text().contains(
                QStringLiteral("after 1 transaction"))
                && partial_body->text().contains(QStringLiteral("chapter-1"))
                && partial_body->text().contains(QStringLiteral("revision: 17"))
                && partial_body->text().contains(QStringLiteral("Completion and validation are unconfirmed"))
                && partial_body->text().contains(QStringLiteral("did not save the EPUB")),
            "failed runs with commits must show a persistent partial-outcome card");

    SigilAgent::AgentEvent rolled_back_commit;
    rolled_back_commit.type = SigilAgent::AgentEventType::ToolFailed;
    rolled_back_commit.payload = QJsonObject {
        { QStringLiteral("tool_call_id"), QStringLiteral("commit-rolled-back") },
        { QStringLiteral("name"), QStringLiteral("transaction.commit") },
        { QStringLiteral("code"), QStringLiteral("TRANSACTION_ROLLED_BACK") },
        { QStringLiteral("message"), QStringLiteral("<unsafe> write failed") },
        { QStringLiteral("data"), QJsonObject {
            { QStringLiteral("resource_outcomes"), QJsonObject {
                { QStringLiteral("scope_available"), true },
                { QStringLiteral("all_or_nothing"), true },
                { QStringLiteral("status"), QStringLiteral("not_applied") },
                { QStringLiteral("transaction_state"), QStringLiteral("rolled_back") },
                { QStringLiteral("resource_count"), 2 },
                { QStringLiteral("successful_resource_count"), 0 },
                { QStringLiteral("failed_resource_count"), 2 },
                { QStringLiteral("structural_operation_count"), 1 },
                { QStringLiteral("successful_structural_operation_count"), 0 },
                { QStringLiteral("failed_structural_operation_count"), 1 }
            } }
        } }
    };
    dock.appendEvent(rolled_back_commit);
    application.processEvents();
    auto *rolled_back_card = dock.findChild<QWidget *>(
        QStringLiteral("agentToolCard-commit-rolled-back"));
    auto *rolled_back_title = rolled_back_card
        ? rolled_back_card->findChild<QToolButton *>(
              QStringLiteral("agentToolCard-commit-rolled-backTitle"))
        : nullptr;
    auto *rolled_back_body = rolled_back_card
        ? rolled_back_card->findChild<QLabel *>(
              QStringLiteral("agentToolCard-commit-rolled-backBody"))
        : nullptr;
    Require(rolled_back_title && rolled_back_title->text().contains(
                QStringLiteral("Apply failed"))
                && rolled_back_body && rolled_back_body->isVisible()
                && rolled_back_body->textFormat() == Qt::PlainText
                && rolled_back_body->text().contains(
                    QStringLiteral("Not applied to the current book"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Resources: 0 succeeded · 2 failed"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Structural operations: 0 succeeded · 1 failed"))
                && rolled_back_body->text().contains(
                    QStringLiteral("no partial book changes remain"))
                && rolled_back_body->text().contains(
                    QStringLiteral("Full EPUBCheck: not run"))
                && rolled_back_body->text().contains(
                    QStringLiteral("<unsafe> write failed"))
                && rolled_back_card->property("successfulResourceCount").toInt() == 0
                && rolled_back_card->property("failedResourceCount").toInt() == 2
                && rolled_back_card->property("transactionState").toString()
                    == QStringLiteral("rolled_back"),
            "rolled-back commit failure must visibly report atomic resource outcomes as plain text");

    SigilAgent::AgentEvent retained_commit = rolled_back_commit;
    retained_commit.payload.insert(
        QStringLiteral("tool_call_id"), QStringLiteral("commit-retained"));
    retained_commit.payload.insert(
        QStringLiteral("code"), QStringLiteral("BOOK_REVISION_CONFLICT"));
    QJsonObject retained_data = retained_commit.payload.value(
        QStringLiteral("data")).toObject();
    QJsonObject retained_outcomes = retained_data.value(
        QStringLiteral("resource_outcomes")).toObject();
    retained_outcomes.insert(
        QStringLiteral("transaction_state"), QStringLiteral("staged"));
    retained_data.insert(QStringLiteral("resource_outcomes"), retained_outcomes);
    retained_commit.payload.insert(QStringLiteral("data"), retained_data);
    dock.appendEvent(retained_commit);
    application.processEvents();
    auto *retained_card = dock.findChild<QWidget *>(
        QStringLiteral("agentToolCard-commit-retained"));
    auto *retained_body = retained_card
        ? retained_card->findChild<QLabel *>(
              QStringLiteral("agentToolCard-commit-retainedBody"))
        : nullptr;
    Require(retained_body && retained_body->isVisible()
                && retained_body->text().contains(
                    QStringLiteral("remains available for review, retry, or rollback"))
                && retained_card->property("transactionState").toString()
                    == QStringLiteral("staged"),
            "revision conflict result must say that its staged transaction remains available");
    QString requested_restore;
    QString requested_restore_book;
    QObject::connect(&dock, &SigilAgent::AgentDock::taskRestoreRequested,
                     [&requested_restore, &requested_restore_book](
                         const QString &checkpoint_id, const QString &book_session_id) {
        requested_restore = checkpoint_id;
        requested_restore_book = book_session_id;
    });
    restore_button->click();
    Require(requested_restore == QStringLiteral("restore-17")
                && requested_restore_book == QStringLiteral("12345678-abcd")
                && !restore_button->isEnabled(),
            "Restore this task must emit the checkpoint and frozen book session once");

    SigilAgent::AgentEvent restore_conflict;
    restore_conflict.type = SigilAgent::AgentEventType::TaskRestoreFailed;
    restore_conflict.payload = QJsonObject {
        { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
        { QStringLiteral("code"), QStringLiteral("TASK_RESTORE_CONFLICT") },
        { QStringLiteral("conflicts"), QJsonArray {
            QJsonObject {
                { QStringLiteral("resource_id"), QStringLiteral("chapter-2") },
                { QStringLiteral("reason"), QStringLiteral("content_changed") }
            } } }
    };
    dock.appendEvent(restore_conflict);
    application.processEvents();
    auto *conflict_card = dock.findChild<QWidget *>(
        QStringLiteral("agentTaskRestoreFailedCard-restore-17"));
    auto *conflict_body = conflict_card
        ? conflict_card->findChild<QLabel *>(
              QStringLiteral("agentTaskRestoreFailedCard-restore-17Body"))
        : nullptr;
    Require(conflict_body && conflict_body->text().contains(QStringLiteral("No book content was changed"))
                && restore_button->isEnabled(),
            "a restore conflict must be visible, non-mutating, and retryable after manual resolution");

    SigilAgent::AgentEvent restored;
    restored.type = SigilAgent::AgentEventType::TaskRestoreCompleted;
    restored.payload = QJsonObject {
        { QStringLiteral("checkpoint_id"), QStringLiteral("restore-17") },
        { QStringLiteral("affected_resources"), QJsonArray {
            QStringLiteral("chapter-1"), QStringLiteral("chapter-2") } },
        { QStringLiteral("book_revision"), 18 }
    };
    dock.appendEvent(restored);
    application.processEvents();
    Require(!restore_button->isEnabled()
                && restore_button->text().contains(QStringLiteral("Restored")),
            "a completed task restore must permanently settle its action");

    SigilAgent::AgentEvent rolled_back;
    rolled_back.type = SigilAgent::AgentEventType::TransactionRolledBack;
    rolled_back.payload = QJsonObject {
        { QStringLiteral("rolled_back"), true },
        { QStringLiteral("live_book_unchanged"), true }
    };
    dock.appendEvent(rolled_back);
    application.processEvents();
    auto *rollback_card = dock.findChild<QWidget *>(QStringLiteral("agentRollbackCard"));
    auto *rollback_body = rollback_card
        ? rollback_card->findChild<QLabel *>(QStringLiteral("agentRollbackCardBody"))
        : nullptr;
    Require(rollback_body && rollback_body->isVisible()
                && rollback_body->text().contains(QStringLiteral("live book was not changed")),
            "rollback must render a visible live-book status card");

    composer->setPlainText(QStringLiteral("hello from enter"));
    QString seen;
    QStringList seen_handles;
    int send_count = 0;
    QObject::connect(&dock, &SigilAgent::AgentDock::sendRequested,
                     [&](const QString &text, const QStringList &handles) {
                         ++send_count;
                         seen = text;
                         seen_handles = handles;
                         Require(composer->toPlainText().isEmpty(),
                                 "composer must already be empty when sendRequested fires");
                     });
    auto *send = dock.findChild<QPushButton *>(QStringLiteral("agentSendButton"));
    Require(send && send->isEnabled(), "Send must be enabled while idle");
    send->click();
    application.processEvents();
    Require(send_count == 1 && seen == QStringLiteral("hello from enter")
                && seen_handles == QStringList { QStringLiteral("book") },
            "Send must emit the composer text and exact scope snapshot");
    Require(composer->toPlainText().isEmpty(), "composer must stay empty after Send");

    auto *retry = dock.findChild<QPushButton *>(QStringLiteral("agentRetryButton"));
    Require(retry && !retry->isEnabled(),
            "Retry must remain unavailable until the submitted provider request fails");
    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    dock.appendEvent(provider_started);
    dock.appendEvent(provider_failed);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(retry->isEnabled()
                && retry->property("bookSessionId").toString()
                    == QStringLiteral("12345678-abcd")
                && retry->property("contextHandles").toStringList()
                    == QStringList { QStringLiteral("book") },
            "a provider failure must enable retry for the exact submitted book and scope");
    retry->click();
    application.processEvents();
    Require(send_count == 2 && seen == QStringLiteral("hello from enter")
                && seen_handles == QStringList { QStringLiteral("book") }
                && !retry->isEnabled(),
            "Retry must resend the frozen text and handles once, then disable itself");

    dock.setRunState(SigilAgent::AgentRunState::StreamingResponse);
    dock.appendEvent(provider_started);
    dock.appendEvent(provider_failed);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(retry->isEnabled(), "a repeated provider failure must offer another retry");
    dock.setBookContext(QStringLiteral("Another Book"), QStringLiteral("other.epub"),
                        3, false, 1, QStringLiteral("different-book-session"));
    Require(!retry->isEnabled()
                && retry->toolTip().contains(QStringLiteral("open book changed")),
            "retry must fail closed after the window binds a different book session");
    dock.setBookContext(QStringLiteral("Junior Physics"), QStringLiteral("physics.epub"),
                        42, false, 7, QStringLiteral("12345678-abcd"));
    dock.appendEvent(provider_started);
    SigilAgent::AgentEvent late_failure = provider_failed;
    late_failure.payload.insert(QStringLiteral("step"), 2);
    dock.appendEvent(late_failure);
    dock.setRunState(SigilAgent::AgentRunState::Failed);
    Require(!retry->isEnabled()
                && retry->toolTip().contains(QStringLiteral("already executed tools")),
            "a later model-step failure must not offer a retry that could duplicate tool effects");
    dock.setSessionId(QStringLiteral("new-session-id"));
    Require(!retry->isEnabled()
                && retry->property("contextHandles").toStringList().isEmpty(),
            "New Session must discard all retry payload and scope state");
    TestMarkdownNavigation(application);
    return EXIT_SUCCESS;
}
