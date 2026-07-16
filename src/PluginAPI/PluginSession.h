/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef PLUGINSESSION_H
#define PLUGINSESSION_H

#include <QHash>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QUuid>

#include <memory>

#include "Misc/Plugin.h"
#include "PluginAPI/PluginProtocol.h"

class ContentTab;
class MainWindow;
class PluginSessionConsole;
class QLocalServer;
class QLocalSocket;
class Resource;
class TabManager;
class TextResource;
class QTemporaryFile;
class QTemporaryDir;
class QTimer;

namespace PluginApi
{
class TextTransaction;
}

class PluginSession : public QObject
{
    Q_OBJECT

public:
    PluginSession(const Plugin &plugin,
                  MainWindow *main_window,
                  TabManager *tab_manager,
                  QObject *parent = nullptr);
    ~PluginSession() override;

    QUuid SessionId() const;
    QString PluginName() const;
    QString PluginType() const;
    QString Status() const;
    int ValidationErrorCount() const;
    bool IsBookSession() const;
    QString PendingInputEpubPath() const;
    bool Start(QString *error);
    void Cancel();

signals:
    void Ended();

private slots:
    void AcceptConnection();
    void ReadMessages();
    void ProcessFinished(int exit_code, QProcess::ExitStatus exit_status);

private:
    struct BinaryReadStream {
        QTemporaryFile *file = nullptr;
        QString resourceId;
        QString bookPath;
        quint64 revision = 0;
        qint64 size = 0;
        QString sha256;
    };

    struct InputUpload {
        QTemporaryFile *file = nullptr;
        QString filename;
        qint64 expectedSize = -1;
        qint64 received = 0;
    };

    struct BinaryWriteUpload {
        QTemporaryFile *file = nullptr;
        QString transactionId;
        QString resourceId;
        quint64 expectedRevision = 0;
        qint64 expectedSize = 0;
        qint64 received = 0;
    };

    struct TextWriteUpload {
        QTemporaryFile *file = nullptr;
        QString transactionId;
        QString targetId;
        QString bookPath;
        QString mediaType;
        QString manifestId;
        QString properties;
        QString fallback;
        QString overlay;
        quint64 expectedRevision = 0;
        qint64 expectedSize = 0;
        qint64 received = 0;
        qint64 lastOffset = -1;
        QString lastChunkSha256;
        bool addResource = false;
        bool manifested = true;
        bool addToSpine = true;
    };

    void Dispatch(const QJsonObject &request);
    void Respond(const QJsonValue &id, const QJsonValue &result);
    void RespondError(const QJsonValue &id, int code, const QString &message,
                      const QJsonValue &data = QJsonValue());
    void Notify(const QString &method, QJsonObject params = QJsonObject());
    void QueueNotification(const QString &method, const QJsonObject &params, int delay_ms);
    QJsonObject ResourceInfo(Resource *resource) const;
    Resource *ResolveResource(const QString &resource_id) const;
    TextResource *ResolveTextResource(const QString &resource_id) const;
    void TrackResource(Resource *resource);
    void TrackEditorTab(ContentTab *tab);
    bool AcquireWriter();
    void ReleaseWriter();
    void ClearBinaryWriteUploads();
    void ClearTextWriteUploads();
    PluginApi::TextTransaction *RequireTransaction(const QJsonObject &params,
                                                   const QJsonValue &request_id);
    quint64 Revision(Resource *resource) const;
    QString EditorStateToken(ContentTab *tab) const;
    QJsonObject EditorState() const;
    QJsonObject EditorEventState() const;
    QString ResolveInterpreter() const;
    void Finish(const QString &status, const QString &message = QString());
    void CleanServer();

    Plugin m_Plugin;
    MainWindow *m_MainWindow;
    TabManager *m_TabManager;
    QUuid m_SessionId;
    QString m_ServerName;
    QString m_Token;
    QLocalServer *m_Server;
    QLocalSocket *m_Socket;
    QProcess *m_Process;
    PluginApi::FrameDecoder m_Decoder;
    bool m_Authenticated;
    bool m_Ending;
    bool m_EndSignalScheduled;
    QSet<QString> m_Subscriptions;
    QHash<QString, QJsonObject> m_PendingNotifications;
    QHash<QString, QTimer *> m_NotificationTimers;
    int m_DroppedNotifications;
    QSet<ContentTab *> m_TrackedEditorTabs;
    QString m_ProgressId;
    QString m_ProgressLabel;
    int m_ProgressMaximum;
    int m_ValidationErrorCount;
    QString m_Status;
    QHash<QString, quint64> m_ResourceRevisions;
    QHash<QString, BinaryReadStream> m_BinaryReadStreams;
    qint64 m_BinaryReadBytes;
    QHash<QString, BinaryWriteUpload> m_BinaryWriteUploads;
    QHash<QString, TextWriteUpload> m_TextWriteUploads;
    QHash<QString, InputUpload> m_InputUploads;
    QList<QTemporaryFile *> m_MaterializedFiles;
    qint64 m_MaterializedBytes;
    QTemporaryDir *m_MaterializationRoot;
    QTemporaryFile *m_InputEpubFile;
    bool m_InputEpubAccepted;
    quint64 m_BookRevision;
    bool m_InRequest;
    bool m_HoldsWriter;
    QElapsedTimer m_RequestWindow;
    int m_RequestsInWindow;
    std::unique_ptr<PluginApi::TextTransaction> m_Transaction;
    QPointer<PluginSessionConsole> m_Console;
};

#endif // PLUGINSESSION_H
