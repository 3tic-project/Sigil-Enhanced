#include "BookManipulation/XmlWellFormed.h"

#include <QMutex>
#include <QRegularExpression>

#include <libxml/parser.h>

namespace {

struct FirstError {
    bool seen = false;
    int line = 0;
    int column = 0;
    QString message;
};

void collectError(void *user, xmlError *error)
{
    if (!user || !error || error->level < XML_ERR_ERROR) return;
    auto *first = static_cast<FirstError *>(user);
    if (first->seen) return;
    first->seen = true;
    first->line = error->line;
    first->column = error->int2;
    first->message = QString::fromUtf8(error->message ? error->message : "exception").trimmed();
}

QString withoutDeclaration(QString source)
{
    static const QRegularExpression declaration(
        QStringLiteral(R"(<\s*\?xml\s*[^?>]*\?>\s*)"),
        QRegularExpression::CaseInsensitiveOption);
    return source.replace(declaration, QString());
}

}

namespace XmlWellFormed {

Result Check(const QString &source)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    xmlInitParser();
    const QByteArray bytes = withoutDeclaration(source).toUtf8();
    FirstError first;
    xmlSetStructuredErrorFunc(&first, collectError);
    xmlParserCtxtPtr context = xmlNewParserCtxt();
    Result result;
    if (!context) {
        xmlSetStructuredErrorFunc(nullptr, nullptr);
        return result;
    }
    xmlCtxtReadMemory(context, bytes.constData(), int(bytes.size()), nullptr, "utf-8",
                      XML_PARSE_NONET | XML_PARSE_NOENT);
    const bool wellFormed = context->wellFormed == 1 && !xmlCtxtGetLastError(context);
    xmlFreeParserCtxt(context);
    xmlSetStructuredErrorFunc(nullptr, nullptr);
    if (wellFormed && !first.seen) return result;
    if (!first.seen) {
        result.line = 0;
        result.column = 0;
        result.message = QStringLiteral("exception");
        return result;
    }
    result.line = first.line;
    result.column = first.column;
    result.message = first.message;
    return result;
}

}
