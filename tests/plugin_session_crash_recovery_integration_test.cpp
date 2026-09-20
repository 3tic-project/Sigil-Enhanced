#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QFile>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>

#include <csignal>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/PluginDB.h"
#include "Misc/SettingsStore.h"
#include "Misc/WebProfileMgr.h"
#include "PluginAPI/PluginSessionManager.h"
#include "ResourceObjects/HTMLResource.h"
#include "Tabs/TabManager.h"

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
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

    require(argc == 3, "Expected source root and disposable EPUB fixture");
    const QString marker_path = qEnvironmentVariable("SIGIL_TEST_MUTATION_MARKER");
    require(!marker_path.isEmpty(), "Missing private crash-test marker path");
    const QString root = QString::fromLocal8Bit(argv[1]);
    auto& python = EmbeddedPython::instance();
    python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
    python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
    python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
    SettingsStore settings;
    settings.setPreserveOPFSource(true);
    settings.setUseBundledInterp(false);
    PluginDB::instance().set_engine_path(
        "python3.4", qEnvironmentVariable("SIGIL_TEST_EXTERNAL_PYTHON"));
    WebProfileMgr::instance();

    MainWindow window(QString::fromLocal8Bit(argv[2]));
    TabManager tabs(&window);
    PluginSessionManager sessions(&window, &tabs);
    const auto book = window.GetCurrentBook();
    FolderKeeper* keeper = book->GetFolderKeeper();
    auto* chapter = qobject_cast<HTMLResource*>(
        keeper->GetResourceByBookPath("OEBPS/a.xhtml"));
    require(chapter, "Crash recovery fixture has no chapter");
    chapter->InitialLoad();
    QString unsaved = chapter->GetText();
    require(unsaved.contains(QStringLiteral("Original paragraph")),
            "Crash recovery fixture chapter has unexpected text");
    unsaved.replace(QStringLiteral("Original paragraph"),
                    QStringLiteral("unsaved before crash"));
    chapter->SetTextAsUndoableEdit(unsaved);
    book->SetModified(true);

    sessions.SetCommitMutationCallbackForTesting([&]() {
        const QString checkpoint = window.RecoveryCheckpointBookId();
        require(!checkpoint.isEmpty(), "Commit mutation preceded its recovery checkpoint");
        QFile marker(marker_path);
        const QByteArray value = (checkpoint + QLatin1Char('\n')
                                  + keeper->GetFullPathToMainFolder()
                                  + QLatin1Char('\n')).toUtf8();
        require(marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                    && marker.write(value) == value.size() && marker.flush(),
                "Could not record the physical commit mutation");
        marker.close();
        std::raise(SIGKILL);
    });

    const QString script = QString::fromUtf8(R"PY(
def run(plugin):
    chapter = plugin.book.resolve_path('OEBPS/a.xhtml')
    tx = plugin.book.transaction('crash after first physical mutation', checkpoint='required')
    current = tx.read_text(chapter)
    assert 'unsaved before crash' in current['text']
    tx.add_resource('OEBPS/partial.css', 'body { color: red; }\n',
                    'text/css', manifest_id='partial-crash', add_to_spine=False)
    tx.replace_text(chapter,
                    current['text'].replace('unsaved before crash', 'after crash'),
                    current['revision'])
    tx.commit()
    return 0
)PY");
    QString status, error, output;
    sessions.RunSnippetAndWait(script, &status, &error, 30000, &output);
    throw std::runtime_error("The crash injection did not terminate the process");
}
