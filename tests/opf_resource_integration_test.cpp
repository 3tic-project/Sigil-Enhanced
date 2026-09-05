#include "EmbedPython/EmbeddedPython.h"

#include <QApplication>
#include <QFile>
#include <QDateTime>
#include <QTemporaryDir>
#include <QTextCursor>
#include <iostream>
#include <stdexcept>

#include "Misc/SettingsStore.h"
#include "BookManipulation/CleanSource.h"
#include "ResourceObjects/OPFResource.h"
#include "Widgets/TextDocument.h"

static void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        Require(argc == 2, "Expected the source root");
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

        settings.setPreserveOPFSource(false);
        resource.SetText(source);
        resource.SaveToDisk(false);
        Require(resource.GetText() != source, "Legacy formatting preference was ignored");
        std::cout << "Native OPF resource regression passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
