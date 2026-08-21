#include <cstdlib>
#include <iostream>

#include <QString>

#include "BookManipulation/HtmlNamedEntity.h"

namespace {

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main()
{
    // QChar(0x1d504) aborts in Qt 6 debug builds; EPUB named-entity
    // conversion must keep supplementary-plane codepoints as scalars.
    const QString fraktur_a = HtmlNamedEntityFromCodepoint(0x1d504);
    const auto cps = fraktur_a.toUcs4();
    Require(cps.size() == 1, "supplementary codepoint must be one scalar");
    Require(cps.at(0) == 0x1d504u, "Mathematical Fraktur A must round-trip");
    Require(HtmlNamedEntityToNumericReferences(fraktur_a) == QLatin1String("&#x1d504;"),
            "supplementary entity must become a single numeric reference");

    Require(HtmlNamedEntityFromCodepoint(0xc6).toUcs4().at(0) == 0xc6u,
            "BMP codepoint must still convert");
    Require(HtmlNamedEntityToNumericReferences(HtmlNamedEntityFromCodepoint(0xc6))
                == QLatin1String("&#xc6;"),
            "BMP entity must become a numeric reference");
    Require(HtmlNamedEntityToNumericReferences(QStringLiteral("&")) == QLatin1String("&#x26;"),
            "AMP replacement must stay a single numeric reference");
    return 0;
}
