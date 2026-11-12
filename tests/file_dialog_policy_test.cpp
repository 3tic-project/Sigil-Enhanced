#include <iostream>
#include <stdexcept>

#include <QFileDialog>

#include "Misc/FileDialogPolicy.h"

namespace
{

void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool nonNative(QFileDialog::Options options)
{
    return options.testFlag(QFileDialog::DontUseNativeDialog);
}

}

int main()
{
    using FileDialogPolicy::Options;
    using FileDialogPolicy::Parse;
    using FileDialogPolicy::StorageValue;
    using FileDialogPolicy::Style;

    try {
        require(!nonNative(Options(Style::Native, false, false, false)),
                "The default style must stay on the system dialog");
        require(nonNative(Options(Style::QtNonNative, false, false, false)),
                "The Qt style must select the non-native dialog");
        require(!nonNative(Options(Style::QtNonNative, true, false, true)),
                "SIGIL_FORCE_NATIVE_FILE_DIALOG must beat the saved style and the test attribute");
        require(nonNative(Options(Style::Native, false, false, true)),
                "Qt::AA_DontUseNativeDialogs must keep test dialogs drivable");
        const QFileDialog::Options icons = Options(Style::Native, false, true, false);
        require(icons.testFlag(QFileDialog::DontUseCustomDirectoryIcons) && !nonNative(icons),
                "The icon override must not change the dialog style");
        const QFileDialog::Options directory = Options(Style::QtNonNative, false, false, false)
            | QFileDialog::ShowDirsOnly;
        require(nonNative(directory) && directory.testFlag(QFileDialog::ShowDirsOnly),
                "Caller-specific options must survive the shared style");
        require(Parse(QStringLiteral("qt")) == Style::QtNonNative, "qt must parse as the previous dialog");
        require(Parse(QStringLiteral("native")) == Style::Native, "native must parse as the system dialog");
        require(Parse(QString()) == Style::Native && Parse(QStringLiteral("other")) == Style::Native,
                "Missing or unknown values must stay on the system dialog");
        require(StorageValue(Style::Native) == QLatin1String("native")
                && StorageValue(Style::QtNonNative) == QLatin1String("qt"),
                "Stored values must round-trip through the parser");
        std::cout << "File dialog policy checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
