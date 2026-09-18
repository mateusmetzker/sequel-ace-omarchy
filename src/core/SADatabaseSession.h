//
//  SADatabaseSession.h
//  Sequel Ace (Linux port)
//
//  Owns one server connection on a dedicated worker thread and offers an
//  asynchronous, main-thread-friendly API: every call returns immediately and
//  the handler runs on the main thread when the statement completes. Handles
//  SSH tunnelling, keep-alive pings, transparent reconnection after "server
//  has gone away", query cancellation through KILL QUERY, and console logging.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAConnectionInfo.h"
#include "SAMySQLConnection.h"
#include "SAResult.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <functional>

class SASSHTunnel;
class SASessionWorker;

struct SAServerInfo {
    QString versionString;
    unsigned long versionNumber = 0;
    bool isMariaDB = false;
    QString encoding;
    bool ssl = false;
    QString sslCipher;
    unsigned long threadId = 0;
    quint64 maxAllowedPacket = 0;
    QHash<QString, QString> variables;
    bool serverVersionIsGreaterThanOrEqualTo(unsigned int major, unsigned int minor, unsigned int release) const
    { return versionNumber >= (major * 10000UL + minor * 100UL + release); }
};

class SADatabaseSession : public QObject {
    Q_OBJECT
public:
    enum QueryFlag {
        NoFlags = 0,
        Silent = 1,        // do not report to the console
        NoRetry = 2,       // do not transparently reconnect and re-run on connection loss
        UserQuery = 4,     // custom query: never rewrite, never retry
    };
    Q_DECLARE_FLAGS(QueryFlags, QueryFlag)

    using ResultHandler = std::function<void(const SAResult &)>;
    using BatchHandler = std::function<void(const QVector<SAResult> &)>;
    using ConnectHandler = std::function<void(bool ok, const QString &error)>;

    explicit SADatabaseSession(QObject *parent = nullptr);
    ~SADatabaseSession() override;

    void setConnectionInfo(const SAConnectionInfo &info);
    const SAConnectionInfo &connectionInfo() const { return m_info; }
    QString connectionName() const { return m_info.displayName(); }

    void connectToServer(ConnectHandler done);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }
    bool isBusy() const { return m_pending > 0; }
    const SAServerInfo &serverInfo() const { return m_serverInfo; }
    QString currentDatabase() const { return m_database; }
    SASSHTunnel *sshTunnel() const { return m_tunnel; }

    // Asynchronous statement execution.
    void query(const QString &sql, ResultHandler handler, QueryFlags flags = NoFlags);
    void queryBatch(const QStringList &statements, BatchHandler handler, QueryFlags flags = NoFlags, bool stopOnError = false);
    void selectDatabase(const QString &database, ConnectHandler done);
    void setEncoding(const QString &encoding, ConnectHandler done);
    void cancelCurrentQuery();
    // Re-reads SELECT DATABASE() after user statements such as USE.
    void refreshCurrentDatabase(ConnectHandler done = nullptr);

    // Installed on the SSH tunnel when one is created (see SASSHTunnel).
    void setSSHPromptHandlers(std::function<bool(const QString &)> question,
                              std::function<QString(const QString &, bool *)> passphrase)
    { m_sshQuestion = std::move(question); m_sshPassphrase = std::move(passphrase); }

    // Escaping without touching the worker's handle (utf8mb4 rules).
    static QString escapeString(const QString &value, bool includingQuotes = true);
    static QString quoteIdentifier(const QString &identifier) { return SAMySQLConnection::quoteIdentifier(identifier); }

Q_SIGNALS:
    void connected();
    void disconnected(const QString &reason);
    void databaseChanged(const QString &database);
    void busyChanged(bool busy);
    // Emitted for every statement the session ran (unless Silent).
    void queryPerformed(const QString &sql, double seconds, bool isError, const QString &errorMessage, const QString &database);
    void sshTunnelStateChanged();
    void connectionLostAndRestored();

private:
    void startWorker();
    void stopWorker();
    void beginTask();
    void endTask();
    void connectMySQL(const SAMySQLOptions &options, ConnectHandler done);
    void handleTunnelStateChange();

    SAConnectionInfo m_info;
    SAMySQLOptions m_effectiveOptions;
    SASSHTunnel *m_tunnel = nullptr;
    ConnectHandler m_pendingConnectHandler;
    QThread *m_thread = nullptr;
    SASessionWorker *m_worker = nullptr;
    SAServerInfo m_serverInfo;
    QString m_database;
    bool m_connected = false;
    int m_pending = 0;
    unsigned long m_threadIdForKill = 0;
    std::function<bool(const QString &)> m_sshQuestion;
    std::function<QString(const QString &, bool *)> m_sshPassphrase;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SADatabaseSession::QueryFlags)

// Lives on the worker thread. Public only so SADatabaseSession can invoke it.
class SASessionWorker : public QObject {
    Q_OBJECT
public:
    explicit SASessionWorker(QObject *parent = nullptr);
    ~SASessionWorker() override;

    SAMySQLConnection &connection() { return m_connection; }

public Q_SLOTS:
    void initialise();
    void startKeepAlive(int intervalSeconds);
    void stopKeepAlive();

private Q_SLOTS:
    void keepAliveTick();

private:
    SAMySQLConnection m_connection;
    class QTimer *m_keepAliveTimer = nullptr;
    qint64 m_lastActivityMs = 0;

    friend class SADatabaseSession;
};
