/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "FontPreview/FontPreviewLanguage.h"

namespace {

enum class TagKind {
    Absent,
    Simplified,
    Traditional,
    Mixed,
    Japanese,
    English,
    Unknown
};

TagKind ClassifyTag(const QString &raw)
{
    const QString norm = raw.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    if (norm.isEmpty() || norm == QLatin1String("und") || norm == QLatin1String("mul")
        || norm == QLatin1String("zxx")) {
        return TagKind::Absent;
    }
    const QStringList parts = norm.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return TagKind::Absent;
    }
    const QString lang = parts.at(0);
    QStringList subtags = parts;
    subtags.removeFirst();
    if (lang == QLatin1String("zh") || lang == QLatin1String("chi") || lang == QLatin1String("zho")) {
        const bool hans = subtags.contains(QLatin1String("hans"))
            || subtags.contains(QLatin1String("cn"))
            || subtags.contains(QLatin1String("sg"));
        const bool hant = subtags.contains(QLatin1String("hant"))
            || subtags.contains(QLatin1String("tw"))
            || subtags.contains(QLatin1String("hk"))
            || subtags.contains(QLatin1String("mo"));
        if (hans && !hant) return TagKind::Simplified;
        if (hant && !hans) return TagKind::Traditional;
        return TagKind::Mixed;
    }
    if (lang == QLatin1String("ja") || lang == QLatin1String("jpn")) return TagKind::Japanese;
    if (lang == QLatin1String("en") || lang == QLatin1String("eng")) return TagKind::English;
    return TagKind::Unknown;
}

FontPreviewProfile ProfileFromFont(const QList<QFontDatabase::WritingSystem> &systems)
{
    const bool latin = systems.contains(QFontDatabase::Latin);
    const bool simplified = systems.contains(QFontDatabase::SimplifiedChinese);
    const bool traditional = systems.contains(QFontDatabase::TraditionalChinese);
    const bool japanese = systems.contains(QFontDatabase::Japanese);
    const int cjk = (simplified ? 1 : 0) + (traditional ? 1 : 0) + (japanese ? 1 : 0);
    if (cjk == 1) {
        if (japanese) return FontPreviewProfile::Japanese;
        if (simplified) return FontPreviewProfile::SimplifiedChinese;
        return FontPreviewProfile::TraditionalChinese;
    }
    if (cjk > 1) return FontPreviewProfile::Generic;
    if (latin) return FontPreviewProfile::English;
    return FontPreviewProfile::Generic;
}

FontPreviewProfile ProfileFromTag(TagKind kind)
{
    switch (kind) {
        case TagKind::Simplified: return FontPreviewProfile::SimplifiedChinese;
        case TagKind::Traditional: return FontPreviewProfile::TraditionalChinese;
        case TagKind::Mixed: return FontPreviewProfile::ChineseMixed;
        case TagKind::Japanese: return FontPreviewProfile::Japanese;
        case TagKind::English: return FontPreviewProfile::English;
        case TagKind::Absent:
        case TagKind::Unknown: return FontPreviewProfile::Generic;
    }
    return FontPreviewProfile::Generic;
}

}

FontPreviewProfile FontPreviewProfileForChoice(FontPreviewChoice choice)
{
    switch (choice) {
        case FontPreviewChoice::SimplifiedChinese: return FontPreviewProfile::SimplifiedChinese;
        case FontPreviewChoice::TraditionalChinese: return FontPreviewProfile::TraditionalChinese;
        case FontPreviewChoice::ChineseMixed: return FontPreviewProfile::ChineseMixed;
        case FontPreviewChoice::Japanese: return FontPreviewProfile::Japanese;
        case FontPreviewChoice::English: return FontPreviewProfile::English;
        case FontPreviewChoice::Auto: return FontPreviewProfile::Generic;
    }
    return FontPreviewProfile::Generic;
}

FontPreviewProfile ResolveFontPreviewProfile(
    const QStringList &bookLanguages,
    FontPreviewChoice choice,
    const QList<QFontDatabase::WritingSystem> &fontSystems)
{
    if (choice != FontPreviewChoice::Auto) {
        return FontPreviewProfileForChoice(choice);
    }
    const QString primary = bookLanguages.isEmpty() ? QString() : bookLanguages.first();
    const TagKind kind = ClassifyTag(primary);
    if (kind == TagKind::Absent || kind == TagKind::Unknown) {
        return ProfileFromFont(fontSystems);
    }
    return ProfileFromTag(kind);
}

QString FontPreviewProfileId(FontPreviewProfile profile)
{
    switch (profile) {
        case FontPreviewProfile::SimplifiedChinese: return QStringLiteral("zh-Hans");
        case FontPreviewProfile::TraditionalChinese: return QStringLiteral("zh-Hant");
        case FontPreviewProfile::ChineseMixed: return QStringLiteral("zh-mixed");
        case FontPreviewProfile::Japanese: return QStringLiteral("ja");
        case FontPreviewProfile::English: return QStringLiteral("en");
        case FontPreviewProfile::Generic: return QStringLiteral("generic");
    }
    return QStringLiteral("generic");
}
