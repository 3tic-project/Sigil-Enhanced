#pragma once

#include <QFileDialog>
#include <QString>

namespace FileDialogPolicy
{

enum class Style {
    Native,
    QtNonNative
};

inline Style Parse(const QString &value)
{
    return value == QLatin1String("qt") ? Style::QtNonNative : Style::Native;
}

inline QString StorageValue(Style style)
{
    return style == Style::QtNonNative ? QStringLiteral("qt") : QStringLiteral("native");
}

// forceNative wins over both the saved style and Qt::AA_DontUseNativeDialogs.
// The attribute is honored otherwise so tests can drive a real QFileDialog.
inline QFileDialog::Options Options(Style style, bool forceNative, bool noCustomDirectoryIcons,
                                    bool attributeForcesQt)
{
    QFileDialog::Options options;
    if (noCustomDirectoryIcons) {
        options |= QFileDialog::DontUseCustomDirectoryIcons;
    }
    if (forceNative) {
        return options;
    }
    if (style == Style::QtNonNative || attributeForcesQt) {
        options |= QFileDialog::DontUseNativeDialog;
    }
    return options;
}

}
