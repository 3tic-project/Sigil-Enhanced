#include "EmbedPython/EmbeddedPython.h"

#include <QApplication>
#include <QFile>
#include <QDateTime>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QRegularExpression>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "Misc/SettingsStore.h"
#include "BookManipulation/CleanSource.h"
#include "ResourceObjects/OPFResource.h"
#include "Widgets/TextDocument.h"
#include "Importers/ImportEPUB.h"
#include "Exporters/ExportEPUB.h"
#include "ResourceObjects/HTMLResource.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static QByteArray Utf16LE(const QString &text)
{
    QByteArray bytes = QByteArray::fromHex("fffe");
    for (const QChar character : text) {
        const ushort value = character.unicode();
        bytes.append(char(value & 0xff));
        bytes.append(char(value >> 8));
    }
    return bytes;
}

static QByteArray ReadBytes(const QString &path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Cannot read test file");
    return file.readAll();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTimer dialog_guard;
    QObject::connect(&dialog_guard, &QTimer::timeout, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible()) {
                std::cerr << "Unexpected modal dialog: " << widget->metaObject()->className() << '\n';
                for (QLabel *label : widget->findChildren<QLabel *>())
                    std::cerr << label->text().toStdString() << '\n';
                for (QTextEdit *details : widget->findChildren<QTextEdit *>())
                    std::cerr << details->toPlainText().toStdString() << '\n';
                // Do not run Python singleton destructors while a Python error
                // handler is itself displaying the dialog with the GIL held.
                std::_Exit(EXIT_FAILURE);
            }
        }
    });
    dialog_guard.start(100);
    try {
        Require(argc == 3, "Expected the source root and EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        auto &python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");
        SettingsStore settings;
        Require(settings.preserveOPFSource(), "An absent preference must enable preservation");
        QTemporaryDir scratch;
        Require(scratch.isValid(), "Cannot create temporary resource directory");
        const QString path = scratch.path() + "/content.opf";
        OPFResource resource(scratch.path(), path, "3.0");
        resource.SetEpubVersion("3.0");
        const QString source = QString::fromUtf8(R"XML(<?xml version='1.0' encoding='UTF-8'?>
<package xmlns='http://www.idpf.org/2007/opf' xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:x='urn:publisher' version='3.0' unique-identifier='bookid'>
  <!-- metadata -->
  <metadata>
    <dc:identifier id='bookid'>urn:test</dc:identifier>
    <dc:title id='title'>Original</dc:title>
    <meta property='dcterms:modified' refines='#title'>2020-01-01T00:00:00Z</meta>
    <meta property='dcterms:modified'>2026-09-01T00:00:00Z</meta>
    <meta property='dcterms:modified'>2026-09-02T00:00:00Z</meta>
    <x:extra x:hint='a > b'>保留𠮷</x:extra>
  </metadata>
  <!-- manifest -->
  <manifest><item id='ncx' href='toc.ncx' media-type='application/x-dtbncx+xml'/></manifest>
  <spine toc='ncx'/>
</package>
)XML");
        resource.SetText(source);
        Require(resource.GetPrimaryBookTitle() == "Original", "The normalized model cannot read the title");
        Require(resource.GetText() == source, "Read-only metadata lookup changed source");

        // An unchanged model update must preserve the document's undo stack.
        auto &document = resource.GetTextDocumentForWriting();
        QTextCursor cursor(&document);
        cursor.setPosition(source.indexOf("Original"));
        cursor.insertText("X");
        const QString with_edit = resource.GetText();
        Require(document.isUndoAvailable(), "Test edit was not recorded");
        resource.UpdateNCXOnSpine("ncx");
        Require(resource.GetText() == with_edit && document.isUndoAvailable(), "No-op update replaced the document");
        document.undo();
        Require(resource.GetText() == source, "No-op update broke undo");

        auto metadata = resource.GetDCMetadata();
        for (auto &entry : metadata) {
            if (entry.m_name == "dc:title") entry.m_content = "Changed & 日本";
        }
        resource.SetDCMetadata(metadata);
        const QString expected = QString(source).replace("Original", "Changed &amp; 日本");
        if (resource.GetText() != expected) std::cerr << resource.GetText().toStdString() << '\n';
        Require(resource.GetText() == expected, "Changing a title also changed unrelated source/order");
        resource.SaveToDisk(false);
        QFile file(path);
        Require(file.open(QIODevice::ReadOnly), "Cannot read saved OPF");
        Require(file.readAll() == expected.toUtf8(), "Saving reformatted the OPF");
        file.close();

        const QString timestamp = resource.AddModificationDateMeta();
        const QDateTime parsed_time = QDateTime::fromString(timestamp, Qt::ISODate);
        Require(parsed_time.isValid() && qAbs(parsed_time.secsTo(QDateTime::currentDateTimeUtc())) < 10,
                "Modification time reinterpreted local wall time as UTC");
        Require(resource.GetText().contains("refines='#title'>2020-01-01T00:00:00Z</meta>"),
                "Updating publication modification time removed refined metadata");
        Require(resource.GetText().contains("<!-- metadata -->") && resource.GetText().contains("<x:extra"),
                "Updating modification time removed comments or extensions");
        OPFParser parsed;
        parsed.parse(CleanSource::ProcessXML(resource.GetText(), "application/oebps-package+xml"));
        int publication_dates = 0;
        for (const auto &entry : parsed.m_metadata) {
            if (entry.m_atts.value("property") == "dcterms:modified" && entry.m_atts.value("refines").isEmpty())
                ++publication_dates;
        }
        Require(publication_dates == 1, "Publication has duplicate modification dates");
        parsed.parse(CleanSource::ProcessXML(source, "application/oebps-package+xml"));
        Require(parsed.m_manifest.size() == 1, "Reusing the parser accumulated stale manifest entries");

        QString raw = QString(source).replace("UTF-8", "UTF-16")
            .replace("<!-- metadata -->", QString::fromUtf8("<!-- e\u0301 -->"))
            .replace("\n", "\r\n");
        raw.replace("<!-- manifest -->\r\n", "<!-- manifest -->\n");
        const QByteArray original_bytes = Utf16LE(raw);
        resource.SetSourceBytes(original_bytes);
        Require(!resource.GetText().contains('\r'), "Raw line endings leaked into editor offsets");
        resource.SaveToDisk(false);
        Require(ReadBytes(path) == original_bytes, "Unchanged save lost UTF-16/BOM/mixed line endings/NFD");
        Require(resource.LoadFromDisk(), "Native UTF-16 reload failed");
        auto encoded_metadata = resource.GetDCMetadata();
        for (auto &entry : encoded_metadata) {
            if (entry.m_name == "dc:title") entry.m_content = "Changed 日本";
        }
        resource.SetDCMetadata(encoded_metadata);
        resource.SaveToDisk(false);
        const QString changed_raw = QString(raw).replace("Original", "Changed 日本");
        Require(ReadBytes(path) == Utf16LE(changed_raw), "Structured edit changed unrelated original bytes");
        QTextCursor manual(&document);
        manual.setPosition(resource.GetText().indexOf("Changed 日本"));
        manual.insertText("Manual ");
        resource.SaveToDisk(false);
        Require(ReadBytes(path) == Utf16LE(QString(changed_raw).replace("Changed 日本", "Manual Changed 日本")),
                "Manual edit did not preserve encoding or source line endings");
        Require(document.isUndoAvailable(), "Saving cleared the manual edit's undo history");
        document.undo();
        resource.SaveToDisk(false);
        Require(ReadBytes(path) == Utf16LE(changed_raw), "Undo did not recover original source bytes");

        const QByteArray latin_bytes = "<?xml version='1.0' encoding='ISO-8859-1'?>\r\n"
            "<package xmlns='http://www.idpf.org/2007/opf' version='3.0'><metadata/><manifest/><spine/></package>";
        resource.SetSourceBytes(latin_bytes);
        resource.SaveToDisk(false);
        QTextCursor unrepresentable(&document);
        unrepresentable.movePosition(QTextCursor::End);
        unrepresentable.insertText(QString::fromUtf8("<!--日本-->"));
        const QString unsaved_text = resource.GetText();
        bool rejected = false;
        try {
            resource.SaveToDisk(false);
        } catch (const std::exception &) {
            rejected = true;
        }
        Require(rejected, "Unrepresentable text was silently transcoded");
        Require(ReadBytes(path) == latin_bytes, "Failed encoding save overwrote the original file");
        Require(resource.GetText() == unsaved_text && document.isModified() && document.isUndoAvailable(),
                "Failed save lost the user's edit or modified/undo state");
        document.undo();
        resource.SaveToDisk(false);
        Require(ReadBytes(path) == latin_bytes, "Undo after rejected save did not restore original bytes");

        QString prefixed = source;
        prefixed.replace(QRegularExpression("<(/?)(package|metadata|meta|manifest|item|spine|itemref)(?=[\\s/>])"), "<\\1p:\\2");
        prefixed.replace("xmlns='http://www.idpf.org/2007/opf'", "xmlns:p='http://www.idpf.org/2007/opf'");
        prefixed.replace("dc:", "d:").replace("xmlns:dc=", "xmlns:d=");
        resource.SetText(prefixed);
        Require(resource.GetPrimaryBookTitle() == "Original", "Alternate OPF/DC prefixes broke title lookup");
        auto prefixed_metadata = resource.GetDCMetadata();
        for (auto &entry : prefixed_metadata) {
            if (entry.m_name == "dc:title") entry.m_content = "Prefixed";
        }
        resource.SetDCMetadata(prefixed_metadata);
        Require(resource.GetText() == QString(prefixed).replace("Original", "Prefixed"),
                "Editing prefixed OPF changed its original namespace syntax");

        settings.setPreserveOPFSource(false);
        resource.SetText(source);
        resource.SaveToDisk(false);
        Require(resource.GetText() != source, "Legacy formatting preference was ignored");

        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        const QString input = QString::fromLocal8Bit(argv[2]);
        ImportEPUB importer(input);
        const auto book = importer.GetBook();
        Require(!book->IsModified(), "Opening the valid EPUB marked it modified");
        const QByteArray imported_bytes = ReadBytes(book->GetOPF()->GetFullPath());
        Require(imported_bytes == ReadBytes(input + ".opf"), "Import rewrote original package bytes");
        ExportEPUB(input + ".unchanged.epub", book).WriteBook();
        auto chapter = qobject_cast<HTMLResource *>(book->GetFolderKeeper()->GetResourceByBookPath("OEBPS/a.xhtml"));
        Require(chapter != nullptr, "Fixture chapter is missing");
        chapter->SetText(QString(chapter->GetText()).replace("Original paragraph", "Changed paragraph"));
        book->SetModified(true);
        ExportEPUB(input + ".edited.epub", book).WriteBook();
        std::cout << "Native OPF resource regression passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
