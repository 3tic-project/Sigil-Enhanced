#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWebEngineSettings>
#include <QWebEngineUrlScheme>
#include <iostream>
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
#include "ResourceObjects/OPFResource.h"
#include "Tabs/TabManager.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QByteArray ReadFile(const QString &path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Could not read recovery fixture file");
    return file.readAll();
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
        PluginDB::instance().set_engine_path(
            "python3.4", qEnvironmentVariable("SIGIL_TEST_EXTERNAL_PYTHON"));
        WebProfileMgr::instance();

        MainWindow window(QString::fromLocal8Bit(argv[2]));
        TabManager tabs(&window);
        PluginSessionManager sessions(&window, &tabs);
        const QSharedPointer<Book> book = window.GetCurrentBook();
        FolderKeeper *keeper = book->GetFolderKeeper();
        OPFResource *opf = book->GetOPF();
        auto *chapter = qobject_cast<HTMLResource *>(
            keeper->GetResourceByBookPath("OEBPS/a.xhtml"));
        auto *nav = qobject_cast<HTMLResource *>(
            keeper->GetResourceByBookPath("OEBPS/nav.xhtml"));
        Require(opf && chapter && nav, "Fixture resources are missing");
        const QString packageBefore = opf->GetSourceText();
        const QByteArray packageBytesBefore = opf->GetSourceBytes();
        const QString chapterBefore = chapter->GetText();
        const QString navBefore = nav->GetText();
        const int resourcesBefore = keeper->GetResourceList().size();
        const bool modifiedBefore = book->IsModified();

        // Permit the physical resource addition, then fail after the matching
        // manifest/spine update so both halves require compensation.
        sessions.SetCommitFailureAfterMutationForTesting(1);
        const QString structureFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    tx = plugin.book.transaction('injected structure failure', checkpoint='auto')
    tx.add_resource(
        'OEBPS/injected-failure.xhtml',
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>New</title></head><body><p>new</p></body></html>',
        'application/xhtml+xml', manifest_id='injected-failure', add_to_spine=True)
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('STRUCTURE_ROLLED_BACK', flush=True)
        return 0
    return 9
)PY");
        QString status, error, output;
        Require(sessions.RunSnippetAndWait(structureFailure, &status, &error, 30000, &output),
                ("Injected structure plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("STRUCTURE_ROLLED_BACK")),
                "Structure failure was not reported as compensated");
        Require(!sessions.HasWriter(), "Structure failure retained the global writer lease");
        Require(keeper->GetResourceList().size() == resourcesBefore
                    && !keeper->GetResourceByBookPathNoThrow(
                        QStringLiteral("OEBPS/injected-failure.xhtml"))
                    && !QFileInfo::exists(QDir(keeper->GetFullPathToMainFolder()).filePath(
                        QStringLiteral("OEBPS/injected-failure.xhtml"))),
                "Structure failure left a managed resource or file behind");
        Require(opf->GetSourceText() == packageBefore,
                "Structure failure changed the package source");
        Require(book->IsModified() == modifiedBefore,
                "Structure failure changed the Book modified state");
        const QString checkpoint = window.RecoveryCheckpointBookId();
        Require(!checkpoint.isEmpty(), "Required failure checkpoint has no recovery identity");
        const QString checkpointOpf = QDir(qEnvironmentVariable("SIGIL_PREFS_DIR"))
            .filePath("repo/epub_" + checkpoint + "/OEBPS/content.opf");
        Require(ReadFile(checkpointOpf) == packageBytesBefore,
                "Failure checkpoint did not retain pre-commit package bytes");

        sessions.SetCommitFailureAfterMutationForTesting(0);
        const QString packageFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    metadata = plugin.book.get_metadata()
    for item in metadata['items']:
        if item['name'] == 'dc:title':
            item['content'] = 'injected stale package value'
    tx = plugin.book.transaction('injected package failure', checkpoint='auto')
    tx.update_metadata(metadata['items'], metadata['revision'])
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('PACKAGE_ROLLED_BACK', flush=True)
        return 0
    return 7
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(packageFailure, &status, &error, 30000, &output),
                ("Injected package plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("PACKAGE_ROLLED_BACK")),
                "Package failure was not reported as compensated");
        Require(!sessions.HasWriter(), "Package failure retained the global writer lease");
        Require(opf->GetSourceText() == packageBefore && opf->GetSourceBytes() == packageBytesBefore,
                "Package failure did not restore the exact pre-commit source bytes");
        Require(book->IsModified() == modifiedBefore,
                "Package failure changed the Book modified state");

        sessions.SetCommitFailureAfterMutationForTesting(0);
        const QString textFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    chapter = plugin.book.resolve_path('OEBPS/a.xhtml')
    nav = plugin.book.resolve_path('OEBPS/nav.xhtml')
    tx = plugin.book.transaction('injected text failure', checkpoint='auto')
    first = tx.read_text(chapter)
    second = tx.read_text(nav)
    tx.replace_text(chapter, first['text'].replace('Original paragraph', 'stale chapter'), first['revision'])
    tx.replace_text(nav, second['text'].replace('Contents', 'stale nav'), second['revision'])
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('TEXT_ROLLED_BACK', flush=True)
        return 0
    return 8
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(textFailure, &status, &error, 30000, &output),
                ("Injected text plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("TEXT_ROLLED_BACK")),
                "Text failure was not reported as compensated");
        Require(!sessions.HasWriter(), "Text failure retained the global writer lease");
        Require(chapter->GetText() == chapterBefore && nav->GetText() == navBefore,
                "Text failure left one staged resource applied");
        Require(opf->GetSourceText() == packageBefore,
                "Text failure changed unrelated package source");
        Require(book->IsModified() == modifiedBefore,
                "Text failure changed the Book modified state");

        const QString createRemovalTarget = QString::fromUtf8(R"PY(
def run(plugin):
    tx = plugin.book.transaction('create removal target', checkpoint='auto')
    tx.add_resource(
        'OEBPS/removal-target.css',
        'body { color: #123456; }\n',
        'text/css',
        manifest_id='removal-target',
        add_to_spine=False,
    )
    tx.add_resource(
        'OEBPS/removal-target-2.css',
        'body { background: #abcdef; }\n',
        'text/css',
        manifest_id='removal-target-2',
        add_to_spine=False,
    )
    tx.add_resource(
        'OEBPS/binary-target.png',
        b'\x89PNG\r\n\x1a\noriginal-binary',
        'image/png',
        manifest_id='binary-target',
        add_to_spine=False,
    )
    tx.add_resource(
        'OEBPS/archive-target.bin',
        b'original-unmanaged-archive',
        'application/octet-stream',
        manifested=False,
        add_to_spine=False,
    )
    result = tx.commit()
    assert result['added'] == 4
    print('REMOVAL_TARGET_CREATED', flush=True)
    return 0
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(createRemovalTarget, &status, &error, 30000, &output),
                ("Removal-target plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("REMOVAL_TARGET_CREATED")) && !sessions.HasWriter(),
                "Removal target was not created or retained the writer lease");
        Resource *removalTarget = keeper->GetResourceByBookPathNoThrow(
            QStringLiteral("OEBPS/removal-target.css"));
        Resource *removalTarget2 = keeper->GetResourceByBookPathNoThrow(
            QStringLiteral("OEBPS/removal-target-2.css"));
        Resource *binaryTarget = keeper->GetResourceByBookPathNoThrow(
            QStringLiteral("OEBPS/binary-target.png"));
        Require(removalTarget && removalTarget2 && binaryTarget,
                "Removal targets were not added to the live Book");
        const QByteArray removalBytesBefore = ReadFile(removalTarget->GetFullPath());
        const QByteArray removalBytesBefore2 = ReadFile(removalTarget2->GetFullPath());
        const QByteArray binaryBytesBefore = ReadFile(binaryTarget->GetFullPath());
        const QString archiveTargetPath = QDir(keeper->GetFullPathToMainFolder()).filePath(
            QStringLiteral("OEBPS/archive-target.bin"));
        const QByteArray archiveBytesBefore = ReadFile(archiveTargetPath);

        sessions.SetCommitFailureAfterMutationForTesting(0);
        const QString binaryFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    target = plugin.book.resolve_path('OEBPS/binary-target.png')
    tx = plugin.book.transaction('injected binary failure', checkpoint='auto')
    current = tx.read_binary(target)
    tx.write_binary(target, b'\xffstale-binary\x00', current['revision'])
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('BINARY_ROLLED_BACK', flush=True)
        return 0
    return 5
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(binaryFailure, &status, &error, 30000, &output),
                ("Injected binary plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("BINARY_ROLLED_BACK")) && !sessions.HasWriter(),
                "Binary failure was not compensated or retained the writer lease");
        Require(keeper->GetResourceByBookPathNoThrow(
                    QStringLiteral("OEBPS/binary-target.png")) == binaryTarget
                    && ReadFile(binaryTarget->GetFullPath()) == binaryBytesBefore,
                "Binary failure did not restore the resource bytes");

        sessions.SetCommitFailureAfterMutationForTesting(0);
        const QString archiveFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    current = plugin.book.read_archive_file('OEBPS/archive-target.bin')
    tx = plugin.book.transaction('injected archive failure', checkpoint='auto')
    tx.replace_archive_file(
        'OEBPS/archive-target.bin', b'stale-unmanaged-archive', current['sha256'])
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('ARCHIVE_ROLLED_BACK', flush=True)
        return 0
    return 4
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(archiveFailure, &status, &error, 30000, &output),
                ("Injected archive plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("ARCHIVE_ROLLED_BACK")) && !sessions.HasWriter(),
                "Archive failure was not compensated or retained the writer lease");
        Require(ReadFile(archiveTargetPath) == archiveBytesBefore,
                "Archive failure did not restore the original file bytes");

        const QString addRemovalReference = QString::fromUtf8(R"PY(
def run(plugin):
    chapter = plugin.book.resolve_path('OEBPS/a.xhtml')
    tx = plugin.book.transaction('add relocation reference', checkpoint='auto')
    current = tx.read_text(chapter)
    updated = current['text'].replace(
        '<head>', '<head><link rel="stylesheet" href="removal-target.css"/>', 1)
    tx.replace_text(chapter, updated, current['revision'])
    tx.commit()
    print('RELOCATION_REFERENCE_CREATED', flush=True)
    return 0
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(addRemovalReference, &status, &error, 30000, &output),
                ("Relocation-reference plugin failed: " + error + "\n" + output)
                    .toUtf8().constData());
        Require(output.contains(QStringLiteral("RELOCATION_REFERENCE_CREATED"))
                    && chapter->GetText().contains(QStringLiteral("removal-target.css"))
                    && !sessions.HasWriter(),
                "Relocation reference was not committed");

        const QString removalPackageBefore = opf->GetSourceText();
        const QString relocationChapterBefore = chapter->GetText();
        const int removalResourcesBefore = keeper->GetResourceList().size();
        const bool removalModifiedBefore = book->IsModified();

        // Permit the filesystem move and OPF patch, then fail after updating
        // non-OPF references. Every layer must return to the original path.
        sessions.SetCommitFailureAfterMutationForTesting(2);
        const QString relocationFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    target = plugin.book.resolve_path('OEBPS/removal-target.css')
    tx = plugin.book.transaction('injected relocation failure', checkpoint='auto')
    tx.move_resource(target, 'OEBPS/moved-removal-target.css')
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('RELOCATION_ROLLED_BACK', flush=True)
        return 0
    return 3
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(relocationFailure, &status, &error, 30000, &output),
                ("Injected relocation plugin failed: " + error + "\n" + output)
                    .toUtf8().constData());
        Require(output.contains(QStringLiteral("RELOCATION_ROLLED_BACK")) && !sessions.HasWriter(),
                "Relocation failure was not compensated or retained the writer lease");
        Require(keeper->GetResourceByBookPathNoThrow(
                    QStringLiteral("OEBPS/removal-target.css")) == removalTarget
                    && !keeper->GetResourceByBookPathNoThrow(
                        QStringLiteral("OEBPS/moved-removal-target.css"))
                    && ReadFile(removalTarget->GetFullPath()) == removalBytesBefore,
                "Relocation failure did not restore the resource path and bytes");
        Require(opf->GetSourceText() == removalPackageBefore
                    && chapter->GetText() == relocationChapterBefore,
                "Relocation failure did not restore package and content references");
        Require(book->IsModified() == removalModifiedBefore,
                "Relocation failure changed the Book modified state");

        // Allow the OPF removal patch, then fail after the disposable resource
        // file has been deleted. The host must restore both and keep the same
        // Resource object in the live Book.
        sessions.SetCommitFailureAfterMutationForTesting(1);
        const QString removalFailure = QString::fromUtf8(R"PY(
from sigil_live.errors import ValidationFailed
def run(plugin):
    target = plugin.book.resolve_path('OEBPS/removal-target.css')
    target2 = plugin.book.resolve_path('OEBPS/removal-target-2.css')
    tx = plugin.book.transaction('injected removal failure', checkpoint='auto')
    tx.remove_resource(target)
    tx.remove_resource(target2)
    try:
        tx.commit()
    except ValidationFailed as error:
        assert 'all applied changes were rolled back' in str(error)
        print('REMOVAL_ROLLED_BACK', flush=True)
        return 0
    return 6
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(removalFailure, &status, &error, 30000, &output),
                ("Injected removal plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("REMOVAL_ROLLED_BACK")),
                "Removal failure was not reported as compensated");
        Require(!sessions.HasWriter(), "Removal failure retained the global writer lease");
        Require(keeper->GetResourceByBookPathNoThrow(
                    QStringLiteral("OEBPS/removal-target.css")) == removalTarget
                    && keeper->GetResourceByBookPathNoThrow(
                        QStringLiteral("OEBPS/removal-target-2.css")) == removalTarget2
                    && ReadFile(removalTarget->GetFullPath()) == removalBytesBefore
                    && ReadFile(removalTarget2->GetFullPath()) == removalBytesBefore2,
                "Removal failure did not restore every resource and file byte");
        Require(opf->GetSourceText() == removalPackageBefore,
                "Removal failure did not restore the package manifest/spine");
        Require(keeper->GetResourceList().size() == removalResourcesBefore
                    && book->IsModified() == removalModifiedBefore,
                "Removal failure changed the Book structure or modified state");

        const QString writerProbe = QString::fromUtf8(R"PY(
def run(plugin):
    tx = plugin.book.transaction('writer release probe', checkpoint='none')
    tx.rollback()
    print('WRITER_REACQUIRED', flush=True)
    return 0
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(writerProbe, &status, &error, 30000, &output),
                ("Writer probe failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("WRITER_REACQUIRED")) && !sessions.HasWriter(),
                "A compensated failure prevented a later transaction");

        const QString successfulRemoval = QString::fromUtf8(R"PY(
def run(plugin):
    target = plugin.book.resolve_path('OEBPS/removal-target.css')
    target2 = plugin.book.resolve_path('OEBPS/removal-target-2.css')
    binary_target = plugin.book.resolve_path('OEBPS/binary-target.png')
    chapter = plugin.book.resolve_path('OEBPS/a.xhtml')
    archive = plugin.book.read_archive_file('OEBPS/archive-target.bin')
    tx = plugin.book.transaction('successful atomic removal', checkpoint='auto')
    current = tx.read_text(chapter)
    tx.replace_text(
        chapter,
        current['text'].replace('<link rel="stylesheet" href="removal-target.css"/>', '', 1),
        current['revision'],
    )
    tx.remove_resource(target)
    tx.remove_resource(target2)
    tx.remove_resource(binary_target)
    tx.remove_archive_file('OEBPS/archive-target.bin', archive['sha256'])
    result = tx.commit()
    assert result['removed'] == 4
    print('REMOVAL_COMMITTED', flush=True)
    return 0
)PY");
        output.clear();
        error.clear();
        Require(sessions.RunSnippetAndWait(successfulRemoval, &status, &error, 30000, &output),
                ("Successful removal plugin failed: " + error + "\n" + output).toUtf8().constData());
        Require(output.contains(QStringLiteral("REMOVAL_COMMITTED")) && !sessions.HasWriter(),
                "Atomic removal did not commit or release its writer");
        Require(!keeper->GetResourceByBookPathNoThrow(QStringLiteral("OEBPS/removal-target.css"))
                    && !keeper->GetResourceByBookPathNoThrow(
                        QStringLiteral("OEBPS/removal-target-2.css"))
                    && !keeper->GetResourceByBookPathNoThrow(
                        QStringLiteral("OEBPS/binary-target.png"))
                    && !QFileInfo::exists(QDir(keeper->GetFullPathToMainFolder()).filePath(
                        QStringLiteral("OEBPS/removal-target.css")))
                    && !QFileInfo::exists(QDir(keeper->GetFullPathToMainFolder()).filePath(
                        QStringLiteral("OEBPS/removal-target-2.css")))
                    && !QFileInfo::exists(archiveTargetPath)
                    && keeper->GetResourceList().size() == resourcesBefore,
                "Successful removal retained the Resource model entry or file");
        Require(!opf->GetSourceText().contains(QStringLiteral("removal-target")),
                "Successful removal retained its manifest entry");
        Require(chapter->GetText() == chapterBefore,
                "Successful cleanup retained the temporary stylesheet reference");
        Require(book->IsModified(), "Successful removal did not mark the Book modified");

        std::cout << "Live transaction fault compensation checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
