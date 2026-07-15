#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace FontSubset
{

struct UsageSource {
    QString path;
    QString mediaType;
    QString content;
};

struct GlobalFontUsage {
    QSet<quint32> codepoints;
    QStringList shapingSamples;
    QStringList warnings;
};

class GlobalFontUsageCollector
{
public:
    GlobalFontUsage Collect(const QList<UsageSource>& sources) const;

private:
    void CollectXml(const UsageSource& source, GlobalFontUsage& usage) const;
    void CollectCss(const QString& css, const QString& path,
                    GlobalFontUsage& usage) const;
    void AddText(const QString& text, GlobalFontUsage& usage,
                 bool addShapingSample = true) const;
};

}
