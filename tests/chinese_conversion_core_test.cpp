#include <cstdlib>
#include <iostream>

#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>

#include "ChineseConversion/ChineseConversionData.h"
#include "ChineseConversion/ChineseConversionProfile.h"
#include "ChineseConversion/ChineseConversionSettings.h"
#include "ChineseConversion/OpenCCConverter.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

QString Convert(ChineseConversionMode mode, const QString& input)
{
    const ChineseConversionProfile profile = ChineseConversionProfile::ForMode(mode);
    OpenCCConverter converter(profile, QStringLiteral(SIGIL_OPENCC_TEST_DATA_DIR));
    if (!converter.IsValid()) {
        std::cerr << converter.ErrorString().toStdString() << std::endl;
        std::exit(EXIT_FAILURE);
    }
    QString error;
    const QString result = converter.Convert(input, &error);
    if (!error.isEmpty()) {
        std::cerr << error.toStdString() << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return result;
}

}

int main()
{
    const QList<ChineseConversionProfile> profiles = ChineseConversionProfile::All();
    Require(profiles.size() == 12, "expected twelve stable OpenCC profiles");
    Require(ChineseConversionData::FindDataDirectory()
                == QStringLiteral(SIGIL_OPENCC_TEST_DATA_DIR),
            "OpenCC build data directory was not discovered");

    QSet<QString> keys;
    QSet<QString> configs;
    for (const auto& profile : profiles) {
        Require(!profile.Key().isEmpty(), "profile key is empty");
        Require(!profile.SourceLocale().isEmpty(), "source locale is empty");
        Require(!profile.TargetLocale().isEmpty(), "target locale is empty");
        Require(!keys.contains(profile.Key()), "profile key is duplicated");
        Require(!configs.contains(profile.ConfigFile()), "profile config is duplicated");
        keys.insert(profile.Key());
        configs.insert(profile.ConfigFile());
        const QString configPath = QStringLiteral(SIGIL_OPENCC_TEST_DATA_DIR)
            + QLatin1Char('/') + profile.ConfigFile();
        Require(QFileInfo(configPath).isFile(), "profile configuration is missing");
        Require(ChineseConversionProfile::ModeFromKey(profile.Key()).has_value(),
                "profile key does not round-trip");
    }

    Require(Convert(ChineseConversionMode::S2T, QStringLiteral("汉字"))
                == QStringLiteral("漢字"), "simplified-to-traditional conversion failed");
    Require(Convert(ChineseConversionMode::T2S, QStringLiteral("漢字"))
                == QStringLiteral("汉字"), "traditional-to-simplified conversion failed");
    Require(Convert(ChineseConversionMode::S2TWP, QStringLiteral("鼠标和软件"))
                == QStringLiteral("滑鼠和軟體"), "Taiwan phrase conversion failed");
    Require(Convert(ChineseConversionMode::S2HK, QStringLiteral("里面"))
                == QStringLiteral("裏面"), "Hong Kong variant conversion failed");

    OpenCCConverter missing(
        ChineseConversionProfile::ForMode(ChineseConversionMode::S2T),
        QStringLiteral("/path/that/does/not/exist"));
    Require(!missing.IsValid(), "missing OpenCC data directory was accepted");
    QString error;
    Require(missing.Convert(QStringLiteral("汉字"), &error) == QStringLiteral("汉字"),
            "invalid converter changed input");
    Require(!error.isEmpty(), "invalid converter did not report an error");

    QTemporaryDir temporaryDirectory;
    Require(temporaryDirectory.isValid(), "temporary settings directory is invalid");
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    ChineseConversionOptions savedOptions;
    savedOptions.mode = ChineseConversionMode::S2TWP;
    savedOptions.scope = ChineseConversionScope::CurrentSelection;
    savedOptions.includeAltText = false;
    savedOptions.skipPreElements = true;
    savedOptions.previewBeforeApply = false;
    ChineseConversionSettings::SaveTo(settings, savedOptions);
    settings.sync();
    const ChineseConversionOptions loadedOptions =
        ChineseConversionSettings::LoadFrom(settings);
    Require(loadedOptions.mode == ChineseConversionMode::S2TWP,
            "conversion mode setting did not round-trip");
    Require(loadedOptions.scope == ChineseConversionScope::CurrentSelection,
            "conversion scope setting did not round-trip");
    Require(!loadedOptions.includeAltText && loadedOptions.skipPreElements
                && !loadedOptions.previewBeforeApply,
            "conversion boolean settings did not round-trip");

    settings.beginGroup(QStringLiteral("chinese_conversion"));
    settings.setValue(QStringLiteral("mode"), QStringLiteral("not-a-mode"));
    settings.setValue(QStringLiteral("scope"), QStringLiteral("not-a-scope"));
    settings.endGroup();
    const ChineseConversionOptions fallbackOptions =
        ChineseConversionSettings::LoadFrom(settings);
    Require(fallbackOptions.mode == ChineseConversionMode::S2T,
            "invalid conversion mode did not use the safe default");
    Require(fallbackOptions.scope == ChineseConversionScope::CurrentFile,
            "invalid conversion scope did not use the safe default");

    return EXIT_SUCCESS;
}
