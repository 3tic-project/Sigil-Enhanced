/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "Agent/Execution/ResourceMutations.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace SigilAgent
{

QString defaultXhtmlTemplate()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!DOCTYPE html>\n\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n"
        "<head>\n"
        "  <title></title>\n"
        "</head>\n\n"
        "<body>\n"
        "  <p>&#160;</p>\n"
        "</body>\n"
        "</html>");
}

QString mediaTypeForKind(const QString &kind)
{
    if (kind == QLatin1String("css")) return QStringLiteral("text/css");
    return QStringLiteral("application/xhtml+xml");
}

QString kindFromPathOrType(const QString &book_path, const QString &kind)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered == QLatin1String("css") || lowered == QLatin1String("xhtml")
        || lowered == QLatin1String("html")) {
        return lowered == QLatin1String("html") ? QStringLiteral("xhtml") : lowered;
    }
    const QString suffix = QFileInfo(book_path).suffix().toLower();
    if (suffix == QLatin1String("css")) return QStringLiteral("css");
    return QStringLiteral("xhtml");
}

bool bookPathTaken(const QString &book_path, const QStringList &existing_paths)
{
    const QString normalized = book_path.trimmed();
    for (const QString &existing : existing_paths) {
        if (existing.compare(normalized, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString suggestCopyBookPath(const QString &source_path, const QStringList &existing_paths)
{
    const QFileInfo info(source_path);
    const QString dir = info.path();
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString ext = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;

    QRegularExpression numbered(QStringLiteral("^(.*?)(\\d+)$"));
    const QRegularExpressionMatch match = numbered.match(stem);
    QString prefix = stem;
    int number = 1;
    int width = 1;
    if (match.hasMatch()) {
        prefix = match.captured(1);
        const QString digits = match.captured(2);
        number = digits.toInt();
        width = digits.size();
    } else {
        prefix = stem + QStringLiteral("-copy");
        number = 0;
        width = 1;
    }

    for (int i = 1; i < 1000; ++i) {
        const int next = number + i;
        const QString name = match.hasMatch()
            ? prefix + QStringLiteral("%1").arg(next, width, 10, QLatin1Char('0'))
            : (i == 1 ? prefix : prefix + QString::number(i));
        const QString candidate = (dir.isEmpty() || dir == QLatin1String("."))
            ? QString(name + ext)
            : QString(dir + QLatin1Char('/') + name + ext);
        if (!bookPathTaken(candidate, existing_paths)) return candidate;
    }
    return source_path + QStringLiteral("-copy");
}

} // namespace SigilAgent
