#include "Misc/UpdateVersionFeed.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTimer>

namespace UpdateVersionFeed {

namespace {

bool IsPythonWhitespace(ushort codepoint)
{
    return (codepoint >= 0x09 && codepoint <= 0x0d)
        || (codepoint >= 0x1c && codepoint <= 0x20)
        || codepoint == 0x85 || codepoint == 0xa0 || codepoint == 0x1680
        || (codepoint >= 0x2000 && codepoint <= 0x200a)
        || codepoint == 0x2028 || codepoint == 0x2029
        || codepoint == 0x202f || codepoint == 0x205f || codepoint == 0x3000;
}

QString PythonStrip(const QString& value)
{
    qsizetype first = 0;
    qsizetype last = value.size();
    while (first < last && IsPythonWhitespace(value.at(first).unicode())) {
        ++first;
    }
    while (last > first && IsPythonWhitespace(value.at(last - 1).unicode())) {
        --last;
    }
    return value.mid(first, last - first);
}

}

QString ParseVersion(const QByteArray& xml)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString content = utf8(xml);
    if (utf8.hasError()) {
        return QString();
    }

    // Keep the legacy feed's first-match extraction rather than requiring a
    // fully valid XML document.
    static const QRegularExpression version_pattern(
        QStringLiteral("<current-version>([^<]*)</current-version>"));
    const QRegularExpressionMatch match = version_pattern.match(content);
    return match.hasMatch() ? PythonStrip(match.captured(1)) : QString();
}

QString FetchVersion(const QUrl& url, int timeout_ms)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(timeout_ms);
    if (!reply->isFinished()) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }

    const QString version = reply->error() == QNetworkReply::NoError
        ? ParseVersion(reply->readAll()) : QString();
    reply->deleteLater();
    return version;
}

}
