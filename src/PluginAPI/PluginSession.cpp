/************************************************************************
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "PluginAPI/PluginSession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QPalette>
#include <QRandomGenerator>
#include <QWriteLocker>
#include <QTimer>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "Misc/PluginDB.h"
#include "Misc/SettingsStore.h"
#include "PluginAPI/PluginSessionConsole.h"
#include "PluginAPI/PluginTextEdit.h"
#include "PluginAPI/PluginTextTransaction.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"
#include "Tabs/ContentTab.h"
#include "Tabs/TabManager.h"
#include "ViewEditors/Searchable.h"

namespace
{

QString ResourceTypeName(Resource::ResourceType type)
{
    switch (type) {
        case Resource::HTMLResourceType: return QStringLiteral("html");
        case Resource::CSSResourceType: return QStringLiteral("css");
        case Resource::ImageResourceType: return QStringLiteral("image");
        case Resource::SVGResourceType: return QStringLiteral("svg");
        case Resource::FontResourceType: return QStringLiteral("font");
        case Resource::OPFResourceType: return QStringLiteral("opf");
        case Resource::NCXResourceType: return QStringLiteral("ncx");
        case Resource::AudioResourceType: return QStringLiteral("audio");
        case Resource::VideoResourceType: return QStringLiteral("video");
        case Resource::XMLResourceType: return QStringLiteral("xml");
        case Resource::TextResourceType:
        case Resource::MiscTextResourceType: return QStringLiteral("text");
        default: return QStringLiteral("binary");
    }
}

QJsonValue RequestId(const QJsonObject &request)
{
    return request.contains(QStringLiteral("id"))
        ? request.value(QStringLiteral("id")) : QJsonValue();
}

bool ReadPosition(const QJsonObject &params, const QString &name, int *position)
{
    const QJsonValue value = params.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > std::numeric_limits<int>::max()) {
        return false;
    }
    *position = static_cast<int>(number);
    return true;
}

bool ReadRevision(const QJsonObject &params, const QString &name, quint64 *revision)
{
    const QJsonValue value = params.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < 0 || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    *revision = static_cast<quint64>(number);
    return true;
}

}

PluginSession::PluginSession(const Plugin &plugin,
                             MainWindow *main_window,
                             TabManager *tab_manager,
                             QObject *parent) :
    QObject(parent),
    m_Plugin(plugin),
    m_MainWindow(main_window),
    m_TabManager(tab_manager),
    m_SessionId(QUuid::createUuid()),
    m_Server(nullptr),
    m_Socket(nullptr),
    m_Process(new QProcess(this)),
    m_Authenticated(false),
    m_Ending(false),
    m_EndSignalScheduled(false),
    m_Permissions(EffectivePermissions()),
    m_BookRevision(1)
{
}

PluginSession::~PluginSession()
{
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->kill();
        m_Process->waitForFinished(1000);
    }
    CleanServer();
}

QUuid PluginSession::SessionId() const
{
    return m_SessionId;
}

bool PluginSession::Start(QString *error)
{
    const QString interpreter = ResolveInterpreter();
    if (interpreter.isEmpty()) {
        if (error) {
            *error = tr("No Python 3 interpreter is configured.");
        }
        return false;
    }

    const QString launcher = PluginDB::launcherRoot() + QStringLiteral("/python/live_launcher.py");
    const QString plugin_path = PluginDB::pluginsPath() + QLatin1Char('/')
        + m_Plugin.get_name() + QStringLiteral("/plugin.py");
    if (!QFileInfo::exists(launcher) || !QFileInfo::exists(plugin_path)) {
        if (error) {
            *error = tr("The live plugin launcher or plugin entry point does not exist.");
        }
        return false;
    }

#ifdef Q_OS_WIN
    m_ServerName = QStringLiteral("sigil-plugin-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(m_SessionId.toString(QUuid::WithoutBraces));
#else
    m_ServerName = QDir::temp().absoluteFilePath(
        QStringLiteral("sigil-p-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(m_SessionId.toString(QUuid::WithoutBraces).left(12)));
#endif
    m_Token = QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QString::number(QRandomGenerator::global()->generate64(), 16);

    QLocalServer::removeServer(m_ServerName);
    m_Server = new QLocalServer(this);
    m_Server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_Server->listen(m_ServerName)) {
        if (error) {
            *error = tr("Could not create the local plugin server: %1").arg(m_Server->errorString());
        }
        CleanServer();
        return false;
    }
    connect(m_Server, &QLocalServer::newConnection, this, &PluginSession::AcceptConnection);

    QSharedPointer<Book> book = m_MainWindow->GetCurrentBook();
    for (Resource *resource : book->GetFolderKeeper()->GetResourceList()) {
        m_ResourceRevisions.insert(resource->GetIdentifier(), 1);
        connect(resource, &Resource::Modified, this, [this, resource]() {
            m_ResourceRevisions[resource->GetIdentifier()] += 1;
            m_BookRevision += 1;
        });
        connect(resource, &Resource::Deleted, this, [this, resource](const Resource *) {
            m_ResourceRevisions.remove(resource->GetIdentifier());
            m_BookRevision += 1;
        });
    }

    m_Console = new PluginSessionConsole(m_Plugin.get_name(), m_MainWindow);
    connect(m_Console, &PluginSessionConsole::CancelRequested, this, &PluginSession::Cancel);
    m_Console->show();

    connect(m_Process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_Console) {
            m_Console->AppendOutput(QString::fromUtf8(m_Process->readAllStandardOutput()));
        }
    });
    connect(m_Process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_Console) {
            m_Console->AppendOutput(QString::fromUtf8(m_Process->readAllStandardError()));
        }
    });
    connect(m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        Finish(QStringLiteral("failed"), m_Process->errorString());
    });
    connect(m_Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PluginSession::ProcessFinished);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SIGIL_PLUGIN_SOCKET"), m_ServerName);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_TOKEN"), m_Token);
    environment.insert(QStringLiteral("SIGIL_PLUGIN_API_VERSION"), QStringLiteral("2"));
    m_Process->setProcessEnvironment(environment);
    m_Process->setProgram(interpreter);
    m_Process->setArguments(QStringList {
        launcher,
        QStringLiteral("--plugin"), plugin_path,
        QStringLiteral("--plugin-name"), m_Plugin.get_name()
    });
    m_Process->setWorkingDirectory(QFileInfo(plugin_path).absolutePath());
    m_Process->start();

    QTimer::singleShot(10000, this, [this]() {
        if (!m_Authenticated && !m_Ending) {
            Finish(QStringLiteral("failed"), tr("Plugin handshake timed out."));
            Cancel();
        }
    });
    return true;
}

void PluginSession::Cancel()
{
    if (m_Ending) {
        return;
    }
    m_Ending = true;
    if (m_Socket && m_Socket->state() == QLocalSocket::ConnectedState) {
        const QJsonObject notification {
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("method"), QStringLiteral("session.cancelRequested") },
            { QStringLiteral("params"), QJsonObject() }
        };
        m_Socket->write(PluginApi::EncodeFrame(notification));
        m_Socket->flush();
    }
    if (m_Process->state() != QProcess::NotRunning) {
        m_Process->terminate();
        if (!m_Process->waitForFinished(2000)) {
            m_Process->kill();
            m_Process->waitForFinished(2000);
        }
    }
    Finish(QStringLiteral("cancelled"));
}

void PluginSession::AcceptConnection()
{
    while (m_Server && m_Server->hasPendingConnections()) {
        QLocalSocket *candidate = m_Server->nextPendingConnection();
        if (m_Socket) {
            candidate->disconnectFromServer();
            candidate->deleteLater();
            continue;
        }
        m_Socket = candidate;
        connect(m_Socket, &QLocalSocket::readyRead, this, &PluginSession::ReadMessages);
        connect(m_Socket, &QLocalSocket::disconnected, this, [this]() {
            if (!m_Ending && m_Process->state() != QProcess::NotRunning) {
                Finish(QStringLiteral("failed"), tr("Plugin control connection closed unexpectedly."));
            }
        });
    }
}

void PluginSession::ReadMessages()
{
    QList<QJsonObject> messages;
    QString error;
    if (!m_Decoder.Append(m_Socket->readAll(), &messages, &error)) {
        RespondError(QJsonValue(), -32700, error);
        m_Socket->disconnectFromServer();
        return;
    }
    for (const QJsonObject &message : messages) {
        Dispatch(message);
    }
}

void PluginSession::ProcessFinished(int exit_code, QProcess::ExitStatus exit_status)
{
    if (m_Ending) {
        return;
    }
    if (exit_status == QProcess::CrashExit || exit_code != 0) {
        Finish(QStringLiteral("failed"), tr("Plugin process exited with code %1.").arg(exit_code));
    } else {
        Finish(QStringLiteral("success"));
    }
}

void PluginSession::Dispatch(const QJsonObject &request)
{
    const QJsonValue id = RequestId(request);
    if (request.value(QStringLiteral("jsonrpc")) != QStringLiteral("2.0")
        || !request.value(QStringLiteral("method")).isString()) {
        RespondError(id, -32600, QStringLiteral("Invalid Request"));
        return;
    }
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (!m_Authenticated) {
        if (method != QStringLiteral("session.hello")
            || params.value(QStringLiteral("token")).toString() != m_Token
            || params.value(QStringLiteral("protocol_version")).toInt() != PluginApi::PROTOCOL_VERSION
            || params.value(QStringLiteral("api_version")).toInt() != PluginApi::API_VERSION
            || params.value(QStringLiteral("plugin_name")).toString() != m_Plugin.get_name()) {
            RespondError(id, -32600, QStringLiteral("Invalid session handshake"));
            if (m_Socket) {
                m_Socket->disconnectFromServer();
            }
            return;
        }
        m_Authenticated = true;
        m_Token.clear();
        m_Server->close();
        SettingsStore settings;
        const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
        Respond(id, QJsonObject {
            { QStringLiteral("session_id"), m_SessionId.toString(QUuid::WithoutBraces) },
            { QStringLiteral("protocol_version"), PluginApi::PROTOCOL_VERSION },
            { QStringLiteral("api_version"), PluginApi::API_VERSION },
            { QStringLiteral("position_encoding"), QStringLiteral("utf-16") },
            { QStringLiteral("max_message_size"), static_cast<qint64>(PluginApi::DEFAULT_MAX_MESSAGE_SIZE) },
            { QStringLiteral("permissions"), QJsonArray::fromStringList(m_Permissions) },
            { QStringLiteral("book_revision"), static_cast<qint64>(m_BookRevision) },
            { QStringLiteral("ui"), QJsonObject {
                { QStringLiteral("language"), settings.uiLanguage() },
                { QStringLiteral("color_mode"), dark ? QStringLiteral("dark") : QStringLiteral("light") }
            } }
        });
        if (m_Console) {
            m_Console->SetStatus(tr("Connected"));
        }
        return;
    }

    if (method == QStringLiteral("session.ping")) {
        Respond(id, QJsonObject {{ QStringLiteral("pong"), true }});
    } else if (method == QStringLiteral("session.getInfo")) {
        Respond(id, QJsonObject {
            { QStringLiteral("session_id"), m_SessionId.toString(QUuid::WithoutBraces) },
            { QStringLiteral("plugin_name"), m_Plugin.get_name() },
            { QStringLiteral("permissions"), QJsonArray::fromStringList(m_Permissions) }
        });
    } else if (method == QStringLiteral("session.finish")) {
        Respond(id, QJsonObject {{ QStringLiteral("accepted"), true }});
        Finish(params.value(QStringLiteral("status")).toString(QStringLiteral("success")),
               params.value(QStringLiteral("message")).toString());
    } else if (method == QStringLiteral("book.getRevision")) {
        if (RequirePermission(QStringLiteral("book.read"), id)) {
            Respond(id, QJsonObject {{ QStringLiteral("revision"), static_cast<qint64>(m_BookRevision) }});
        }
    } else if (method == QStringLiteral("book.getInfo")) {
        if (RequirePermission(QStringLiteral("book.read"), id)) {
            QSharedPointer<Book> book = m_MainWindow->GetCurrentBook();
            Respond(id, QJsonObject {
                { QStringLiteral("epub_version"), book->GetConstOPF()->GetEpubVersion() },
                { QStringLiteral("modified"), book->IsModified() },
                { QStringLiteral("file_path"), m_MainWindow->GetCurrentFilePath() },
                { QStringLiteral("revision"), static_cast<qint64>(m_BookRevision) }
            });
        }
    } else if (method == QStringLiteral("resource.list")) {
        if (!RequirePermission(QStringLiteral("book.read"), id)) return;
        QStringList types;
        for (const QJsonValue &type : params.value(QStringLiteral("types")).toArray()) {
            if (type.isString()) {
                types.append(type.toString());
            }
        }
        const int page_size = qBound(1, params.value(QStringLiteral("page_size")).toInt(200), 500);
        const int offset = qMax(0, params.value(QStringLiteral("cursor")).toString().toInt());
        QList<Resource *> resources = m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceList();
        std::sort(resources.begin(), resources.end(), [](Resource *left, Resource *right) {
            return left->GetRelativePath() < right->GetRelativePath();
        });
        QJsonArray items;
        int matched = 0;
        int next_offset = -1;
        for (Resource *resource : resources) {
            if (!types.isEmpty() && !types.contains(ResourceTypeName(resource->Type()))) continue;
            if (matched++ < offset) continue;
            if (items.size() >= page_size) {
                next_offset = matched - 1;
                break;
            }
            items.append(ResourceInfo(resource));
        }
        QJsonObject result {{ QStringLiteral("items"), items }};
        result.insert(QStringLiteral("next_cursor"), next_offset < 0
            ? QJsonValue() : QJsonValue(QString::number(next_offset)));
        Respond(id, result);
    } else if (method == QStringLiteral("resource.resolvePath")) {
        if (!RequirePermission(QStringLiteral("book.read"), id)) return;
        Resource *resource = m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceByBookPathNoThrow(
            params.value(QStringLiteral("book_path")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
        } else {
            Respond(id, ResourceInfo(resource));
        }
    } else if (method == QStringLiteral("resource.getInfo")) {
        if (!RequirePermission(QStringLiteral("book.read"), id)) return;
        Resource *resource = ResolveResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Resource not found"));
        } else {
            Respond(id, ResourceInfo(resource));
        }
    } else if (method == QStringLiteral("resource.readText")) {
        if (!RequirePermission(QStringLiteral("book.read"), id)) return;
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
        } else {
            resource->InitialLoad();
            Respond(id, QJsonObject {
                { QStringLiteral("text"), resource->GetText() },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
    } else if (method == QStringLiteral("resource.readMany")) {
        if (!RequirePermission(QStringLiteral("book.read"), id)) return;
        const QJsonArray ids = params.value(QStringLiteral("resource_ids")).toArray();
        if (ids.size() > 100) {
            RespondError(id, PluginApi::PayloadTooLarge, QStringLiteral("At most 100 resources may be read"));
            return;
        }
        QJsonArray items;
        for (const QJsonValue &value : ids) {
            TextResource *resource = ResolveTextResource(value.toString());
            if (!resource) continue;
            resource->InitialLoad();
            items.append(QJsonObject {
                { QStringLiteral("resource_id"), resource->GetIdentifier() },
                { QStringLiteral("text"), resource->GetText() },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
        Respond(id, QJsonObject {{ QStringLiteral("items"), items }});
    } else if (method == QStringLiteral("transaction.begin")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        if (m_Transaction) {
            RespondError(id, PluginApi::Busy, QStringLiteral("A transaction is already active"));
            return;
        }
        const QString visibility = params.value(QStringLiteral("visibility"))
            .toString(QStringLiteral("staged"));
        const QString checkpoint = params.value(QStringLiteral("checkpoint"))
            .toString(QStringLiteral("auto"));
        if (visibility != QStringLiteral("staged")
            || (checkpoint != QStringLiteral("none")
                && checkpoint != QStringLiteral("auto")
                && checkpoint != QStringLiteral("required"))) {
            RespondError(id, -32602, QStringLiteral("Invalid transaction policy"));
            return;
        }
        const QString transaction_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_Transaction = std::make_unique<PluginApi::TextTransaction>(
            transaction_id,
            params.value(QStringLiteral("label")).toString(QStringLiteral("Plugin changes")),
            checkpoint, m_BookRevision);
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("base_book_revision"), static_cast<qint64>(m_BookRevision) },
            { QStringLiteral("visibility"), QStringLiteral("staged") },
            { QStringLiteral("checkpoint"), checkpoint }
        });
    } else if (method == QStringLiteral("transaction.readText")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
            return;
        }
        resource->InitialLoad();
        quint64 revision = 0;
        const QString text = transaction->ReadText(resource->GetIdentifier(), resource->GetText(),
                                                   Revision(resource), &revision);
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("text"), text },
            { QStringLiteral("revision"), static_cast<qint64>(revision) },
            { QStringLiteral("staged"), transaction->HasChange(resource->GetIdentifier()) }
        });
    } else if (method == QStringLiteral("transaction.replaceText")
               || method == QStringLiteral("transaction.applyTextEdits")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        TextResource *resource = ResolveTextResource(params.value(QStringLiteral("resource_id")).toString());
        if (!resource) {
            RespondError(id, PluginApi::ResourceNotFound, QStringLiteral("Text resource not found"));
            return;
        }
        if (resource->Type() == Resource::OPFResourceType) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("OPF changes require the structure transaction API"));
            return;
        }
        quint64 expected_revision = 0;
        if (!ReadRevision(params, QStringLiteral("expected_revision"), &expected_revision)) {
            RespondError(id, -32602, QStringLiteral("expected_revision must be a non-negative integer"));
            return;
        }
        if (method == QStringLiteral("transaction.replaceText")
            && !params.value(QStringLiteral("text")).isString()) {
            RespondError(id, -32602, QStringLiteral("Text must be a string"));
            return;
        }
        resource->InitialLoad();
        quint64 required_revision = 0;
        transaction->ReadText(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                              &required_revision);
        if (expected_revision != required_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), static_cast<qint64>(expected_revision) },
                             { QStringLiteral("actual"), static_cast<qint64>(required_revision) },
                             { QStringLiteral("resource_id"), resource->GetIdentifier() }
                         });
            return;
        }
        QString stage_error;
        const bool staged = method == QStringLiteral("transaction.replaceText")
            ? transaction->ReplaceText(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                                       expected_revision, params.value(QStringLiteral("text")).toString(),
                                       &stage_error)
            : transaction->ApplyEdits(resource->GetIdentifier(), resource->GetText(), Revision(resource),
                                      expected_revision, params.value(QStringLiteral("edits")).toArray(),
                                      &stage_error);
        if (!staged) {
            RespondError(id, PluginApi::InvalidPatch, stage_error);
            return;
        }
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), resource->GetIdentifier() },
            { QStringLiteral("base_revision"), static_cast<qint64>(required_revision) },
            { QStringLiteral("staged"), true }
        });
    } else if (method == QStringLiteral("transaction.validate")
               || method == QStringLiteral("transaction.preview")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        QJsonArray conflicts;
        QJsonArray text_changes;
        int modified = 0;
        for (const PluginApi::StagedTextChange &change : transaction->Changes()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalText != change.stagedText) {
                ++modified;
                text_changes.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("before_length"), change.originalText.size() },
                    { QStringLiteral("after_length"), change.stagedText.size() }
                });
            }
        }
        QJsonObject result {
            { QStringLiteral("transaction_id"), transaction->Id() },
            { QStringLiteral("valid"), conflicts.isEmpty() },
            { QStringLiteral("conflicts"), conflicts },
            { QStringLiteral("summary"), QJsonObject {
                { QStringLiteral("modified"), modified },
                { QStringLiteral("added"), 0 },
                { QStringLiteral("deleted"), 0 },
                { QStringLiteral("renamed"), 0 }
            } }
        };
        if (method == QStringLiteral("transaction.preview")) {
            result.insert(QStringLiteral("text_changes"), text_changes);
            result.insert(QStringLiteral("binary_changes"), QJsonArray());
            result.insert(QStringLiteral("opf_changes"), QJsonArray());
            result.insert(QStringLiteral("warnings"), QJsonArray());
        }
        Respond(id, result);
    } else if (method == QStringLiteral("transaction.commit")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        QJsonArray conflicts;
        QList<PluginApi::StagedTextChange> dirty_changes;
        for (const PluginApi::StagedTextChange &change : transaction->Changes()) {
            Resource *resource = ResolveResource(change.resourceId);
            const quint64 actual = resource ? Revision(resource) : 0;
            if (!resource || actual != change.baseRevision) {
                conflicts.append(QJsonObject {
                    { QStringLiteral("resource_id"), change.resourceId },
                    { QStringLiteral("expected"), static_cast<qint64>(change.baseRevision) },
                    { QStringLiteral("actual"), resource ? QJsonValue(static_cast<qint64>(actual)) : QJsonValue() }
                });
            }
            if (change.originalText != change.stagedText) dirty_changes.append(change);
        }
        if (!conflicts.isEmpty()) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Transaction has conflicts"),
                         QJsonObject {{ QStringLiteral("conflicts"), conflicts }});
            return;
        }
        bool safety_checkpoint_required = dirty_changes.size() > 1;
        for (const PluginApi::StagedTextChange &change : dirty_changes) {
            Resource *resource = ResolveResource(change.resourceId);
            safety_checkpoint_required = safety_checkpoint_required || (resource &&
                (resource->Type() == Resource::OPFResourceType
                 || resource->Type() == Resource::NCXResourceType));
        }
        if (transaction->CheckpointPolicy() == QStringLiteral("none")
            && safety_checkpoint_required) {
            RespondError(id, PluginApi::TransactionRequired,
                         QStringLiteral("This transaction requires a checkpoint"));
            return;
        }
        const bool checkpoint_required = transaction->CheckpointPolicy() == QStringLiteral("required")
            || (transaction->CheckpointPolicy() == QStringLiteral("auto")
                && safety_checkpoint_required);
        if (checkpoint_required && !m_MainWindow->RepoCommit()) {
            RespondError(id, PluginApi::ValidationFailed,
                         QStringLiteral("Could not create the required checkpoint"));
            return;
        }
        QJsonArray committed;
        for (const PluginApi::StagedTextChange &change : dirty_changes) {
            TextResource *resource = ResolveTextResource(change.resourceId);
            resource->SetText(change.stagedText);
            committed.append(QJsonObject {
                { QStringLiteral("resource_id"), change.resourceId },
                { QStringLiteral("revision"), static_cast<qint64>(Revision(resource)) }
            });
        }
        if (!dirty_changes.isEmpty()) m_MainWindow->GetCurrentBook()->SetModified();
        const QString transaction_id = transaction->Id();
        m_Transaction.reset();
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("committed"), committed },
            { QStringLiteral("modified"), dirty_changes.size() },
            { QStringLiteral("checkpoint_created"), checkpoint_required }
        });
    } else if (method == QStringLiteral("transaction.rollback")) {
        if (!RequirePermission(QStringLiteral("book.write.text"), id)) return;
        PluginApi::TextTransaction *transaction = RequireTransaction(params, id);
        if (!transaction) return;
        const QString transaction_id = transaction->Id();
        const int discarded = transaction->Size();
        m_Transaction.reset();
        Respond(id, QJsonObject {
            { QStringLiteral("transaction_id"), transaction_id },
            { QStringLiteral("rolled_back"), true },
            { QStringLiteral("discarded"), discarded }
        });
    } else if (method == QStringLiteral("editor.getState")
               || method == QStringLiteral("editor.getSelection")) {
        if (RequirePermission(QStringLiteral("editor.read"), id)) {
            Respond(id, EditorState());
        }
    } else if (method == QStringLiteral("editor.getOpenTabs")) {
        if (!RequirePermission(QStringLiteral("editor.read"), id)) return;
        QJsonArray tabs;
        for (ContentTab *tab : m_TabManager->GetContentTabs()) {
            tabs.append(ResourceInfo(tab->GetLoadedResource()));
        }
        Respond(id, QJsonObject {{ QStringLiteral("items"), tabs }});
    } else if (method == QStringLiteral("editor.applyEdits")
               || method == QStringLiteral("editor.replaceSelection")
               || method == QStringLiteral("editor.insertText")) {
        if (!RequirePermission(QStringLiteral("editor.write"), id)) return;
        ContentTab *tab = m_TabManager->GetCurrentContentTab();
        Resource *active_resource = tab ? tab->GetLoadedResource() : nullptr;
        const QString requested_id = params.value(QStringLiteral("resource_id")).toString();
        if (!active_resource || (!requested_id.isEmpty()
                                 && requested_id != active_resource->GetIdentifier())) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Editor writes require the active resource"));
            return;
        }
        auto *text_resource = qobject_cast<TextResource *>(active_resource);
        if (!text_resource) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("The active resource is not editable text"));
            return;
        }
        const QJsonValue expected_value = params.value(QStringLiteral("expected_revision"));
        const double expected_number = expected_value.toDouble(-1);
        const quint64 actual_revision = Revision(text_resource);
        if (!expected_value.isDouble() || expected_number < 0
            || std::floor(expected_number) != expected_number
            || expected_number > static_cast<double>(std::numeric_limits<qint64>::max())
            || static_cast<quint64>(expected_number) != actual_revision) {
            RespondError(id, PluginApi::RevisionConflict, QStringLiteral("Revision conflict"),
                         QJsonObject {
                             { QStringLiteral("expected"), expected_value },
                             { QStringLiteral("actual"), static_cast<qint64>(actual_revision) },
                             { QStringLiteral("resource_id"), active_resource->GetIdentifier() }
                         });
            return;
        }

        QJsonArray edit_values;
        if (method == QStringLiteral("editor.applyEdits")) {
            edit_values = params.value(QStringLiteral("edits")).toArray();
        } else {
            if (!params.value(QStringLiteral("text")).isString()) {
                RespondError(id, -32602, QStringLiteral("Text must be a string"));
                return;
            }
            const int position = method == QStringLiteral("editor.insertText")
                ? tab->GetCursorPosition() : tab->GetSelectionStart();
            const int end = method == QStringLiteral("editor.insertText")
                ? position : tab->GetSelectionEnd();
            edit_values.append(QJsonObject {
                { QStringLiteral("start"), position },
                { QStringLiteral("end"), end },
                { QStringLiteral("text"), params.value(QStringLiteral("text")) }
            });
        }

        text_resource->InitialLoad();
        const QString current_text = text_resource->GetText();
        QList<PluginApi::TextEdit> edits;
        QString patch_error;
        if (!PluginApi::ParseTextEdits(edit_values, current_text, &edits, &patch_error)) {
            RespondError(id, PluginApi::InvalidPatch, patch_error);
            return;
        }
        {
            QWriteLocker locker(&text_resource->GetLock());
            PluginApi::ApplyTextEdits(&text_resource->GetTextDocumentForWriting(), edits);
        }
        m_MainWindow->GetCurrentBook()->SetModified();
        Respond(id, QJsonObject {
            { QStringLiteral("resource_id"), active_resource->GetIdentifier() },
            { QStringLiteral("revision"), static_cast<qint64>(Revision(active_resource)) },
            { QStringLiteral("applied_edits"), edits.size() }
        });
    } else if (method == QStringLiteral("editor.setCursor")
               || method == QStringLiteral("editor.setSelection")) {
        if (!RequirePermission(QStringLiteral("editor.write"), id)) return;
        ContentTab *tab = m_TabManager->GetCurrentContentTab();
        Resource *active_resource = tab ? tab->GetLoadedResource() : nullptr;
        const QString requested_id = params.value(QStringLiteral("resource_id")).toString();
        if (!active_resource || (!requested_id.isEmpty()
                                 && requested_id != active_resource->GetIdentifier())) {
            RespondError(id, PluginApi::UnsupportedOperation,
                         QStringLiteral("Editor navigation requires the active resource"));
            return;
        }
        int start = 0;
        int end = 0;
        const bool positions_valid = method == QStringLiteral("editor.setCursor")
            ? ReadPosition(params, QStringLiteral("position"), &start)
            : ReadPosition(params, QStringLiteral("start"), &start)
                && ReadPosition(params, QStringLiteral("end"), &end);
        if (method == QStringLiteral("editor.setCursor")) end = start;
        if (!positions_valid || !tab->SetSelectionRange(start, end)) {
            RespondError(id, PluginApi::InvalidPatch, QStringLiteral("Selection range is invalid"));
            return;
        }
        Respond(id, EditorState());
    } else {
        RespondError(id, -32601, QStringLiteral("Method not found"));
    }
}

void PluginSession::Respond(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isUndefined() || !m_Socket) return;
    QByteArray frame = PluginApi::EncodeFrame(PluginApi::MakeResult(id, result));
    if (frame.size() - static_cast<qsizetype>(sizeof(quint32))
        > PluginApi::DEFAULT_MAX_MESSAGE_SIZE) {
        frame = PluginApi::EncodeFrame(PluginApi::MakeError(
            id, PluginApi::PayloadTooLarge, QStringLiteral("Response exceeds the message limit")));
    }
    m_Socket->write(frame);
    m_Socket->flush();
}

void PluginSession::RespondError(const QJsonValue &id, int code, const QString &message,
                                 const QJsonValue &data)
{
    if (!m_Socket) return;
    m_Socket->write(PluginApi::EncodeFrame(PluginApi::MakeError(id, code, message, data)));
    m_Socket->flush();
}

bool PluginSession::RequirePermission(const QString &permission, const QJsonValue &id)
{
    if (m_Permissions.contains(permission)) return true;
    RespondError(id, PluginApi::PermissionDenied, QStringLiteral("Permission denied"),
                 QJsonObject {{ QStringLiteral("permission"), permission }});
    return false;
}

QJsonObject PluginSession::ResourceInfo(Resource *resource) const
{
    auto *text = qobject_cast<TextResource *>(resource);
    return QJsonObject {
        { QStringLiteral("resource_id"), resource->GetIdentifier() },
        { QStringLiteral("book_path"), resource->GetRelativePath() },
        { QStringLiteral("media_type"), resource->GetMediaType() },
        { QStringLiteral("resource_type"), ResourceTypeName(resource->Type()) },
        { QStringLiteral("content_revision"), static_cast<qint64>(Revision(resource)) },
        { QStringLiteral("loaded"), text ? text->IsLoaded() : true }
    };
}

Resource *PluginSession::ResolveResource(const QString &resource_id) const
{
    return m_MainWindow->GetCurrentBook()->GetFolderKeeper()->GetResourceByIdentifier(resource_id);
}

TextResource *PluginSession::ResolveTextResource(const QString &resource_id) const
{
    return qobject_cast<TextResource *>(ResolveResource(resource_id));
}

PluginApi::TextTransaction *PluginSession::RequireTransaction(const QJsonObject &params,
                                                              const QJsonValue &request_id)
{
    const QString transaction_id = params.value(QStringLiteral("transaction_id")).toString();
    if (!m_Transaction || transaction_id.isEmpty()
        || transaction_id != m_Transaction->Id()) {
        RespondError(request_id, PluginApi::TransactionNotFound,
                     QStringLiteral("Transaction not found"));
        return nullptr;
    }
    return m_Transaction.get();
}

quint64 PluginSession::Revision(Resource *resource) const
{
    return m_ResourceRevisions.value(resource->GetIdentifier(), 1);
}

QJsonObject PluginSession::EditorState() const
{
    ContentTab *tab = m_TabManager->GetCurrentContentTab();
    if (!tab || !tab->GetLoadedResource()) {
        return QJsonObject {{ QStringLiteral("active"), false }};
    }
    Searchable *searchable = tab->GetSearchableContent();
    const int cursor = tab->GetCursorPosition();
    const int selection_start = tab->GetSelectionStart();
    const int selection_end = tab->GetSelectionEnd();
    return QJsonObject {
        { QStringLiteral("active"), true },
        { QStringLiteral("resource_id"), tab->GetLoadedResource()->GetIdentifier() },
        { QStringLiteral("book_path"), tab->GetLoadedResource()->GetRelativePath() },
        { QStringLiteral("revision"), static_cast<qint64>(Revision(tab->GetLoadedResource())) },
        { QStringLiteral("cursor"), cursor },
        { QStringLiteral("selection"), QJsonObject {
            { QStringLiteral("start"), selection_start },
            { QStringLiteral("end"), selection_end },
            { QStringLiteral("text"), searchable ? searchable->GetSelectedText() : QString() }
        } },
        { QStringLiteral("position_encoding"), QStringLiteral("utf-16") }
    };
}

QString PluginSession::ResolveInterpreter() const
{
    SettingsStore settings;
    const QString bundled = PluginDB::buildBundledInterpPath();
    if (settings.useBundledInterp() && !bundled.isEmpty()) {
        return bundled;
    }
    return PluginDB::instance()->get_engine_path(QStringLiteral("python3.4"));
}

QStringList PluginSession::EffectivePermissions() const
{
    QStringList permissions = m_Plugin.get_permissions();
    if (permissions.isEmpty()) {
        permissions << QStringLiteral("book.read") << QStringLiteral("editor.read");
        if (m_Plugin.get_type() == QStringLiteral("edit")) {
            permissions << QStringLiteral("book.write.text") << QStringLiteral("book.write.binary")
                        << QStringLiteral("book.structure") << QStringLiteral("editor.write");
        }
    }
    return permissions;
}

void PluginSession::Finish(const QString &status, const QString &message)
{
    m_Ending = true;
    if (!message.isEmpty() && m_Console) {
        m_Console->AppendOutput(message);
    }
    if (m_Console) {
        m_Console->SetStatus(tr("Status: %1").arg(status));
        m_Console->SetFinished();
    }
    CleanServer();
    if (!m_EndSignalScheduled) {
        m_EndSignalScheduled = true;
        QTimer::singleShot(0, this, &PluginSession::Ended);
    }
}

void PluginSession::CleanServer()
{
    if (m_Socket) {
        m_Socket->disconnect(this);
        m_Socket->disconnectFromServer();
        m_Socket->deleteLater();
        m_Socket = nullptr;
    }
    if (m_Server) {
        m_Server->close();
        m_Server->deleteLater();
        m_Server = nullptr;
    }
    if (!m_ServerName.isEmpty()) {
        QLocalServer::removeServer(m_ServerName);
    }
}
