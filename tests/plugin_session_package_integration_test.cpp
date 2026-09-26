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
#include "Agent/Execution/SigilBookWorkspace.h"
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

        SnippetRunOutcome read_only;
        QString read_error;
        Require(sessions.RunSnippetDetailed(QStringLiteral(
                    "print('READ_REPORT_READY')\nplugin.book.get_revision()\n"),
                    &read_only, &read_error, 30000, true),
                ("Read-only snippet failed: " + read_error).toUtf8().constData());
        if (!read_only.stdoutText.contains(QStringLiteral("READ_REPORT_READY"))
            || read_only.exitCode != 0 || read_only.bookChanged
            || window.GetCurrentBook()->IsModified()) {
            throw std::runtime_error(QStringLiteral(
                "Read-only result: stdout=%1 exit=%2 changed=%3 modified=%4 stderr=%5")
                .arg(read_only.stdoutText, QString::number(read_only.exitCode),
                     QString::number(read_only.bookChanged),
                     QString::number(window.GetCurrentBook()->IsModified()),
                     read_only.stderrText).toStdString());
        }
        SnippetRunOutcome split_utf8;
        read_error.clear();
        Require(sessions.RunSnippetDetailed(QStringLiteral(
                    "import sys, time\n"
                    "b = '汉'.encode('utf-8')\n"
                    "sys.stdout.buffer.write(b[:1]); sys.stdout.buffer.flush()\n"
                    "time.sleep(0.2)\n"
                    "sys.stdout.buffer.write(b[1:]); sys.stdout.buffer.flush()\n"),
                    &split_utf8, &read_error, 30000, true)
                    && split_utf8.stdoutText == QStringLiteral("汉")
                    && split_utf8.stdoutLength == 1,
                "Live stdout must decode UTF-8 characters split across process reads");
        SnippetRunOutcome denied_write;
        read_error.clear();
        Require(!sessions.RunSnippetDetailed(
                    QStringLiteral("plugin.book.transaction()\n"),
                    &denied_write, &read_error, 30000, true)
                    && !denied_write.bookChanged
                    && (read_error.contains(QStringLiteral("Read-only"))
                        || denied_write.stderrText.contains(QStringLiteral("Read-only")))
                    && !window.GetCurrentBook()->IsModified(),
                "Read-only snippet must reject Live Book transactions with a clear error");
        SnippetRunOutcome bad_result;
        read_error.clear();
        Require(!sessions.RunSnippetDetailed(QStringLiteral("result = 'report text'\n"),
                                             &bad_result, &read_error, 30000, true)
                    && read_error.contains(QStringLiteral("Snippet returned"))
                    && bad_result.exitCode != 0 && !bad_result.bookChanged,
                "Nonzero snippet return values must explain the exit-status contract");

        SigilAgent::SigilBookWorkspace agent_workspace;
        agent_workspace.setBook(window.GetCurrentBook());
        agent_workspace.setPluginSessionManager(&sessions);
        const quint64 read_revision = agent_workspace.revision();
        const SigilAgent::BookOpResult zero_result = agent_workspace.runLivePython(
            QStringLiteral("print('ZERO_RESULT_OK')\nresult = 0\n"), 30000,
            QStringLiteral("read"));
        Require(zero_result.ok && !zero_result.applied
                    && zero_result.data.value(QStringLiteral("stdout")).toString()
                        .contains(QStringLiteral("ZERO_RESULT_OK"))
                    && zero_result.data.value(QStringLiteral("exit_status")).toInt() == 0
                    && agent_workspace.revision() == read_revision
                    && !window.GetCurrentBook()->IsModified(),
                "Read-only result=0 must return stdout without changing Agent revision or Book");
        const SigilAgent::BookOpResult no_op_edit = agent_workspace.runLivePython(
            QStringLiteral("print('NO_OP_EDIT')\n"), 30000, QStringLiteral("edit"));
        Require(no_op_edit.ok && !no_op_edit.applied
                    && no_op_edit.data.value(QStringLiteral("stdout")).toString()
                        .contains(QStringLiteral("NO_OP_EDIT"))
                    && agent_workspace.revision() == read_revision
                    && !window.GetCurrentBook()->IsModified(),
                "An edit-mode snippet without a commit must not invent a book revision");
        const SigilAgent::BookOpResult long_output = agent_workspace.runLivePython(
            QStringLiteral("print('A' * 100000)\n"), 30000, QStringLiteral("read"));
        Require(long_output.ok && !long_output.applied
                    && long_output.data.value(QStringLiteral("stdout_length")).toInt() == 100001
                    && long_output.data.value(QStringLiteral("stdout")).toString().size() == 8000
                    && long_output.data.value(QStringLiteral("stdout_truncated")).toBool()
                    && agent_workspace.revision() == read_revision,
                "Live Python output over the budget must report the original length and truncation");
        SnippetRunOutcome bounded_capture;
        read_error.clear();
        Require(sessions.RunSnippetDetailed(QStringLiteral("print('B' * 100000)\n"),
                                            &bounded_capture, &read_error, 30000, true)
                    && bounded_capture.stdoutLength == 100001
                    && bounded_capture.stdoutText.size() == 32768
                    && !bounded_capture.bookChanged,
                "Live session must bound captured stdout while counting its full length");
        const SigilAgent::BookOpResult noisy_failure = agent_workspace.runLivePython(
            QStringLiteral("import sys\nprint('A' * 10000)\n"
                           "sys.stderr.write('SCRIPT_DIAGNOSTIC')\nresult = 1\n"),
            30000, QStringLiteral("read"));
        Require(!noisy_failure.ok && noisy_failure.data.value(QStringLiteral("stdout_truncated")).toBool()
                    && noisy_failure.data.value(QStringLiteral("stderr")).toString()
                        .contains(QStringLiteral("SCRIPT_DIAGNOSTIC"))
                    && noisy_failure.data.value(QStringLiteral("stderr_length")).toInt() >= 17
                    && agent_workspace.revision() == read_revision,
                "Python failure diagnostics must remain visible after large stdout");
        const SigilAgent::BookOpResult timed_out = agent_workspace.runLivePython(
            QStringLiteral("import time\ntime.sleep(10)\n"), 1000, QStringLiteral("read"));
        Require(!timed_out.ok && timed_out.code == QStringLiteral("LIVE_PYTHON_FAILED")
                    && timed_out.message.contains(QStringLiteral("timed out"), Qt::CaseInsensitive)
                    && !timed_out.data.value(QStringLiteral("book_changed")).toBool()
                    && agent_workspace.revision() == read_revision,
                "Timed-out read-only Python must report a specific error without a revision change");

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
        SnippetRunOutcome changed;
        Require(sessions.RunSnippetDetailed(success, &changed, &error, 30000),
                ("Successful RPC snippet failed: " + error + "\n" + changed.output).toUtf8().constData());
        status = changed.status;
        output = changed.output;
        Require(changed.bookChanged, "Committed Live transaction must report a Book change");
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
