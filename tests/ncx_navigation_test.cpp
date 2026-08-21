#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QString>

#include "BookManipulation/NcxNavigation.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

const char *kBookNcx =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE ncx PUBLIC \"-//NISO//DTD ncx 2005-1//EN\"\n"
    "   \"http://www.daisy.org/z3986/2005/ncx-2005-1.dtd\">\n"
    "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
    "  <head>\n"
    "    <meta name=\"dtb:uid\" content=\"easypub-e3fdba3a\"/>\n"
    "  </head>\n"
    "  <docTitle>\n"
    "    <text>国中理化课（160-174）作者：rescueme</text>\n"
    "  </docTitle>\n"
    "  <navMap>\n"
    "    <navPoint id=\"navPoint-1\" playOrder=\"1\">\n"
    "      <navLabel><text>第一百六十章</text></navLabel>\n"
    "      <content src=\"chapter1.html\"/>\n"
    "    </navPoint>\n"
    "    <navPoint id=\"navPoint-2\" playOrder=\"2\">\n"
    "      <navLabel><text>第一百六十一章</text></navLabel>\n"
    "      <content src=\"chapter2.html\"/>\n"
    "    </navPoint>\n"
    "    <navPoint id=\"navPoint-17\" playOrder=\"17\">\n"
    "      <navLabel><text>第一百七十四章IF线下集</text></navLabel>\n"
    "      <content src=\"chapter18.html\"/>\n"
    "    </navPoint>\n"
    "  </navMap>\n"
    "</ncx>\n";

const char *kNestedNcx =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
    "  <docTitle><text>Nested</text></docTitle>\n"
    "  <navMap>\n"
    "    <navPoint id=\"p1\">\n"
    "      <navLabel><text>Part 1</text></navLabel>\n"
    "      <content src=\"part1.html\"/>\n"
    "      <navPoint id=\"c1\">\n"
    "        <navLabel><text>Chapter 1</text></navLabel>\n"
    "        <content src=\"Text/ch1.html#title\"/>\n"
    "      </navPoint>\n"
    "    </navPoint>\n"
    "  </navMap>\n"
    "  <pageList>\n"
    "    <pageTarget type=\"normal\" value=\"3\">\n"
    "      <navLabel><text>3</text></navLabel>\n"
    "      <content src=\"part1.html#p3\"/>\n"
    "    </pageTarget>\n"
    "  </pageList>\n"
    "</ncx>\n";

}

int main(int argc, char *argv[])
{
    if (argc == 2) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        Require(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not read NCX");
        const NcxNavigation parsed = NcxNavigation::parse(QString::fromUtf8(file.readAll()));
        Require(!parsed.toc.isEmpty(), "NCX file produced an empty TOC");
        std::cout << parsed.doctitle.toStdString() << '\n';
        std::cout << parsed.toc.size() << " entries\n";
        for (const NcxNavPoint &pt : parsed.toc) {
            std::cout << pt.level << ' ' << pt.label.toStdString()
                      << " -> " << pt.src.toStdString() << '\n';
        }
        return 0;
    }

    const NcxNavigation book = NcxNavigation::parse(QString::fromUtf8(kBookNcx));
    Require(book.doctitle == QString::fromUtf8("国中理化课（160-174）作者：rescueme"),
            "docTitle must survive NCX DTD and namespaced tags");
    Require(book.toc.size() == 3, "flat NCX navPoints must all be collected");
    Require(book.toc.at(0).level == 1 && book.toc.at(0).label == QString::fromUtf8("第一百六十章"),
            "first navPoint label must parse");
    Require(book.toc.at(0).src == QLatin1String("chapter1.html"),
            "first navPoint src must parse");
    Require(book.toc.at(2).label == QString::fromUtf8("第一百七十四章IF线下集"),
            "last navPoint label must parse");
    Require(book.toc.at(2).src == QLatin1String("chapter18.html"),
            "last navPoint src must parse");

    const NcxNavigation nested = NcxNavigation::parse(QString::fromUtf8(kNestedNcx));
    Require(nested.doctitle == QLatin1String("Nested"), "nested NCX docTitle failed");
    Require(nested.toc.size() == 2, "parent and child navPoints must both be emitted");
    Require(nested.toc.at(0).level == 1 && nested.toc.at(0).label == QLatin1String("Part 1"),
            "parent navPoint must come first");
    Require(nested.toc.at(1).level == 2 && nested.toc.at(1).label == QLatin1String("Chapter 1"),
            "child navPoint must keep its depth");
    Require(nested.toc.at(1).src == QLatin1String("Text/ch1.html#title"),
            "child src plus fragment must be preserved");
    Require(nested.pages.size() == 1 && nested.pages.at(0).value == QLatin1String("3"),
            "pageList value must parse");
    Require(nested.pages.at(0).src == QLatin1String("part1.html#p3"),
            "pageList src must parse");

    Require(NcxNavigation::srcToBookPath(QStringLiteral("chapter1.html"), QStringLiteral("OEBPS"))
                == QLatin1String("OEBPS/chapter1.html"),
            "NCX-relative src must resolve against the NCX folder");
    Require(NcxNavigation::srcToBookPath(QStringLiteral("Text/chapter1.html"), QStringLiteral("OEBPS"))
                == QLatin1String("OEBPS/Text/chapter1.html"),
            "post-standardize NCX src must resolve under OEBPS/Text");
    Require(NcxNavigation::srcToBookPath(QStringLiteral("chapter1.html#title"), QStringLiteral("OEBPS"))
                == QLatin1String("OEBPS/chapter1.html#title"),
            "src fragments must stay on the book path");
    Require(NcxNavigation::srcToBookPath(QStringLiteral("Text/chapter1.html"), QStringLiteral("."))
                == QLatin1String("Text/chapter1.html"),
            "root NCX folder must not keep a dot prefix");

    Require(NcxNavigation::bookPathToNavHref(QStringLiteral("OEBPS/Text/chapter1.html"),
                                             QStringLiteral("OEBPS/Text/nav.xhtml"))
                == QLatin1String("chapter1.html"),
            "nav in Text/ must link to sibling chapters without a prefix");
    Require(NcxNavigation::bookPathToNavHref(QStringLiteral("OEBPS/chapter1.html"),
                                             QStringLiteral("OEBPS/Text/nav.xhtml"))
                == QLatin1String("../chapter1.html"),
            "nav in Text/ must walk up to a chapter left in OEBPS/");
    Require(NcxNavigation::bookPathToNavHref(QStringLiteral("OEBPS/Text/chapter1.html#title"),
                                             QStringLiteral("OEBPS/Text/nav.xhtml"))
                == QLatin1String("chapter1.html#title"),
            "nav hrefs must keep fragments");

    return 0;
}
