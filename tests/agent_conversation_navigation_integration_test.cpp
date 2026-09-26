#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QDeadlineTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "Agent/UI/AgentDock.h"
#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"

namespace
{

void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

struct Anchor {
    QString href;
    QString text;
};

QList<Anchor> anchors(const QLabel *label)
{
    static const QRegularExpression pattern(QStringLiteral(
        "<a href=\"(sigil-agent://location/[0-9a-f]{32})\"[^>]*>(?:<span[^>]*>)?([^<]+)<"));
    QList<Anchor> found;
    auto it = pattern.globalMatch(label->text());
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        found.append({ match.captured(1), match.captured(2) });
    }
    return found;
}

QHash<QString, QString> textSnapshot(Book &book)
{
    QHash<QString, QString> texts;
    for (Resource *resource : book.GetFolderKeeper()->GetResourceList()) {
        if (auto *text = qobject_cast<TextResource *>(resource)) {
            texts.insert(resource->GetIdentifier(), text->GetText());
        }
    }
    return texts;
}

bool waitForLine(MainWindow &window, const QString &path, int line)
{
    const QDeadlineTimer deadline(5000);
    while (!deadline.hasExpired()) {
        MainApplication::processEvents(QEventLoop::AllEvents, 20);
        ContentTab *tab = window.GetCurrentContentTab();
        if (!tab || !tab->GetLoadedResource() || !tab->IsLoadingFinished()) continue;
        if (tab->GetLoadedResource()->GetRelativePath() != path) continue;
        if (line < 1 || tab->GetCursorLine() == line) return true;
    }
    return false;
}

void sendAnswer(SigilAgent::AgentDock &dock, const QString &answer)
{
    SigilAgent::AgentEvent user;
    user.type = SigilAgent::AgentEventType::UserMessage;
    user.payload = QJsonObject { { QStringLiteral("text"), QString::fromUtf8("帮我校对一下第一章") } };
    dock.appendEvent(user);
    SigilAgent::AgentEvent step;
    step.type = SigilAgent::AgentEventType::ModelRequestStarted;
    dock.appendEvent(step);
    SigilAgent::AgentEvent message;
    message.type = SigilAgent::AgentEventType::AssistantMessage;
    message.payload = QJsonObject { { QStringLiteral("content"), answer } };
    dock.appendEvent(message);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);

    try {
        require(argc == 3, "Expected source root and disposable EPUB fixture");
        const QString input = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QString answer = QString::fromUtf8(readFile(input + QStringLiteral(".answer.md")));
        const QJsonObject expected = QJsonDocument::fromJson(
            readFile(input + QStringLiteral(".expect.json"))).object();
        const QString chapter_path = expected.value(QStringLiteral("resource_path")).toString();
        const QJsonObject expected_lines = expected.value(QStringLiteral("lines")).toObject();

        SettingsStore settings;
        settings.setCleanOn(0);
        MainWindow window(input);
        window.resize(1400, 900);
        window.show();
        MainApplication::processEvents();
        const QSharedPointer<Book> book = window.GetCurrentBook();
        require(book && !book->IsModified(), "MainWindow did not load the fixture cleanly");
        auto *dock = window.findChild<SigilAgent::AgentDock *>(QStringLiteral("agentDock"));
        require(dock, "MainWindow has no Agent dock");
        auto *chapter = qobject_cast<TextResource *>(
            book->GetFolderKeeper()->GetResourceByBookPathNoThrow(chapter_path));
        require(chapter, "The fixture chapter is missing");
        const QString chapter_id = chapter->GetIdentifier();
        const QHash<QString, QString> before = textSnapshot(*book);

        sendAnswer(*dock, answer);
        QWidget *card = dock->findChild<QWidget *>(QStringLiteral("agentAnswerCard"));
        auto *body = card ? card->findChild<QLabel *>(QStringLiteral("agentAnswerCardBody")) : nullptr;
        require(body && card->property("markdownState").toString() == QLatin1String("rendered"),
                "The answer did not render as Markdown");
        require(card->property("rawMarkdown").toString() == answer,
                "The card did not keep the original answer");
        const int file_links = card->property("locationFileLinks").toInt();
        const int line_links = card->property("locationLineLinks").toInt();
        require(file_links == expected.value(QStringLiteral("file_links")).toInt()
                    && line_links == expected.value(QStringLiteral("line_links")).toInt()
                    && card->property("locationUnboundLineRefs").toInt() == 0,
                "Unexpected link counts: files " + std::to_string(file_links)
                    + ", lines " + std::to_string(line_links));

        const QList<Anchor> links = anchors(body);
        int verified_lines = 0;
        Anchor first_line;
        Anchor file_anchor;
        for (const Anchor &anchor : links) {
            if (anchor.text == chapter_path) {
                file_anchor = anchor;
                continue;
            }
            const QRegularExpressionMatch match =
                QRegularExpression(QStringLiteral("\\AL(\\d+)\\z")).match(anchor.text);
            if (!match.hasMatch()) continue;
            const int line = match.captured(1).toInt();
            emit body->linkActivated(anchor.href);
            require(dock->property("lastLocationStatus").toString() == QLatin1String("exact"),
                    "Link was not exact: " + anchor.text.toStdString());
            require(waitForLine(window, chapter_path, line),
                    "Code View did not reach " + anchor.text.toStdString());
            const QString source_line = chapter->GetText().split(QLatin1Char('\n')).value(line - 1);
            require(source_line == expected_lines.value(QString::number(line)).toString(),
                    "The cursor line is not the cited source line " + anchor.text.toStdString());
            if (first_line.href.isEmpty()) first_line = anchor;
            ++verified_lines;
        }
        require(verified_lines == line_links, "Not every line link was clicked");
        require(!file_anchor.href.isEmpty(), "The file link is missing");
        emit body->linkActivated(file_anchor.href);
        require(waitForLine(window, chapter_path, -1), "The file link did not open the chapter");
        require(!book->IsModified() && textSnapshot(*book) == before,
                "Opening Agent links changed the book");

        const QString original = chapter->GetText();
        const int first_number = first_line.text.mid(1).toInt();
        const int cursor_before = window.GetCurrentContentTab()->GetCursorLine();
        const int first_newline = original.indexOf(QLatin1Char('\n'));
        chapter->SetText(original.left(first_newline + 1) + QStringLiteral("<!-- edit -->\n")
                         + original.mid(first_newline + 1));
        emit body->linkActivated(first_line.href);
        MainApplication::processEvents();
        auto *notice = dock->findChild<QWidget *>(QStringLiteral("agentLocationNotice"));
        auto *open_file = dock->findChild<QPushButton *>(QStringLiteral("agentLocationOpenFileButton"));
        require(dock->property("lastLocationStatus").toString() == QLatin1String("content_changed")
                    && notice && notice->isVisible() && open_file && open_file->isVisible()
                    && window.GetCurrentContentTab()->GetCursorLine() == cursor_before,
                "A stale line link moved the cursor or did not explain the change");
        open_file->click();
        require(waitForLine(window, chapter_path, -1), "Open file did not open the changed chapter");

        chapter->SetText(original);
        emit body->linkActivated(first_line.href);
        require(dock->property("lastLocationStatus").toString() == QLatin1String("exact")
                    && waitForLine(window, chapter_path, first_number),
                "Restoring the text did not restore exact navigation");

        require(chapter->RenameTo(QStringLiteral("Renamed001.xhtml")), "Rename failed");
        const QString renamed_path = chapter->GetRelativePath();
        require(renamed_path != chapter_path && chapter->GetIdentifier() == chapter_id,
                "Rename did not keep the resource identity");
        emit body->linkActivated(first_line.href);
        require(dock->property("lastLocationStatus").toString() == QLatin1String("exact")
                    && waitForLine(window, renamed_path, first_number),
                "A link did not follow its renamed resource");

        std::cout << "Agent conversation navigation passed: " << verified_lines
                  << " source-line links and " << file_links
                  << " file link opened the cited Code View lines; stale, restored and renamed targets checked\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
