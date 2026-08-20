/************************************************************************
**
**  Copyright (C) 2026 Sigil-Enhanced contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/KfxImportController.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>

#include "BuiltinPlugins/KfxImportProtocol.h"
#ifdef slots
#undef slots
#define SIGIL_KFX_RESTORE_QT_SLOTS
#endif
#include "EmbedPython/EmbeddedPython.h"
#ifdef SIGIL_KFX_RESTORE_QT_SLOTS
#define slots Q_SLOTS
#undef SIGIL_KFX_RESTORE_QT_SLOTS
#endif
#include "Misc/PluginDB.h"

namespace BuiltinPlugins
{

namespace
{

bool isUsableInterpreter(const QString& path)
{
    const QFileInfo info(path);
    return info.isFile() && info.isReadable() && info.isExecutable();
}

QString phaseText(const QString& phase)
{
    if (phase == QStringLiteral("preflight")) {
        return KfxImportController::tr("Checking KFX input...");
    }
    if (phase == QStringLiteral("parse")) {
        return KfxImportController::tr("Reading KFX data...");
    }
    if (phase == QStringLiteral("convert")) {
        return KfxImportController::tr("Converting KFX to EPUB...");
    }
    if (phase == QStringLiteral("write")) {
        return KfxImportController::tr("Writing temporary EPUB...");
    }
    if (phase == QStringLiteral("validate")) {
        return KfxImportController::tr("Validating converted EPUB...");
    }
    return KfxImportController::tr("Converting KFX to EPUB...");
}

}

QString KfxImportController::pythonInterpreter()
{
    const QString bundled = PluginDB::buildBundledInterpPath();
    if (isUsableInterpreter(bundled)) {
        return bundled;
    }

#if defined(Q_OS_MAC) && !defined(NDEBUG)
    const QString debug_runtime = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("../python-runtime/bin/python3"));
    if (isUsableInterpreter(debug_runtime)) {
        return debug_runtime;
    }
    return QString();
#endif

    const QString configured = PluginDB::instance().get_engine_path(QStringLiteral("python3.4"));
    if (isUsableInterpreter(configured)) {
        return configured;
    }

    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (isUsableInterpreter(python3)) {
        return python3;
    }
    return QString();
}

QString KfxImportController::userFacingError(const QString& code, const QString& fallback)
{
    if (code == QStringLiteral("KFX-E-INPUT")) {
        return tr("The selected KFX file cannot be read safely.");
    }
    if (code == QStringLiteral("KFX-E-UNSUPPORTED")) {
        return tr("This KFX container uses an unsupported feature.");
    }
    if (code == QStringLiteral("KFX-E-DRM")) {
        return tr("This KFX file is DRM-protected and cannot be converted.");
    }
    if (code == QStringLiteral("KFX-E-NOSPACE")) {
        return tr("There is not enough free disk space to create the EPUB.");
    }
    if (code == QStringLiteral("KFX-E-MALFORMED")) {
        return tr("The KFX file is malformed or incomplete.");
    }
    if (code == QStringLiteral("KFX-E-VALIDATE")) {
        return tr("The converted file did not pass EPUB validation and was discarded.");
    }
    if (code == QStringLiteral("KFX-E-PROTOCOL")) {
        return tr("The KFX converter returned an invalid response.");
    }
    if (code == QStringLiteral("KFX-E-RUNTIME")) {
        return tr("No usable Python 3 interpreter could be found for the KFX converter.");
    }
    return fallback.isEmpty() ? tr("KFX conversion failed.") : fallback;
}

KfxImportController::Result KfxImportController::convert(const QString& sourcePath, QWidget* parent)
{
    Result result;
    const QString interpreter = pythonInterpreter();
    if (interpreter.isEmpty()) {
        result.errorCode = QStringLiteral("KFX-E-RUNTIME");
        result.errorMessage = userFacingError(result.errorCode, QString());
        return result;
    }

    const QString python_root = EmbeddedPython::instance().embeddedRoot();
    const QString worker_module = QDir(python_root).filePath(
        QStringLiteral("sigil_kfx_import/worker.py"));
    const QString worker_bootstrap = QDir(python_root).filePath(
        QStringLiteral("sigil_kfx_import/bootstrap.py"));
    if (!QFileInfo(worker_module).isFile() || !QFileInfo(worker_bootstrap).isFile()) {
        result.errorCode = QStringLiteral("KFX-E-RUNTIME");
        result.errorMessage = tr("The built-in KFX converter is missing from this installation.");
        return result;
    }

    QTemporaryFile output(QDir::tempPath() + QStringLiteral("/sigil-kfx-import-XXXXXX.epub"));
    output.setAutoRemove(false);
    if (!output.open()) {
        result.errorCode = QStringLiteral("KFX-E-NOSPACE");
        result.errorMessage = tr("Cannot create a temporary EPUB file: %1").arg(output.errorString());
        return result;
    }
    result.outputPath = output.fileName();
    output.close();

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QStringList python_variables = QStringList()
        << QStringLiteral("PYTHONHOME")
        << QStringLiteral("PYTHONPATH")
        << QStringLiteral("PYTHONSTARTUP")
        << QStringLiteral("PYTHONINSPECT")
        << QStringLiteral("PYTHONUSERBASE")
        << QStringLiteral("PYTHONWARNINGS");
    foreach(const QString& variable, python_variables) {
        environment.remove(variable);
    }
#ifdef Q_OS_WIN32
    if (interpreter == PluginDB::buildBundledInterpPath()) {
        environment.insert(QStringLiteral("PYTHONHOME"), QFileInfo(interpreter).absolutePath());
    }
#endif
    environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("UTF-8"));
    environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);

    const QStringList arguments = QStringList()
        << QStringLiteral("-u")
        << QStringLiteral("-B")
        << QStringLiteral("-I")
        << QStringLiteral("-S")
        << worker_bootstrap
        << QStringLiteral("--input") << QFileInfo(sourcePath).absoluteFilePath()
        << QStringLiteral("--output") << result.outputPath
        << QStringLiteral("--job-id") << QUuid::createUuid().toString(QUuid::WithoutBraces);

    QProgressDialog progress(tr("Starting KFX conversion..."), tr("Cancel"), 0, 100, parent);
    progress.setWindowTitle(tr("Convert KFX to EPUB"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QByteArray stdout_buffer;
    QByteArray stderr_buffer;
    bool protocol_error = false;
    bool received_result = false;
    QEventLoop loop;
    QTimer kill_timer;
    kill_timer.setSingleShot(true);

    auto consumeLine = [&](const QByteArray& line) {
        if (line.trimmed().isEmpty()) {
            return;
        }
        KfxWorkerEvent event;
        QString parse_error;
        if (!KfxImportProtocol::parseLine(line, &event, &parse_error)) {
            protocol_error = true;
            if (result.diagnosticDetails.size() < 32768) {
                result.diagnosticDetails += parse_error + QLatin1Char('\n');
            }
            return;
        }
        switch (event.type) {
        case KfxWorkerEvent::Phase:
            progress.setLabelText(phaseText(event.name));
            break;
        case KfxWorkerEvent::Progress:
            if (event.total > 0) {
                progress.setRange(0, event.total);
                progress.setValue(qBound(0, event.current, event.total));
            }
            break;
        case KfxWorkerEvent::Warning:
            if (!event.message.isEmpty() && result.warnings.size() < 1000
                && !result.warnings.contains(event.message)) {
                result.warnings << event.message;
            }
            break;
        case KfxWorkerEvent::Success:
            received_result = true;
            result.succeeded = true;
            result.summary = event.summary;
            break;
        case KfxWorkerEvent::Error:
            received_result = true;
            result.errorCode = event.code;
            result.errorMessage = userFacingError(event.code, event.message);
            break;
        default:
            break;
        }
    };

    auto consumeStdout = [&]() {
        stdout_buffer += process.readAllStandardOutput();
        qsizetype newline = -1;
        while ((newline = stdout_buffer.indexOf('\n')) >= 0) {
            consumeLine(stdout_buffer.left(newline));
            stdout_buffer.remove(0, newline + 1);
        }
    };

    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, consumeStdout);
    QObject::connect(&process, &QProcess::readyReadStandardError, &process, [&]() {
        if (stderr_buffer.size() < 65536) {
            stderr_buffer += process.readAllStandardError().left(65536 - stderr_buffer.size());
        } else {
            process.readAllStandardError();
        }
    });
    QObject::connect(&progress, &QProgressDialog::canceled, &process, [&]() {
        // QProgressDialog::close() emits canceled() on some Qt platforms. Once
        // the worker has stopped, closing our own progress dialog must not turn
        // an already completed conversion into a user cancellation.
        if (process.state() == QProcess::NotRunning) {
            return;
        }
        result.cancelled = true;
        process.terminate();
        kill_timer.start(3000);
    });
    QObject::connect(&kill_timer, &QTimer::timeout, &process, [&]() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
        }
    });
    QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     &loop, &QEventLoop::quit);

    process.start(interpreter, arguments);
    if (!process.waitForStarted(5000)) {
        progress.close();
        QFile::remove(result.outputPath);
        result.outputPath.clear();
        result.errorCode = QStringLiteral("KFX-E-RUNTIME");
        result.errorMessage = tr("The KFX converter could not start: %1").arg(process.errorString());
        return result;
    }

    progress.show();
    if (process.state() != QProcess::NotRunning) {
        loop.exec();
    }
    kill_timer.stop();
    consumeStdout();
    if (!stdout_buffer.trimmed().isEmpty()) {
        consumeLine(stdout_buffer);
    }
    if (stderr_buffer.size() < 65536) {
        stderr_buffer += process.readAllStandardError().left(65536 - stderr_buffer.size());
    }
    progress.close();
    result.diagnosticDetails += QString::fromUtf8(stderr_buffer).trimmed();

    if (result.cancelled) {
        result.succeeded = false;
        QFile::remove(result.outputPath);
        result.outputPath.clear();
        return result;
    }
    if (protocol_error || !received_result) {
        result.succeeded = false;
        result.errorCode = QStringLiteral("KFX-E-PROTOCOL");
        result.errorMessage = userFacingError(result.errorCode, QString());
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.succeeded = false;
        if (result.errorCode.isEmpty()) {
            result.errorCode = QStringLiteral("KFX-E-CONVERT");
            result.errorMessage = userFacingError(result.errorCode, QString());
        }
    }
    if (result.succeeded
        && (result.outputPath.isEmpty() || !QFileInfo(result.outputPath).isFile()
            || QFileInfo(result.outputPath).size() <= 0)) {
        result.succeeded = false;
        result.errorCode = QStringLiteral("KFX-E-VALIDATE");
        result.errorMessage = userFacingError(result.errorCode, QString());
    }

    if (!result.succeeded) {
        QFile::remove(result.outputPath);
        result.outputPath.clear();
    }
    return result;
}

}
