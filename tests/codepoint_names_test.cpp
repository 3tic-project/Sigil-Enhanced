#include "Misc/CodepointNames.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool Check(const char* label, int cp, const QString& expected)
{
    const QString actual = CodepointNames::instance().GetName(cp);
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "%s U+%X: expected '%s', got '%s'\n", label, cp,
                 expected.toUtf8().constData(), actual.toUtf8().constData());
    return false;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_INIT_RESOURCE(CodepointNamesData);

    if (argc == 2 && std::strcmp(argv[1], "--batch-probe") == 0) {
        char line[64];
        while (std::fgets(line, sizeof(line), stdin)) {
            const int cp = std::strtol(line, nullptr, 10);
            std::puts(CodepointNames::instance().GetName(cp).toUtf8().constData());
        }
        return 0;
    }

    bool ok = true;
    ok &= Check("EOF", -1, QStringLiteral("EOF"));
    ok &= Check("negative", -2, QString());
    ok &= Check("NULL", 0, QStringLiteral("NULL"));
    ok &= Check("control 31", 31, QStringLiteral("UNIT SEPARATOR (US)"));
    ok &= Check("ASCII", 'A', QStringLiteral("LATIN CAPITAL LETTER A"));
    ok &= Check("CJK", 0x4e00, QStringLiteral("CJK UNIFIED IDEOGRAPH-4E00"));
    ok &= Check("Hangul", 0xac00, QStringLiteral("HANGUL SYLLABLE GA"));
    ok &= Check("emoji", 0x1f600, QStringLiteral("GRINNING FACE"));
    ok &= Check("Unicode 15 addition", 0x1fae8, QStringLiteral("Unknown"));
    ok &= Check("surrogate", 0xd800, QStringLiteral("Unknown"));
    ok &= Check("last scalar", 0x10ffff, QStringLiteral("Unknown"));
    ok &= Check("beyond Unicode", 0x110000, QString());
    return ok ? 0 : 1;
}
