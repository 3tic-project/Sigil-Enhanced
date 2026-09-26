#include "EmbedPython/EmbeddedPython.h"

#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "BookManipulation/CleanSource.h"
#include "BookManipulation/XmlProcessor.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/NCXResource.h"
#include "SourceUpdates/AnchorUpdates.h"
#include "SourceUpdates/PerformNCXUpdates.h"
#include "SourceUpdates/PerformOPFUpdates.h"
#include "SourceUpdates/PerformXMLUpdates.h"

namespace {

int failures = 0;
int nativeCalls = 0;
int deferredCalls = 0;

void Check(bool condition, const QString &message)
{
    if (!condition) {
        std::cerr << message.toStdString() << std::endl;
        ++failures;
    }
}

QString Python(const QString &function, const QList<QVariant> &args)
{
    int rv = 0;
    QString traceback;
    const QVariant result = EmbeddedPython::instance().runInPython("xmlprocessor", function, args, &rv, traceback);
    Check(rv == 0, function + " raised: " + traceback);
    return result.toString();
}

QList<QVariant> UpdateArgs(const QString &source, const QString &newBookPath, const QString &oldBookPath,
                           const QHash<QString, QString> &updates)
{
    QStringList values;
    for (const QString &key : updates.keys()) values.append(updates.value(key));
    return {source, newBookPath, oldBookPath, QStringList(updates.keys()), values};
}

// Records whether the caller could stay native, and requires that only malformed input defers.
void Track(bool native, bool wellFormed, const QString &label)
{
    native ? ++nativeCalls : ++deferredCalls;
    Check(native == wellFormed, label + (native ? " stayed native on malformed input" : " deferred well-formed input"));
}

const QString NCX = QString::fromUtf8(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE ncx PUBLIC "-//NISO//DTD ncx 2005-1//EN" "http://www.daisy.org/z3986/2005/ncx-2005-1.dtd">
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:1"/><!-- note &amp; --></head>
  <docTitle><text>A &amp; B &#233; 章</text></docTitle>
  <navMap>
    <navPoint id="n1" playOrder="1"><navLabel><text>One</text></navLabel><content src="Text/Section%200001.xhtml"/>
      <navPoint id="n2" playOrder="2"><navLabel><text>Two</text></navLabel><content src="Text/Section%200001.xhtml#second"/></navPoint>
    </navPoint>
    <navPoint id="n3" playOrder="3"><navLabel><text>Three</text></navLabel><content src="Text/%E7%AB%A0.xhtml#third"/></navPoint>
  </navMap>
</ncx>
)");

const QString OPF = QString::fromUtf8(R"(<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:identifier id="BookId" opf:scheme="UUID">urn:uuid:1</dc:identifier><dc:title>T &amp; U</dc:title>
    <meta name="cover" content="cover"/>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="s1" href="Text/Section%200001.xhtml" media-type="application/xhtml+xml"/>
    <item id="s2" href="Text/%E7%AB%A0.xhtml" media-type="application/xhtml+xml"/>
    <item id="cover" href="Images/c 1.jpg" media-type="image/jpeg"/>
  </manifest>
  <spine toc="ncx"><itemref idref="s1"/><itemref idref="s2"/></spine>
  <guide><reference type="text" title="Start" href="Text/Section%200001.xhtml#second"/></guide>
</package>
)");

const QString SMIL = QString::fromUtf8(R"(<?xml version="1.0" encoding="UTF-8"?>
<smil xmlns="http://www.w3.org/ns/SMIL" xmlns:epub="http://www.idpf.org/2007/ops" version="3.0">
  <body epub:textref="../Text/Section%200001.xhtml">
    <par id="p1"><text src="../Text/Section%200001.xhtml#w1"/><audio src="../Audio/a.mp3" clipBegin="0s"/></par>
  </body>
</smil>
)");

const QString PAGE_MAP = QString::fromUtf8(R"(<?xml version="1.0"?>
<page-map xmlns="http://www.idpf.org/2007/opf"><page name="1" href="Text/%E7%AB%A0.xhtml#p1"/></page-map>
)");

QString Broken(const QString &source)
{
    return QString(source).replace("</navMap>", "").replace("</manifest>", "").replace("</body>", "").replace("/></page-map>", "></page-map>");
}

}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 3) return EXIT_FAILURE;
    const QString root = QString::fromLocal8Bit(argv[1]);
    auto &python = EmbeddedPython::instance();
    python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
    python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
    python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");

    const QHash<QString, QString> updates {
        {"OEBPS/Text/Section 0001.xhtml", "OEBPS/Text/Part/Section 1.xhtml"},
        {QString::fromUtf8("OEBPS/Text/章.xhtml"), QString::fromUtf8("OEBPS/Misc/章 2.xhtml")},
        {"OEBPS/Images/c 1.jpg", "OEBPS/Images/cover.jpg"},
        {"OEBPS/Audio/a.mp3", "OEBPS/Audio/b c.mp3"},
    };
    const QString unused;
    QString out;

    for (const QString &mtype : {QString("application/x-dtbncx+xml"), QString("application/oebps-package+xml"),
                                 QString("application/smil+xml"), QString("application/oebps-page-map+xml")}) {
        for (const QString &base : {NCX, OPF, SMIL, PAGE_MAP}) {
            // opf_newparser raises on documents without a package element; that path stays in Python.
            if (mtype == "application/oebps-package+xml" && base != OPF) continue;
            for (const bool wellFormed : {true, false}) {
                const QString source = wellFormed ? base : Broken(base);
                const QString label = mtype + (wellFormed ? " well-formed " : " malformed ") + source.left(60);
                const QString expected = Python("repairXML", {source, mtype});
                Check(CleanSource::XMLPrettyPrintBS4(source, mtype) == expected, "XMLPrettyPrintBS4 differs for " + label);
                Track(XmlProcessor::RepairXML(source, mtype, &out), wellFormed, "RepairXML " + label);
            }
        }
    }

    for (const QString &base : {NCX, OPF, SMIL, PAGE_MAP}) {
        for (const bool wellFormed : {true, false}) {
            const QString source = wellFormed ? base : Broken(base);
            const QString label = (wellFormed ? "well-formed " : "malformed ") + source.left(60);
            for (const auto &paths : {qMakePair(QString("OEBPS/toc.ncx"), QString("OEBPS/toc.ncx")),
                                      qMakePair(QString("OEBPS/Misc/content.opf"), QString("OEBPS/content.opf")),
                                      qMakePair(QString("OEBPS/Audio/a.smil"), QString("OEBPS/Text/a.smil"))}) {
                const QList<QVariant> args = UpdateArgs(source, paths.first, paths.second, updates);
                Check(PerformNCXUpdates(source, paths.first, updates, paths.second)() == Python("performNCXSourceUpdates", args),
                      "PerformNCXUpdates differs for " + label);
                Track(XmlProcessor::PerformNCXSourceUpdates(source, paths.first, paths.second, updates, &out), wellFormed, "NCX " + label);
                Check(PerformOPFUpdates(source, paths.first, updates, paths.second)() == Python("performOPFSourceUpdates", args),
                      "PerformOPFUpdates differs for " + label);
                Track(XmlProcessor::PerformOPFSourceUpdates(source, paths.first, paths.second, updates, &out), wellFormed, "OPF " + label);
                Check(PerformXMLUpdates(source, paths.first, updates, paths.second, "application/smil+xml")() == Python("performSMILUpdates", args),
                      "PerformXMLUpdates SMIL differs for " + label);
                Check(PerformXMLUpdates(source, paths.first, updates, paths.second, "application/vnd.adobe-page-map+xml")() ==
                      Python("performPageMapUpdates", args), "PerformXMLUpdates page-map differs for " + label);
                Track(XmlProcessor::PerformSMILUpdates(source, paths.first, paths.second, updates, &out), wellFormed, "SMIL " + label);
            }
        }
    }

    QTemporaryDir folder;
    const QString main = folder.path();
    QDir(main).mkpath("OEBPS/Text");
    for (const QString &path : {QString("OEBPS/toc.ncx"), QString("OEBPS/Text/Section 0001.xhtml"), QString("OEBPS/Text/Section 0001_1.xhtml")}) {
        QFile file(main + "/" + path);
        Check(file.open(QIODevice::WriteOnly), "cannot create " + path);
        file.write(path.endsWith(".ncx") ? NCX.toUtf8() : QByteArray("<html/>"));
    }
    const QString html = R"(<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>x</title></head><body><p id="%1">a</p><p id="%2">b</p></body></html>)";
    HTMLResource first(main, main + "/OEBPS/Text/Section 0001.xhtml", nullptr);
    HTMLResource second(main, main + "/OEBPS/Text/Section 0001_1.xhtml", nullptr);
    first.SetText(html.arg("first", "w1"));
    second.SetText(html.arg("second", "third"));
    const QHash<QString, QString> idLocations {
        {"first", first.GetRelativePath()}, {"w1", first.GetRelativePath()},
        {"second", second.GetRelativePath()}, {"third", second.GetRelativePath()},
    };
    for (const bool wellFormed : {true, false}) {
        const QString source = wellFormed ? NCX : Broken(NCX);
        NCXResource ncx(main, main + "/OEBPS/toc.ncx", "2.0");
        ncx.SetText(source);
        QStringList values;
        for (const QString &key : idLocations.keys()) values.append(idLocations.value(key));
        const QString expected = Python("anchorNCXUpdates", {source, QString("OEBPS/toc.ncx"), first.GetRelativePath(),
                                                             QStringList(idLocations.keys()), values});
        AnchorUpdates::UpdateTOCEntries(&ncx, first.GetRelativePath(), {&first, &second});
        Check(ncx.GetText() == expected, QString("UpdateTOCEntries differs (%1)").arg(wellFormed));
        Track(XmlProcessor::AnchorNCXUpdates(source, "OEBPS/toc.ncx", first.GetRelativePath(), idLocations, &out),
              wellFormed, "AnchorNCXUpdates");

        const QStringList merged {QString::fromUtf8("OEBPS/Text/章.xhtml")};
        ncx.SetText(source);
        const QString mergedExpected = Python("anchorNCXUpdatesAfterMerge", {source, QString("OEBPS/toc.ncx"),
                                                                             first.GetRelativePath(), merged});
        AnchorUpdates::UpdateTOCEntriesAfterMerge(&ncx, first.GetRelativePath(), merged);
        Check(ncx.GetText() == mergedExpected, QString("UpdateTOCEntriesAfterMerge differs (%1)").arg(wellFormed));
        Track(XmlProcessor::AnchorNCXUpdatesAfterMerge(source, "OEBPS/toc.ncx", first.GetRelativePath(), merged, &out),
              wellFormed, "AnchorNCXUpdatesAfterMerge");
    }

    std::cout << "xml_processor_integration: " << nativeCalls << " native, " << deferredCalls
              << " deferred, " << failures << " failures" << std::endl;
    return failures == 0 && nativeCalls > 0 && deferredCalls > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
