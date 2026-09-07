#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebEngineUrlScheme>
#include <QWebEngineSettings>
#include <iostream>
#include <stdexcept>

#include "MainUI/MainWindow.h"
#include "MainUI/MainApplication.h"
#include "Misc/PluginDB.h"
#include "Misc/SettingsStore.h"
#include "Misc/WebProfileMgr.h"
#include "PluginAPI/PluginSessionManager.h"
#include "ResourceObjects/OPFResource.h"
#include "Tabs/TabManager.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

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
        Require(argc == 3, "Expected source root and EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setUseBundledInterp(false);
        PluginDB::instance().set_engine_path("python3.4", qEnvironmentVariable("SIGIL_TEST_EXTERNAL_PYTHON"));
        WebProfileMgr::instance();

        MainWindow window(QString::fromLocal8Bit(argv[2]));
        TabManager tabs(&window);
        PluginSessionManager sessions(&window, &tabs);
        OPFResource *opf = window.GetCurrentBook()->GetOPF();
        const QString before = opf->GetSourceText();
        const QByteArray beforeBytes = opf->GetSourceBytes();
        Require(!window.GetCurrentBook()->IsModified(), "Fixture unexpectedly opened modified");

        const QString success = QString::fromUtf8(R"PY(
def run(plugin):
    metadata = plugin.book.get_metadata()
    spine = plugin.book.get_spine()
    for item in metadata['items']:
        if item['name'] == 'dc:title':
            item['content'] = 'Plugin title 日本 & more'
    tx = plugin.book.transaction(checkpoint='auto')
    tx.update_metadata(metadata['items'], metadata['revision'])
    tx.update_spine(spine['items'], spine['attributes'], spine['revision'])
    preview = tx.preview()
    change = preview['opf_changes'][0]
    assert change['before_sha256'] != change['after_sha256']
    result = tx.commit()
    assert result['checkpoint_created'] and result['checkpoint_book_id']
    print('COMMITTED ' + result['checkpoint_book_id'], flush=True)
    return 0
)PY");
        Require(QFileInfo::exists(PluginDB::launcherRoot() + "/python/live_launcher.py"),
                ("Live launcher not found under " + PluginDB::launcherRoot()).toUtf8().constData());
        QString status, error, output;
        Require(sessions.RunSnippetAndWait(success, &status, &error, 30000, &output),
                ("Successful RPC snippet failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains("COMMITTED "), "Plugin did not report a committed checkpoint");
        const QString expected = QString(before).replace(">Source regression<", ">Plugin title 日本 &amp; more<");
        Require(opf->GetSourceText() == expected, "RPC metadata/spine update changed unrelated OPF source");
        const QString checkpoint = window.RecoveryCheckpointBookId();
        const QString snapshotPath = QDir(qEnvironmentVariable("SIGIL_PREFS_DIR"))
            .filePath("repo/epub_" + checkpoint + "/OEBPS/content.opf");
        QFile snapshot(snapshotPath);
        Require(snapshot.open(QIODevice::ReadOnly), "Recovery checkpoint did not contain the package");
        Require(snapshot.readAll() == beforeBytes,
                "Recovery checkpoint did not retain original package bytes");

        const QString marker = QDir(qEnvironmentVariable("SIGIL_PREFS_DIR")).filePath("package-plan-staged.marker");
        QFile::remove(marker);
        qputenv("SIGIL_PLUGIN_TEST_MARKER", marker.toUtf8());
        const QString stale = QString::fromUtf8(R"PY(
import os
import time
from sigil_live.errors import RevisionConflict
def run(plugin):
    metadata = plugin.book.get_metadata()
    for item in metadata['items']:
        if item['name'] == 'dc:title':
            item['content'] = 'Stale plugin value'
    tx = plugin.book.transaction(checkpoint='auto')
    tx.update_metadata(metadata['items'], metadata['revision'])
    tx.preview()
    print('STAGED', flush=True)
    with open(os.environ['SIGIL_PLUGIN_TEST_MARKER'], 'wb'):
        pass
    time.sleep(1.5)
    try:
        tx.commit()
    except RevisionConflict:
        tx.rollback()
        print('CONFLICT', flush=True)
        return 0
    return 9
)PY");
        QTimer mutation;
        mutation.setInterval(20);
        QObject::connect(&mutation, &QTimer::timeout, [&]() {
            if (!QFileInfo::exists(marker)) return;
            mutation.stop();
            opf->SetText(QString(opf->GetSourceText()).replace("Plugin title 日本 &amp; more", "Host edit"));
        });
        mutation.start();
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(stale, &status, &error, 30000, &output),
                ("Stale RPC snippet failed: " + error + "\n" + output).toUtf8().constData());
        mutation.stop();
        Require(QFile::remove(marker), "Stale-plan coordination marker was not created");
        Require(output.contains("STAGED") && output.contains("CONFLICT"), "Stale plan was not rejected at commit");
        Require(opf->GetSourceText().contains(">Host edit<")
                && !opf->GetSourceText().contains("Stale plugin value"), "Rejected plugin plan overwrote the host edit");
        QString latin = before;
        latin.replace("encoding='UTF-16'", "encoding='ISO-8859-1'");
        latin.replace("é 𠮷", "plain source");
        opf->SetSourceBytes(latin.toLatin1());
        opf->SetText(QString(opf->GetSourceText()).replace("Source regression", "日本語"));
        const QString unencodable = opf->GetSourceText();
        Require(!window.CreateRecoveryCheckpoint(true), "Checkpoint silently replaced unencodable source characters");
        Require(opf->GetSourceText() == unencodable, "Failed checkpoint changed unencodable OPF source");
        std::cout << "Live plugin package source and stale-commit checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
