#include "Misc/TxtEncoding.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <cstring>

namespace {

bool Check(const char* label, const QByteArray& input, const QString& expected)
{
    const QString actual = TxtEncoding::Decode(input);
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "%s: expected %s, got %s\n", label,
                 expected.toUtf8().toHex().constData(),
                 actual.toUtf8().toHex().constData());
    return false;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc == 2 && std::strcmp(argv[1], "--batch-probe") == 0) {
        char line[128];
        while (std::fgets(line, sizeof(line), stdin)) {
            const QByteArray decoded = TxtEncoding::Decode(QByteArray::fromHex(line)).toUtf8().toHex();
            std::puts(decoded.constData());
        }
        return 0;
    }

    // Expected values were recorded from importtxt.read_unicode with Python's
    // strict UTF-8, GB18030, UTF-16 fallback order.
    bool ok = true;
    ok &= Check("UTF-8", QByteArray::fromHex("e4b8ade69687"), QString::fromUtf8("中文"));
    ok &= Check("UTF-8 BOM retained", QByteArray::fromHex("efbbbf48656c6c6f0d0a"),
                QString(QChar(0xfeff)) + QStringLiteral("Hello\r\n"));
    ok &= Check("GB18030", QByteArray::fromHex("d6d0cec4"), QString::fromUtf8("中文"));
    ok &= Check("GB18030 four-byte", QByteArray::fromHex("9439fc36"), QString::fromUtf8("😀"));
    ok &= Check("GB18030 first supplementary", QByteArray::fromHex("90308130"), QString::fromUtf8("𐀀"));
    ok &= Check("GB18030 last scalar", QByteArray::fromHex("e3329a35"), QString::fromUtf8("\xf4\x8f\xbf\xbf"));
    ok &= Check("GB18030 outside Unicode", QByteArray::fromHex("e3329a36"), QString::fromUtf8("㋣㚚"));
    ok &= Check("UTF-16 LE", QByteArray::fromHex("fffe48006900"), QStringLiteral("Hi"));
    ok &= Check("UTF-16 BE", QByteArray::fromHex("feff00480069"), QStringLiteral("Hi"));
    ok &= Check("UTF-8 wins for NUL bytes", QByteArray::fromHex("48006900"),
                QString(QChar('H')) + QChar(0) + QChar('i') + QChar(0));
    ok &= Check("BOM-less UTF-16 fallback", QByteArray::fromHex("8130"), QString::fromUtf8("め"));
    ok &= Check("BOM-less UTF-16 fallback 2", QByteArray::fromHex("81ff"), QString::fromUtf8("ﾁ"));
    ok &= Check("truncated UTF-8 then UTF-16", QByteArray::fromHex("00c2"), QString::fromUtf8("숀"));
    ok &= Check("truncated GB18030", QByteArray::fromHex("81"), QString());
    ok &= Check("invalid", QByteArray::fromHex("ff"), QString());
    ok &= Check("truncated UTF-16", QByteArray::fromHex("fffe48"), QString());
    ok &= Check("empty", QByteArray(), QString());
    return ok ? 0 : 1;
}
