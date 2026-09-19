#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BuiltinPlugins/CmoaParagraphNormalizer.h"
#include "BuiltinPlugins/DivParagraphNormalizationPlan.h"
#include "BuiltinPlugins/DivParagraphStylesheetResolver.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "MainUI/PreviewWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/CSSResource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ViewEditors/ViewPreview.h"

using BuiltinPlugins::CmoaParagraphNormalizer;
using BuiltinPlugins::DivParagraphNormalizationPlan;
using BuiltinPlugins::DivParagraphStylesheetResolver;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString withFixedFont(QString source)
{
    const QString fixed = QStringLiteral(
        "<style id=\"sigil-d06-font\">"
        "html,body,body *{font-family:'Hiragino Mincho ProN' !important;}"
        "</style>");
    const int head = source.indexOf(QStringLiteral("</head>"), 0,
                                    Qt::CaseInsensitive);
    require(head >= 0, "Cmoa visual fixture has no closing head element");
    source.insert(head, fixed);
    return source;
}

QVariant evaluate(ViewPreview* view, const QString& javascript)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QVariant result;
    bool completed = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    view->page()->runJavaScript(
        javascript, QWebEngineScript::ApplicationWorld,
        [&](const QVariant& value) {
            result = value;
            completed = true;
            loop.quit();
        });
    loop.exec();
    require(completed, "Sigil Preview metrics JavaScript timed out");
    return result;
}

QVariantMap renderMetrics(ViewPreview* view, const QString& path,
                          const QString& source)
{
    QDeadlineTimer deadline(15000);
    view->CustomSetDocument(path, withFixedFont(source));
    while (!view->IsLoadingFinished() && !deadline.hasExpired()) {
        MainApplication::processEvents();
        QThread::msleep(5);
    }
    require(view->IsLoadingFinished() && view->WasLoadOkay(),
            "Sigil Preview did not finish the visual fixture load");
    for (int attempt = 0; attempt < 10; ++attempt) {
        MainApplication::processEvents();
        QThread::msleep(5);
    }

    const QString javascript = QStringLiteral(R"JS(
(() => {
  window.scrollTo(0, 0);
  const keys = [
    'display', 'writingMode', 'direction', 'fontFamily', 'fontSize',
    'lineHeight', 'textIndent', 'marginBlockStart', 'marginBlockEnd',
    'marginInlineStart', 'marginInlineEnd', 'paddingBlockStart',
    'paddingBlockEnd', 'paddingInlineStart', 'paddingInlineEnd',
    'breakBefore', 'breakAfter', 'textAlign', 'rubyPosition'
  ];
  const inspect = (element) => {
    const style = getComputedStyle(element);
    const rect = element.getBoundingClientRect();
    const result = { tag: element.localName };
    for (const key of keys) result[key] = style[key];
    result.rect = [rect.x, rect.y, rect.width, rect.height];
    return result;
  };
  return {
    viewport: [innerWidth, innerHeight, devicePixelRatio],
    documentSize: [document.documentElement.scrollWidth,
                   document.documentElement.scrollHeight],
    html: inspect(document.documentElement),
    body: inspect(document.body),
    heading: inspect(document.querySelector('h1')),
    blocks: Array.from(document.querySelectorAll('.css_class_18'), inspect)
  };
})()
)JS");
    const QVariant result = evaluate(view, javascript);
    require(result.canConvert<QVariantMap>(),
            "Sigil Preview did not return computed visual metrics");
    return result.toMap();
}

void requireEquivalentMetrics(const QVariantMap& before,
                              const QVariantMap& after,
                              int expected_conversions)
{
    require(before.value(QStringLiteral("viewport")) ==
                after.value(QStringLiteral("viewport")),
            "Sigil Preview viewport changed between visual renders");
    require(before.value(QStringLiteral("documentSize")) ==
                after.value(QStringLiteral("documentSize")),
            "Cmoa conversion changed the rendered document extent");
    for (const QString& key : {QStringLiteral("html"), QStringLiteral("body"),
                               QStringLiteral("heading")}) {
        if (before.value(key) != after.value(key)) {
            std::cerr << key.toStdString() << " before: "
                      << QJsonDocument::fromVariant(before.value(key))
                             .toJson(QJsonDocument::Compact).constData()
                      << "\nafter: "
                      << QJsonDocument::fromVariant(after.value(key))
                             .toJson(QJsonDocument::Compact).constData()
                      << '\n';
        }
        require(before.value(key) == after.value(key),
                "Cmoa conversion changed a page or heading metric");
    }

    const QVariantList before_blocks =
        before.value(QStringLiteral("blocks")).toList();
    const QVariantList after_blocks =
        after.value(QStringLiteral("blocks")).toList();
    require(before_blocks.size() == after_blocks.size(),
            "Cmoa conversion changed the measured block count");
    int conversions = 0;
    for (int index = 0; index < before_blocks.size(); ++index) {
        QVariantMap before_block = before_blocks.at(index).toMap();
        QVariantMap after_block = after_blocks.at(index).toMap();
        const QString before_tag = before_block.take(QStringLiteral("tag")).toString();
        const QString after_tag = after_block.take(QStringLiteral("tag")).toString();
        if (before_tag != after_tag) {
            require(before_tag == QLatin1String("div") &&
                        after_tag == QLatin1String("p"),
                    "Cmoa visual fixture changed an unexpected tag");
            ++conversions;
        }
        require(before_block == after_block,
                "Cmoa conversion changed computed style or geometry");
    }
    require(conversions == expected_conversions,
            "Rendered DIV-to-P conversion count did not match the plan");
}

}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);

    try {
        require(argc == 3, "Expected source root and Cmoa EPUB path");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");

        SettingsStore settings;
        settings.setCleanOn(0);
        settings.setZoomPreview(1.0f);
        MainWindow window(QString::fromLocal8Bit(argv[2]));
        window.resize(1600, 1000);
        auto* preview = window.findChild<PreviewWindow*>();
        auto* view = preview ? preview->findChild<ViewPreview*>(
            QStringLiteral("webpreview")) : nullptr;
        require(preview && view, "Could not find Sigil's real Preview widgets");
        preview->setFloating(true);
        preview->resize(1200, 900);
        preview->show();
        view->SetZoomFactor(1.0f);
        MainApplication::processEvents();

        std::cerr << "Sigil Preview visual fixture ready\n" << std::flush;

        const auto book = window.GetCurrentBook();
        QHash<QString, QString> css_by_path;
        for (CSSResource* css :
             book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true)) {
            css->InitialLoad();
            css_by_path.insert(QDir::cleanPath(css->GetRelativePath()),
                               css->GetText());
        }

        const QStringList visual_paths {
            QStringLiteral("item/xhtml/p-002.xhtml"),
            QStringLiteral("item/xhtml/p-007.xhtml"),
            QStringLiteral("item/xhtml/p-013.xhtml")
        };
        int rendered_conversions = 0;
        for (const QString& path : visual_paths) {
            std::cerr << "Rendering " << path.toStdString() << '\n' << std::flush;
            auto* html = qobject_cast<HTMLResource*>(
                book->GetFolderKeeper()->GetResourceByBookPath(path));
            require(html, "Cmoa visual chapter is missing");
            html->InitialLoad();
            const QString source = html->GetText();
            const auto result = CmoaParagraphNormalizer::normalizeXhtmlText(
                source, CmoaParagraphNormalizer::defaultOptions(),
                DivParagraphStylesheetResolver::resolve(
                    source, path, css_by_path));
            const int conversions = result.before.convertibleLeaves;
            require(result.ok && result.changed && conversions > 0,
                    "Cmoa visual chapter did not produce a safe conversion");
            const QVariantMap before = renderMetrics(
                view, html->GetFullPath(), source);
            const QVariantMap after = renderMetrics(
                view, html->GetFullPath(), result.text);
            requireEquivalentMetrics(before, after, conversions);
            rendered_conversions += conversions;

            const QVariantList viewport = before.value(
                QStringLiteral("viewport")).toList();
            const QString font = before.value(QStringLiteral("body")).toMap()
                .value(QStringLiteral("fontFamily")).toString();
            std::cout << path.toStdString() << ": "
                      << conversions << " conversions, viewport "
                      << viewport.value(0).toInt() << 'x'
                      << viewport.value(1).toInt() << ", font "
                      << font.toStdString() << '\n';
        }
        require(rendered_conversions > 0,
                "Cmoa visual acceptance rendered no conversions");
        require(!book->IsModified(),
                "Read-only Sigil Preview visual acceptance modified the book");
        std::cout << "Sigil Preview Cmoa visual equivalence passed for "
                  << rendered_conversions << " conversions\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
