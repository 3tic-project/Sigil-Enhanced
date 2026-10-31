#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <iostream>
#include <memory>
#include <stdexcept>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void ready()
{
    std::cout << "READY\n" << std::flush;
    QTextStream input(stdin);
    require(input.readLine() == "go", "Missing worker start signal");
}

std::unique_ptr<QProcess> start(const QStringList& arguments)
{
    auto process = std::make_unique<QProcess>();
    process->start(QCoreApplication::applicationFilePath(), arguments);
    require(process->waitForStarted(10000), "Settings worker did not start");
    QElapsedTimer elapsed;
    elapsed.start();
    while (!process->canReadLine() && elapsed.elapsed() < 10000) process->waitForReadyRead(100);
    require(process->readLine().trimmed() == "READY", "Settings worker did not become ready");
    return process;
}

void finish(QProcess& process)
{
    require(process.state() == QProcess::NotRunning || process.waitForFinished(20000),
            "Settings worker blocked indefinitely");
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::cerr << process.readAllStandardError().constData();
        throw std::runtime_error("Settings worker crashed or failed");
    }
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Could not read settings bytes");
    return file.readAll();
}

int worker(const QStringList& args)
{
    require(args.size() >= 4, "Missing worker arguments");
    QSettings first(args.at(2), QSettings::IniFormat);
    QSettings second(args.at(3), QSettings::IniFormat);
    if (args.at(1) == "idle") {
        require(first.value("seed").toString() == QStringLiteral("共享配置𠮷") &&
                second.value("seed").toString() == QStringLiteral("共享配置𠮷"), "Worker cannot read shared settings");
        QFileSystemWatcher watcher;
        watcher.addPaths({first.fileName(), second.fileName()});
        ready(); // Keep QSettings and real file watchers alive while another process writes.
        first.sync();
        second.sync();
        require(first.value("second-process").toString() == "write succeeded while first instance remains open" &&
                second.value("second-process").toString() == "write succeeded while first instance remains open",
                "A running instance could not import another process's settings");
        return 0;
    }
    ready();
    if (args.at(1) == "read") {
        first.sync();
        second.sync();
        require(first.status() == QSettings::NoError && second.status() == QSettings::NoError &&
                first.value("seed").toString() == QStringLiteral("共享配置𠮷") &&
                second.value("seed").toString() == QStringLiteral("共享配置𠮷"),
                "Another process could not read locked settings");
        return 0;
    }
    if (args.at(1) == "denied") {
        first.setValue("denied-write", "must not replace the existing file");
        first.sync();
        require(first.status() == QSettings::AccessError, "Denied write did not report AccessError");
        return 0; // A file-sharing error must remain an ordinary error, not a process crash.
    }
    const int count = args.at(1) == "once" ? 1 : 20;
    const QString id = args.value(4, "single");
    for (int i = 0; i < count; ++i) {
        const QString key = QStringLiteral("workers/%1/value_%2").arg(id).arg(i);
        first.setValue(key, i);
        second.setValue(key, i);
        first.sync();
        second.sync();
        require(first.status() == QSettings::NoError && second.status() == QSettings::NoError,
                "Concurrent settings synchronization failed");
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const QStringList args = app.arguments();
        if (args.size() > 1) return worker(args);
        QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/Sigil-配置测试-XXXXXX"));
        require(temporary.isValid(), "Cannot create isolated settings directory");
        const QString firstPath = temporary.filePath("sigil_v6.ini");
        const QString secondPath = temporary.filePath("sigil_extend.ini");
        for (const QString& path : {firstPath, secondPath}) {
            QSettings settings(path, QSettings::IniFormat);
            require(settings.isAtomicSyncRequired(), "Atomic synchronization must remain enabled");
            settings.setValue("seed", QStringLiteral("共享配置𠮷"));
            settings.sync();
            require(settings.status() == QSettings::NoError, "Cannot initialize shared settings");
        }

        auto idle = start({"idle", firstPath, secondPath});
        for (const QString& path : {firstPath, secondPath}) {
#ifdef Q_OS_WIN
            const QString native = QDir::toNativeSeparators(path);
            HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            require(handle != INVALID_HANDLE_VALUE, "An idle Qt settings object/watcher kept the INI file open");
            CloseHandle(handle);
#endif
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("second-process", "write succeeded while first instance remains open");
            settings.sync();
            require(settings.status() == QSettings::NoError, "Idle settings objects blocked another instance");
        }
        idle->write("go\n");
        finish(*idle);

        QList<std::shared_ptr<QProcess>> writers;
        for (int i = 0; i < 4; ++i) {
            auto process = start({"writer", firstPath, secondPath, QString::number(i)});
            writers.append(std::shared_ptr<QProcess>(process.release()));
        }
        for (const auto& process : writers) process->write("go\n");
        for (const auto& process : writers) finish(*process);
        for (const QString& path : {firstPath, secondPath}) {
            QSettings settings(path, QSettings::IniFormat);
            settings.sync();
            require(settings.status() == QSettings::NoError, "Shared INI became unreadable");
            for (int id = 0; id < 4; ++id) {
                for (int i = 0; i < 20; ++i) {
                    require(settings.value(QStringLiteral("workers/%1/value_%2").arg(id).arg(i), -1).toInt() == i,
                            "Concurrent writers lost another process's keys");
                }
            }
            require(settings.value("seed").toString() == QStringLiteral("共享配置𠮷"), "Shared Unicode data changed");
        }

        // Hold the same advisory lock Qt uses, then release it while the writer waits.
        QLockFile gate(firstPath + ".lock");
        require(gate.tryLock(), "Cannot create a controlled settings lock");
        auto waiting = start({"once", firstPath, secondPath});
        waiting->write("go\n");
        require(!waiting->waitForFinished(200), "A writer ignored another process's settings lock");
        auto reader = start({"read", firstPath, secondPath});
        reader->write("go\n");
        finish(*reader);
        gate.unlock();
        finish(*waiting);

        const QString blocked = temporary.filePath("blocked.ini");
        require(QDir().mkdir(blocked), "Cannot create controlled write failure");
        auto denied = start({"denied", blocked, secondPath});
        denied->write("go\n");
        finish(*denied);
        require(QDir().rmdir(blocked), "Cannot release controlled write failure");
        {
            QSettings recovered(blocked, QSettings::IniFormat);
            recovered.setValue("recovered", true);
            recovered.sync();
            require(recovered.status() == QSettings::NoError, "Settings did not recover after write access returned");
        }

#ifdef Q_OS_WIN
        const QByteArray before = readFile(firstPath);
        const QString native = QDir::toNativeSeparators(firstPath);
        HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot hold INI open with Windows sharing restrictions");
        auto sharingReader = start({"read", firstPath, secondPath});
        sharingReader->write("go\n");
        finish(*sharingReader);
        auto sharingDenied = start({"denied", firstPath, secondPath});
        sharingDenied->write("go\n");
        finish(*sharingDenied);
        require(readFile(firstPath) == before, "A Windows sharing violation damaged existing configuration");
        CloseHandle(held);
        QSettings recovered(firstPath, QSettings::IniFormat);
        recovered.setValue("after-sharing-violation", true);
        recovered.sync();
        require(recovered.status() == QSettings::NoError, "INI write did not recover after the Windows handle was released");
        std::cout << "Windows exclusive handles and sharing-violation recovery passed\n";
#endif
        std::cout << "Concurrent INI reads/writes, lock contention and access failures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
