#include "EmbedPython/EmbeddedPython.h" // Python must precede Qt's slots macro.

#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QTimer>
#include <QWebEngineUrlScheme>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "BookManipulation/Book.h"
#include "Dialogs/MetaEditor.h"
#include "MainUI/MainApplication.h"
#include "MainUI/MainWindow.h"
#include "Misc/SettingsStore.h"
#include "ResourceObjects/OPFResource.h"

namespace
{

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read EPUB fixture or output");
    return file.readAll();
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool invokeBoolSlot(MainWindow& window, const char* name)
{
    bool result = false;
    require(QMetaObject::invokeMethod(
                &window, name, Qt::DirectConnection, Q_RETURN_ARG(bool, result)),
            "Could not invoke a MainWindow save slot");
    return result;
}

class ModalDriver final : public QObject
{
public:
    explicit ModalDriver(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void chooseSavePath(const QString& path)
    {
        m_Path = path;
        m_Mode = Mode::SavePath;
        m_Attempts = 0;
        QTimer::singleShot(0, this, [this]() { drive(); });
    }

    void cancelMetadataEditor()
    {
        m_Mode = Mode::CancelMetadata;
        m_Attempts = 0;
        QTimer::singleShot(0, this, [this]() { drive(); });
    }

    void acceptMetadataEditor()
    {
        m_Mode = Mode::AcceptMetadata;
        m_Attempts = 0;
        QTimer::singleShot(0, this, [this]() { drive(); });
    }

private:
    enum class Mode {
        None,
        SavePath,
        CancelMetadata,
        AcceptMetadata
    };

    void drive()
    {
        if (++m_Attempts > 500) {
            std::cerr << "Timed out waiting for the expected modal dialog\n";
            std::_Exit(EXIT_FAILURE);
        }
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) {
                continue;
            }
            if (m_Mode == Mode::SavePath) {
                if (auto* dialog = qobject_cast<QFileDialog*>(widget)) {
                    dialog->selectFile(m_Path);
                    const QString selected = dialog->selectedFiles().value(0);
                    if (QFileInfo(selected).absoluteFilePath() !=
                        QFileInfo(m_Path).absoluteFilePath()) {
                        std::cerr << "Save dialog did not select the requested destination\n";
                        std::_Exit(EXIT_FAILURE);
                    }
                    m_Mode = Mode::None;
                    if (!QMetaObject::invokeMethod(
                            dialog, "accept", Qt::QueuedConnection)) {
                        std::cerr << "Could not accept the real save dialog\n";
                        std::_Exit(EXIT_FAILURE);
                    }
                    return;
                }
            } else if (m_Mode == Mode::CancelMetadata) {
                if (auto* dialog = qobject_cast<MetaEditor*>(widget)) {
                    m_Mode = Mode::None;
                    require(QMetaObject::invokeMethod(
                                dialog, "reject", Qt::QueuedConnection),
                            "Could not cancel Metadata Editor through its real reject slot");
                    return;
                }
            } else if (m_Mode == Mode::AcceptMetadata) {
                if (auto* dialog = qobject_cast<MetaEditor*>(widget)) {
                    m_Mode = Mode::None;
                    require(QMetaObject::invokeMethod(
                                dialog, "saveData", Qt::QueuedConnection),
                            "Could not accept Metadata Editor through its real saveData slot");
                    return;
                }
            }
        }
        QTimer::singleShot(10, this, [this]() { drive(); });
    }

    Mode m_Mode = Mode::None;
    QString m_Path;
    int m_Attempts = 0;
};

}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QWebEngineUrlScheme scheme("sigil");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme |
                    QWebEngineUrlScheme::LocalScheme |
                    QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::ContentSecurityPolicyIgnored |
                    QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
    MainApplication app(argc, argv);

    try {
        require(argc == 3, "Expected source root and disposable EPUB fixture");
        const QString root = QString::fromLocal8Bit(argv[1]);
        const QString input = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QString save_as = input + QStringLiteral(".save-as.epub");
        const QString save_copy = input + QStringLiteral(".save-copy.epub");
        auto& python = EmbeddedPython::instance();
        python.addToPythonSysPath(qEnvironmentVariable("SIGIL_TEST_PYTHON_ROOT"));
        python.addToPythonSysPath(root + "/src/Resource_Files/plugin_launchers/python");
        python.addToPythonSysPath(root + "/src/Resource_Files/python3lib");

        SettingsStore settings;
        settings.setPreserveOPFSource(true);
        settings.setCleanOn(0);
        const QByteArray original = readBytes(input);

        MainWindow window(input);
        const auto book = window.GetCurrentBook();
        require(book && book->GetOPF(), "MainWindow did not load the EPUB fixture");
        require(!book->IsModified(), "Opening the EPUB marked the live Book modified");
        require(window.GetCurrentFilePath() == input,
                "MainWindow did not retain the opened EPUB path");

        require(invokeBoolSlot(window, "Save"),
                "MainWindow Save failed for an unchanged EPUB");
        require(readBytes(input) == original,
                "MainWindow Save changed bytes in an unchanged EPUB");
        require(!book->IsModified(), "No-op Save marked the live Book modified");

        ModalDriver driver(&window);
        driver.chooseSavePath(save_as);
        require(invokeBoolSlot(window, "SaveAs"),
                "MainWindow Save As failed for an unchanged EPUB");
        require(readBytes(save_as) == original,
                "MainWindow Save As did not produce a byte-identical EPUB");
        require(readBytes(input) == original,
                "MainWindow Save As changed the original EPUB");
        require(window.GetCurrentFilePath() == save_as,
                "MainWindow Save As did not update the current EPUB path");
        require(!book->IsModified(), "No-op Save As marked the live Book modified");

        driver.chooseSavePath(save_copy);
        require(invokeBoolSlot(window, "SaveACopy"),
                "MainWindow Save A Copy failed for an unchanged EPUB");
        require(readBytes(save_copy) == original,
                "MainWindow Save A Copy did not produce a byte-identical EPUB");
        require(readBytes(save_as) == original && readBytes(input) == original,
                "MainWindow Save A Copy changed an existing EPUB");
        require(window.GetCurrentFilePath() == save_as,
                "Save A Copy unexpectedly changed the current EPUB path");
        require(!book->IsModified(), "No-op Save A Copy marked the live Book modified");

        const QString opf_before = book->GetOPF()->GetText();
        driver.cancelMetadataEditor();
        require(QMetaObject::invokeMethod(
                    &window, "MetaEditorDialog", Qt::DirectConnection),
                "Could not invoke MainWindow Metadata Editor action");
        require(book->GetOPF()->GetText() == opf_before,
                "Cancelling Metadata Editor changed the OPF source");
        require(!book->IsModified(),
                "Cancelling Metadata Editor marked the live Book modified");
        require(readBytes(save_as) == original,
                "Cancelling Metadata Editor changed the current EPUB on disk");

        // Accepting unchanged metadata must write what the legacy metaproc
        // round trip produced for the same OPF text.
        python.addToPythonSysPath(root + "/tests/fixtures");
        int rv = 0;
        QString traceback;
        const QString expected_opf = python.runInPython(
            "metaproc_roundtrip_legacy", "roundtrip",
            {opf_before, book->GetConstOPF()->GetEpubVersion()}, &rv, traceback).toString();
        require(rv == 0, "Legacy metadata round trip raised");
        driver.acceptMetadataEditor();
        require(QMetaObject::invokeMethod(
                    &window, "MetaEditorDialog", Qt::DirectConnection),
                "Could not invoke MainWindow Metadata Editor action for accept");
        require(book->GetOPF()->GetText() == expected_opf,
                "Accepting Metadata Editor differs from the legacy metaproc round trip");

        std::cout << "MainWindow no-op acceptance passed: SHA-256 "
                  << sha256(original).toStdString()
                  << ", Save/Save As/Save A Copy byte-identical, metadata cancel clean, accept matches legacy\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
