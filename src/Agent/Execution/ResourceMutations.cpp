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

QString defaultSvgTemplate()
{
    return QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"100\" height=\"100\"></svg>\n");
}

QString mediaTypeForKind(const QString &kind)
{
    if (kind == QLatin1String("css")) return QStringLiteral("text/css");
    if (kind == QLatin1String("svg")) return QStringLiteral("image/svg+xml");
    if (kind == QLatin1String("js") || kind == QLatin1String("javascript")) {
        return QStringLiteral("application/javascript");
    }
    if (kind == QLatin1String("text")) return QStringLiteral("text/plain");
    return QStringLiteral("application/xhtml+xml");
}

QString kindFromPathOrType(const QString &book_path, const QString &kind)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered == QLatin1String("html")) return QStringLiteral("xhtml");
    if (lowered == QLatin1String("javascript")) return QStringLiteral("js");
    if (lowered == QLatin1String("css") || lowered == QLatin1String("xhtml")
        || lowered == QLatin1String("svg") || lowered == QLatin1String("js")
        || lowered == QLatin1String("text")) {
        return lowered;
    }
    const QString suffix = QFileInfo(book_path).suffix().toLower();
    if (suffix == QLatin1String("css")) return QStringLiteral("css");
    if (suffix == QLatin1String("svg")) return QStringLiteral("svg");
    if (suffix == QLatin1String("js")) return QStringLiteral("js");
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("json")) return QStringLiteral("text");
    return QStringLiteral("xhtml");
}

bool kindIsCreatable(const QString &kind)
{
    return kind == QLatin1String("xhtml") || kind == QLatin1String("css")
        || kind == QLatin1String("svg") || kind == QLatin1String("js")
        || kind == QLatin1String("text");
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

QString resolveRenameTarget(const QString &current_path, const QString &requested)
{
    QString target = requested.trimmed();
    if (target.isEmpty()) return target;
    const bool has_dir = target.contains(QLatin1Char('/')) || target.contains(QLatin1Char('\\'));
    if (has_dir) return target;
    if (!target.contains(QLatin1Char('.'))) {
        const QString suffix = QFileInfo(current_path).suffix();
        if (!suffix.isEmpty()) target += QLatin1Char('.') + suffix;
    }
    const QString dir = QFileInfo(current_path).path();
    if (!dir.isEmpty() && dir != QLatin1String(".")) {
        target = dir + QLatin1Char('/') + QFileInfo(target).fileName();
    }
    return target;
}

} // namespace SigilAgent
