#include "zip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Misc/SafeArchiveExtractor.h"
#include "PluginAPI/PluginInputValidator.h"

namespace
{

struct ArchiveEntry {
    QString path;
    QByteArray data;
    quint32 externalAttributes = 0;
};

bool createArchive(const QString& path, const QList<ArchiveEntry>& entries)
{
    zipFile archive = zipOpen64(path.toUtf8().constData(), APPEND_STATUS_CREATE);
    if (!archive) {
        return false;
    }
    for (const ArchiveEntry& entry : entries) {
        zip_fileinfo info = {};
        const QByteArray name = entry.path.toUtf8();
        info.external_fa = entry.externalAttributes;
        const int openResult = entry.externalAttributes == 0 ?
            zipOpenNewFileInZip64(archive, name.constData(), &info, nullptr, 0,
                                  nullptr, 0, nullptr, Z_DEFLATED,
                                  Z_DEFAULT_COMPRESSION, 0) :
            zipOpenNewFileInZip4_64(archive, name.constData(), &info, nullptr, 0,
                                    nullptr, 0, nullptr, Z_DEFLATED,
                                    Z_DEFAULT_COMPRESSION, 0, -MAX_WBITS,
                                    DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, nullptr, 0,
                                    (3 << 8) | 20, 0, 0);
        if (openResult != ZIP_OK) {
            zipClose(archive, nullptr);
            return false;
        }
        if (!entry.data.isEmpty() &&
            zipWriteInFileInZip(archive, entry.data.constData(),
                                static_cast<unsigned int>(entry.data.size())) != ZIP_OK) {
            zipCloseFileInZip(archive);
            zipClose(archive, nullptr);
            return false;
        }
        if (zipCloseFileInZip(archive) != ZIP_OK) {
            zipClose(archive, nullptr);
            return false;
        }
    }
    return zipClose(archive, nullptr) == ZIP_OK;
}

bool expect(bool condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

SafeArchiveExtractor::Result extractEntries(
    const QList<ArchiveEntry>& entries,
    QTemporaryDir& workspace,
    const SafeArchiveExtractor::Limits& limits = SafeArchiveExtractor::Limits(),
    const SafeArchiveExtractor::CancelCheck& cancelled = {})
{
    const QString archivePath = QDir(workspace.path()).filePath("sample.zip");
    const QString outputPath = QDir(workspace.path()).filePath("output");
    QDir().mkpath(outputPath);
    if (!createArchive(archivePath, entries)) {
        return {};
    }
    return SafeArchiveExtractor::extract(archivePath, outputPath, limits, cancelled);
}

bool testSafePaths()
{
    QTemporaryDir workspace;
    const QString root = workspace.path();
    QString target;
    QString precomposed;
    QString decomposed;
#if defined(Q_OS_UNIX)
    const QString outside = QDir(root).filePath("outside");
    const QString link = QDir(root).filePath("link");
    QDir().mkdir(outside);
    const bool symlinkCreated = QFile::link(outside, link);
#endif
    return expect(SafeArchiveExtractor::safeArchivePath(root, "OPS/chapter.xhtml", &target),
                  "normal nested path rejected") &&
           expect(target.startsWith(root + '/'), "normal path escaped root") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "../escape"),
                  "parent traversal accepted") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "/absolute"),
                  "absolute path accepted") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "C:/drive"),
                  "drive path accepted") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "dir\\..\\escape"),
                  "backslash traversal accepted") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "OPS//file"),
                  "empty path segment accepted") &&
           expect(!SafeArchiveExtractor::safeArchivePath(root, "AUX.txt"),
                  "Windows device name accepted") &&
           expect(SafeArchiveExtractor::safeArchivePath(
                      root, QString::fromUtf8("caf\xc3\xa9"), nullptr, &precomposed),
                  "precomposed Unicode path rejected") &&
           expect(SafeArchiveExtractor::safeArchivePath(
                      root, QString::fromUtf8("cafe\xcc\x81"), nullptr, &decomposed),
                  "decomposed Unicode path rejected") &&
           expect(precomposed == decomposed,
                  "Unicode paths were not normalized consistently")
#if defined(Q_OS_UNIX)
           && expect(symlinkCreated, "could not create symlink test fixture")
           && expect(!SafeArchiveExtractor::safeArchivePath(root, "link/escape"),
                     "existing parent symlink accepted")
#endif
        ;
}

bool testNormalExtraction()
{
    QTemporaryDir workspace;
    const auto result = extractEntries({{"OPS/chapter.xhtml", "chapter"},
                                        {"META-INF/container.xml", "container"}},
                                       workspace);
    const QString output = QDir(workspace.path()).filePath("output/OPS/chapter.xhtml");
    QFile file(output);
    return expect(result.ok, "normal archive rejected") &&
           expect(result.entries.size() == 2, "normal archive entry count changed") &&
           expect(file.open(QIODevice::ReadOnly), "normal output missing") &&
           expect(file.readAll() == "chapter", "normal output content changed");
}

bool testPathRejectionAndCleanup()
{
    QTemporaryDir workspace;
    const auto result = extractEntries({{"good.txt", "written first"},
                                        {"../escape.txt", "blocked"}},
                                       workspace);
    return expect(!result.ok, "unsafe archive accepted") &&
           expect(result.error == SafeArchiveExtractor::Error::InvalidPath,
                  "unsafe archive returned wrong error") &&
           expect(!QFileInfo::exists(QDir(workspace.path()).filePath("output/good.txt")),
                  "partial output not cleaned") &&
           expect(!QFileInfo::exists(QDir(workspace.path()).filePath("escape.txt")),
                  "path traversal wrote outside root");
}

bool testNormalPluginLayout()
{
    QTemporaryDir workspace;
    const auto result = extractEntries({{"example/plugin.xml", "<plugin/>"},
                                        {"example/plugin.py", "print('ok')"}},
                                       workspace);
    return expect(result.ok, "normal plugin archive rejected") &&
           expect(QFileInfo::exists(
                      QDir(workspace.path()).filePath("output/example/plugin.xml")),
                  "normal plugin manifest missing");
}

bool testRepositoryArchives()
{
    const QString sourceRoot = QStringLiteral(SIGIL_TEST_SOURCE_DIR);
    const QStringList samples = {
        QDir(sourceRoot).filePath("docs/testplugin_v020.zip"),
        QDir(sourceRoot).filePath("docs/Sigil_Plugin_Framework_rev15.epub")
    };
    for (const QString& sample : samples) {
        QTemporaryDir output;
        if (!expect(QFileInfo::exists(sample), "repository archive fixture missing")) {
            return false;
        }
        const auto result = SafeArchiveExtractor::extract(sample, output.path());
        if (!expect(result.ok, "repository archive fixture rejected")) {
            fprintf(stderr, "Fixture error: %s\n",
                    SafeArchiveExtractor::errorMessage(result).toUtf8().constData());
            return false;
        }
    }
    return true;
}

bool testBudgets()
{
    SafeArchiveExtractor::Limits limits;
    QTemporaryDir countWorkspace;
    limits.maxEntries = 1;
    auto result = extractEntries({{"one", "1"}, {"two", "2"}}, countWorkspace, limits);
    if (!expect(result.error == SafeArchiveExtractor::Error::EntryCountLimit,
                "file-count budget not enforced")) {
        return false;
    }

    QTemporaryDir singleWorkspace;
    limits = {};
    limits.maxSingleFileBytes = 3;
    result = extractEntries({{"large", "1234"}}, singleWorkspace, limits);
    if (!expect(result.error == SafeArchiveExtractor::Error::SingleFileLimit,
                "single-file budget not enforced")) {
        return false;
    }

    QTemporaryDir totalWorkspace;
    limits = {};
    limits.maxTotalBytes = 6;
    result = extractEntries({{"one", "1234"}, {"two", "5678"}}, totalWorkspace, limits);
    if (!expect(result.error == SafeArchiveExtractor::Error::TotalSizeLimit,
                "total-size budget not enforced")) {
        return false;
    }

    QTemporaryDir ratioWorkspace;
    limits = {};
    limits.maxCompressionRatio = 2;
    result = extractEntries({{"compressed", QByteArray(4096, '\0')}}, ratioWorkspace, limits);
    if (!expect(result.error == SafeArchiveExtractor::Error::CompressionRatioLimit,
                "compression-ratio budget not enforced")) {
        return false;
    }

    QTemporaryDir depthWorkspace;
    limits = {};
    limits.maxPathDepth = 2;
    result = extractEntries({{"a/b/c", "content"}}, depthWorkspace, limits);
    if (!expect(result.error == SafeArchiveExtractor::Error::InvalidPath,
                "path-depth budget not enforced")) {
        return false;
    }

    QTemporaryDir lengthWorkspace;
    limits = {};
    limits.maxPathLength = 4;
    result = extractEntries({{"12345", "content"}}, lengthWorkspace, limits);
    return expect(result.error == SafeArchiveExtractor::Error::InvalidPath,
                  "path-length budget not enforced");
}

bool testDuplicateRejection()
{
    QTemporaryDir workspace;
    const auto result = extractEntries({{"same", "first"}, {"same", "second"}},
                                       workspace);
    return expect(result.error == SafeArchiveExtractor::Error::DuplicatePath,
                  "duplicate archive path accepted") &&
           expect(!QFileInfo::exists(QDir(workspace.path()).filePath("output/same")),
                  "duplicate-path failure left partial output");
}

bool testSymbolicLinkRejection()
{
    QTemporaryDir workspace;
    const quint32 symbolicLinkMode = static_cast<quint32>(0120777) << 16;
    const auto result = extractEntries({{"link", "target", symbolicLinkMode}}, workspace);
    return expect(result.error == SafeArchiveExtractor::Error::SymbolicLink,
                  "symbolic-link archive entry accepted") &&
           expect(!QFileInfo::exists(QDir(workspace.path()).filePath("output/link")),
                  "symbolic-link archive entry was written");
}

bool testCancellation()
{
    QTemporaryDir workspace;
    int checks = 0;
    SafeArchiveExtractor::Limits limits;
    limits.maxCompressionRatio = 10000;
    const auto result = extractEntries({{"file", QByteArray(32768, 'x')}}, workspace, limits,
                                       [&checks]() { return ++checks > 1; });
    return expect(result.error == SafeArchiveExtractor::Error::Cancelled,
                  "cancellation not enforced") &&
           expect(!QFileInfo::exists(QDir(workspace.path()).filePath("output/file")),
                  "cancelled output not cleaned");
}

bool testPluginInputValidation()
{
    const QByteArray container = QByteArrayLiteral(
        "<?xml version='1.0'?><container><rootfiles>"
        "<rootfile full-path='EPUB/content.opf'/></rootfiles></container>");
    const QByteArray package = QByteArrayLiteral(
        "<?xml version='1.0'?><package version='3.0'>"
        "<metadata/><manifest/><spine/></package>");
    QTemporaryDir valid_workspace;
    const QString valid_path = QDir(valid_workspace.path()).filePath("valid.epub");
    if (!expect(createArchive(valid_path, {
                    {"mimetype", "application/epub+zip"},
                    {"META-INF/container.xml", container},
                    {"EPUB/content.opf", package}
                }), "could not create valid input EPUB")) {
        return false;
    }
    QString error;
    if (!expect(PluginApi::ValidateInputEpub(valid_path, &error),
                "valid input EPUB was rejected")) {
        fprintf(stderr, "Validator error: %s\n", error.toUtf8().constData());
        return false;
    }

    QTemporaryDir invalid_workspace;
    const QString invalid_path = QDir(invalid_workspace.path()).filePath("invalid.epub");
    return expect(createArchive(invalid_path, {{"payload.bin", "PK-like payload"}}),
                  "could not create invalid input EPUB") &&
           expect(!PluginApi::ValidateInputEpub(invalid_path, &error),
                  "ZIP without EPUB infrastructure was accepted");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const bool ok = testSafePaths() &&
                    testNormalExtraction() &&
                    testNormalPluginLayout() &&
                    testRepositoryArchives() &&
                    testPathRejectionAndCleanup() &&
                    testBudgets() &&
                    testDuplicateRejection() &&
                    testSymbolicLinkRejection() &&
                    testCancellation() &&
                    testPluginInputValidation();
    if (ok) {
        fprintf(stdout, "All safe archive extractor tests passed.\n");
    }
    return ok ? 0 : 1;
}
