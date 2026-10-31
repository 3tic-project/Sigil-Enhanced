/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

#include "Misc/SettingsStore.h"
#include "Misc/TempCleanup.h"

namespace
{

const int RETRY_INTERVAL_MS = 500;
const int PYTHON_RELEASE_EVERY = 10;
const int SILENT_EXIT_WAIT_MS = 1000;

#ifdef Q_OS_WIN
const Qt::CaseSensitivity PATH_CASE = Qt::CaseInsensitive;
#else
const Qt::CaseSensitivity PATH_CASE = Qt::CaseSensitive;
#endif

QString ResolvedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

// A process cannot delete its own working directory on Windows, and embedded
// Python code shares that working directory with Sigil.
void MoveWorkingDirectoryOut(const QString &path)
{
    const QString root = ResolvedPath(path);
    const QString cwd = ResolvedPath(QDir::currentPath());
    if (cwd.compare(root, PATH_CASE) == 0 || cwd.startsWith(root + QLatin1Char('/'), PATH_CASE)) {
        QDir::setCurrent(QFileInfo(root).absolutePath());
    }
}

#ifdef Q_OS_WIN
void ClearReadOnlyAttribute(const QString &path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.size() >= MAX_PATH - 12 && !native.startsWith(QStringLiteral("\\\\?\\"))) {
        // QStringBuilder keeps references/value categories in its type, so
        // these concatenations cannot be operands of the same conditional.
        if (native.startsWith(QStringLiteral("\\\\"))) {
            native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
        } else {
            native = QStringLiteral("\\\\?\\") + native;
        }
    }
    const LPCWSTR wide = reinterpret_cast<LPCWSTR>(native.utf16());
    const DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(wide, attributes & ~FILE_ATTRIBUTE_READONLY);
    }
}
#endif

// Plugins can leave read-only files and folders behind; Windows refuses to
// delete read-only folders and POSIX refuses to empty unwritable ones.
void MakeTreeRemovable(const QString &path)
{
#ifdef Q_OS_WIN
    ClearReadOnlyAttribute(path);
#else
    QFile::setPermissions(path, QFile::permissions(path)
                          | QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink() || entry.isJunction()) continue;
        if (entry.isDir()) {
            MakeTreeRemovable(entry.filePath());
        } else {
#ifdef Q_OS_WIN
            ClearReadOnlyAttribute(entry.filePath());
#endif
        }
    }
}

}

TempCleanup &TempCleanup::instance()
{
    // Deliberately leaked: TempFolder destructors may run during static teardown.
    static TempCleanup *cleanup = new TempCleanup();
    return *cleanup;
}

TempCleanup::TempCleanup()
    : m_timer(this)
{
    if (QCoreApplication::instance()) {
        moveToThread(QCoreApplication::instance()->thread());
    }
    m_timer.setSingleShot(true);
    m_timer.setInterval(RETRY_INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &TempCleanup::RetryPending);
}

bool TempCleanup::RemoveTree(const QString &path)
{
    if (path.isEmpty() || !QDir(path).exists()) return true;
    MoveWorkingDirectoryOut(path);
    if (QDir(path).removeRecursively()) return true;
    MakeTreeRemovable(path);
    QDir(path).removeRecursively();
    return !QDir(path).exists();
}

void TempCleanup::Remove(const QString &path)
{
    if (RemoveTree(path)) return;
    {
        QMutexLocker locker(&m_mutex);
        if (m_pending.contains(path)) return;
        m_pending.append(path);
    }
    qWarning() << "Temporary directory is still in use; retrying removal later:" << path;
    ScheduleRetry();
}

QStringList TempCleanup::PendingPaths() const
{
    QMutexLocker locker(&m_mutex);
    return m_pending;
}

void TempCleanup::SetHandleReleaser(std::function<void(const QString &)> releaser)
{
    m_releaser = std::move(releaser);
}

void TempCleanup::ScheduleRetry()
{
    QMetaObject::invokeMethod(this, [this]() {
        if (!m_timer.isActive()) m_timer.start();
    }, Qt::QueuedConnection);
}

bool TempCleanup::RetryOnce(bool release_handles)
{
    for (const QString &path : PendingPaths()) {
        if (release_handles && m_releaser) m_releaser(path);
        if (RemoveTree(path)) {
            QMutexLocker locker(&m_mutex);
            m_pending.removeAll(path);
        }
    }
    const bool done = PendingPaths().isEmpty();
    if (done) m_retries = 0;
    return done;
}

void TempCleanup::RetryPending()
{
    const bool release_handles = m_retries++ % PYTHON_RELEASE_EVERY == 0;
    if (!RetryOnce(release_handles)) m_timer.start();
}

void TempCleanup::FinishBeforeExit()
{
    m_timer.stop();
    QElapsedTimer grace;
    grace.start();
    bool release_handles = true;
    // Most locks (virus scanners, the preview engine) disappear within a moment.
    while (!RetryOnce(release_handles)) {
        release_handles = false;
        if (grace.elapsed() >= SILENT_EXIT_WAIT_MS) break;
        QEventLoop wait;
        QTimer::singleShot(200, &wait, &QEventLoop::quit);
        wait.exec();
    }
    if (PendingPaths().isEmpty()) return;

    if (qobject_cast<QApplication *>(QCoreApplication::instance())) {
        QDialog dialog;
        dialog.setWindowTitle(tr("Cleaning Up Temporary Files"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *message = new QLabel(tr("Sigil is waiting for its temporary working files to be released so they can be deleted. It will exit when cleanup finishes."), &dialog);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *later = new QLabel(tr("If you exit now, Sigil will try again the next time it starts."), &dialog);
        later->setWordWrap(true);
        layout->addWidget(later);
        auto *count = new QLabel(&dialog);
        layout->addWidget(count);
        auto *progress = new QProgressBar(&dialog);
        progress->setRange(0, 0);
        layout->addWidget(progress);
        auto *paths = new QPlainTextEdit(&dialog);
        paths->setReadOnly(true);
        layout->addWidget(paths);
        auto *buttons = new QDialogButtonBox(&dialog);
        buttons->addButton(tr("Exit Without Waiting"), QDialogButtonBox::RejectRole);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.resize(560, 320);

        auto refresh = [&]() {
            const QStringList pending = PendingPaths();
            count->setText(tr("Folders still in use: %1").arg(pending.size()));
            paths->setPlainText(QDir::toNativeSeparators(pending.join(QLatin1Char('\n'))));
        };
        int ticks = 0;
        QTimer poll;
        poll.setInterval(RETRY_INTERVAL_MS);
        connect(&poll, &QTimer::timeout, &dialog, [&]() {
            if (RetryOnce(++ticks % PYTHON_RELEASE_EVERY == 0)) {
                dialog.accept();
            } else {
                refresh();
            }
        });
        refresh();
        poll.start();
        dialog.exec();
    }

    const QStringList remaining = PendingPaths();
    if (!remaining.isEmpty()) {
        qWarning() << "Exiting with temporary directories still in use:" << remaining;
        RecordLeftovers(remaining);
    }
}

void TempCleanup::RecordLeftovers(const QStringList &paths)
{
    SettingsStore settings;
    QStringList recorded = settings.pendingTempCleanup();
    for (const QString &path : paths) {
        if (!recorded.contains(path)) recorded.append(path);
    }
    settings.setPendingTempCleanup(recorded);
}

void TempCleanup::RemoveLeftoversFromPreviousRun()
{
    SettingsStore settings;
    const QStringList recorded = settings.pendingTempCleanup();
    if (recorded.isEmpty()) return;
    // Only ever delete directories that Sigil itself created and recorded.
    static const QRegularExpression owned(
        QStringLiteral("^(Sigil|sigil-plugin-session)-[A-Za-z0-9]{6}$"));
    QStringList remaining;
    for (const QString &path : recorded) {
        if (!owned.match(QFileInfo(path).fileName()).hasMatch()) continue;
        if (!RemoveTree(path)) remaining.append(path);
    }
    settings.setPendingTempCleanup(remaining);
}
