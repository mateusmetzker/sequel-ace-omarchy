//
//  SADatabaseSession.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SADatabaseSession.h"
#include "SAPreferences.h"
#include "SASSHTunnel.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QTimer>

#include <errmsg.h>
#include <mysqld_error.h>

// ---- worker ------------------------------------------------------------------

SASessionWorker::SASessionWorker(QObject *parent)
    : QObject(parent)
{
}

SASessionWorker::~SASessionWorker()
{
    stopKeepAlive();
    m_connection.disconnect();
    SAMySQLConnection::finaliseThread();
}

void SASessionWorker::initialise()
{
    SAMySQLConnection::initialiseThread();
}

void SASessionWorker::startKeepAlive(int intervalSeconds)
{
    if (!m_keepAliveTimer) {
        m_keepAliveTimer = new QTimer(this);
        connect(m_keepAliveTimer, &QTimer::timeout, this, &SASessionWorker::keepAliveTick);
    }
    m_keepAliveTimer->start(qMax(5, intervalSeconds) * 1000);
}

void SASessionWorker::stopKeepAlive()
{
    if (m_keepAliveTimer) m_keepAliveTimer->stop();
}

void SASessionWorker::keepAliveTick()
{
    if (!m_connection.isConnected()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_keepAliveTimer && now - m_lastActivityMs < m_keepAliveTimer->interval()) return;
    m_connection.ping();
    m_lastActivityMs = now;
}

// ---- session -----------------------------------------------------------------

SADatabaseSession::SADatabaseSession(QObject *parent)
    : QObject(parent)
{
    startWorker();
}

SADatabaseSession::~SADatabaseSession()
{
    stopWorker();
    delete m_tunnel;
}

void SADatabaseSession::startWorker()
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("SADatabaseSession worker"));
    m_worker = new SASessionWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, &SASessionWorker::initialise, Qt::QueuedConnection);
}

void SADatabaseSession::stopWorker()
{
    if (!m_thread) return;
    m_thread->quit();
    if (!m_thread->wait(5000)) {
        m_thread->terminate();
        m_thread->wait();
    }
    m_worker = nullptr;
    m_thread = nullptr;
}

void SADatabaseSession::setConnectionInfo(const SAConnectionInfo &info)
{
    m_info = info;
}

void SADatabaseSession::beginTask()
{
    if (m_pending++ == 0) Q_EMIT busyChanged(true);
}

void SADatabaseSession::endTask()
{
    if (m_pending > 0 && --m_pending == 0) Q_EMIT busyChanged(false);
}

QString SADatabaseSession::escapeString(const QString &value, bool includingQuotes)
{
    SAMySQLConnection unconnected;
    return unconnected.escapeString(value, includingQuotes);
}

// ---- connecting --------------------------------------------------------------

void SADatabaseSession::connectToServer(ConnectHandler done)
{
    QString problem;
    if (!m_info.isValidForConnecting(&problem)) {
        if (done) done(false, problem);
        return;
    }
    SAMySQLOptions options = m_info.toMySQLOptions();
    SAPreferences &prefs = SAPreferences::instance();
    options.connectTimeout = static_cast<unsigned int>(qMax(1, prefs.intFor(SAPreferences::ConnectionTimeoutValue)));
    options.encoding = prefs.stringFor(SAPreferences::DefaultEncoding);

    if (m_info.type == SAConnectionType::SSHTunnel) {
        delete m_tunnel;
        m_tunnel = new SASSHTunnel(m_info, this);
        if (m_sshQuestion) m_tunnel->setQuestionHandler(m_sshQuestion);
        if (m_sshPassphrase) m_tunnel->setPassphraseHandler(m_sshPassphrase);
        connect(m_tunnel, &SASSHTunnel::stateChanged, this, &SADatabaseSession::handleTunnelStateChange);
        m_pendingConnectHandler = std::move(done);
        m_effectiveOptions = options;
        beginTask();
        Q_EMIT sshTunnelStateChanged();
        m_tunnel->connectTunnel();
        return;
    }
    connectMySQL(options, std::move(done));
}

void SADatabaseSession::handleTunnelStateChange()
{
    Q_EMIT sshTunnelStateChanged();
    if (!m_tunnel) return;
    switch (m_tunnel->state()) {
    case SASSHTunnel::Connected: {
        if (!m_pendingConnectHandler) return;   // reconnect of an established tunnel
        SAMySQLOptions options = m_effectiveOptions;
        options.host = m_info.resolvedMySQLHost();
        options.port = m_tunnel->localPort();
        options.socketPath.clear();
        ConnectHandler done = std::move(m_pendingConnectHandler);
        m_pendingConnectHandler = nullptr;
        endTask();
        connectMySQL(options, std::move(done));
        break;
    }
    case SASSHTunnel::Failed:
    case SASSHTunnel::Idle: {
        if (m_pendingConnectHandler) {
            ConnectHandler done = std::move(m_pendingConnectHandler);
            m_pendingConnectHandler = nullptr;
            endTask();
            done(false, m_tunnel->lastError().isEmpty() ? tr("The SSH tunnel could not be established.") : m_tunnel->lastError());
        } else if (m_connected && m_tunnel->state() == SASSHTunnel::Idle) {
            // Tunnel dropped under an active connection.
            m_connected = false;
            Q_EMIT disconnected(m_tunnel->lastError().isEmpty() ? tr("The SSH tunnel has unexpectedly closed.") : m_tunnel->lastError());
        }
        break;
    }
    default:
        break;
    }
}

void SADatabaseSession::connectMySQL(const SAMySQLOptions &options, ConnectHandler done)
{
    m_effectiveOptions = options;
    beginTask();
    QPointer<SADatabaseSession> guard(this);
    SASessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, options, guard, done = std::move(done)]() {
        SAMySQLConnection &conn = worker->connection();
        conn.disconnect();
        conn.setOptions(options);
        const bool ok = conn.connect();
        QString error = ok ? QString() : conn.lastErrorMessage();
        QString tzWarning;
        SAServerInfo info;
        QString database;
        if (ok) {
            info.versionString = conn.serverVersionString();
            info.versionNumber = conn.serverVersionNumber();
            info.isMariaDB = conn.isMariaDB();
            info.encoding = conn.encoding();
            info.ssl = conn.isConnectedViaSSL();
            info.sslCipher = conn.sslCipher();
            info.threadId = conn.threadId();
            info.maxAllowedPacket = conn.maxAllowedPacket();
            info.variables = conn.serverVariables();
            database = conn.currentDatabase();
            if (!options.database.isEmpty() && database.isEmpty())
                error = conn.lastErrorMessage();   // database could not be selected; connection stays up
            worker->m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        }
        QMetaObject::invokeMethod(guard.isNull() ? nullptr : guard.data(), [guard, ok, error, info, database, done]() {
            if (guard.isNull()) return;
            SADatabaseSession *self = guard.data();
            self->endTask();
            self->m_connected = ok;
            self->m_serverInfo = info;
            self->m_database = database;
            self->m_threadIdForKill = info.threadId;
            if (ok) {
                SAPreferences &prefs = SAPreferences::instance();
                if (prefs.boolFor(SAPreferences::UseKeepAlive)) {
                    const int interval = prefs.intFor(SAPreferences::KeepAliveInterval);
                    QMetaObject::invokeMethod(self->m_worker, [w = self->m_worker, interval]() { w->startKeepAlive(interval); }, Qt::QueuedConnection);
                }
                Q_EMIT self->connected();
                if (!database.isEmpty()) Q_EMIT self->databaseChanged(database);
            }
            if (done) done(ok, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SADatabaseSession::disconnectFromServer()
{
    m_connected = false;
    if (m_worker) {
        SASessionWorker *worker = m_worker;
        QMetaObject::invokeMethod(worker, [worker]() {
            worker->stopKeepAlive();
            worker->connection().disconnect();
        }, Qt::QueuedConnection);
    }
    if (m_tunnel) {
        m_tunnel->disconnectTunnel();
    }
    m_database.clear();
    Q_EMIT disconnected(QString());
}

// ---- queries -----------------------------------------------------------------

void SADatabaseSession::query(const QString &sql, ResultHandler handler, QueryFlags flags)
{
    beginTask();
    QPointer<SADatabaseSession> guard(this);
    SASessionWorker *worker = m_worker;
    const QString database = m_database;
    QMetaObject::invokeMethod(worker, [worker, sql, guard, flags, handler = std::move(handler)]() {
        SAMySQLConnection &conn = worker->connection();
        worker->m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        bool restored = false;
        SAResult result = conn.query(sql);
        if (!result.ok && (result.errorNumber == CR_SERVER_GONE_ERROR || result.errorNumber == CR_SERVER_LOST)
            && !(flags & (NoRetry | UserQuery))) {
            if (conn.reconnect()) {
                restored = true;
                result = conn.query(sql);
            }
        } else if (!result.ok && (result.errorNumber == CR_SERVER_GONE_ERROR || result.errorNumber == CR_SERVER_LOST)
                   && (flags & UserQuery)) {
            // Restore the connection but let the user decide whether to re-run.
            if (conn.reconnect()) {
                restored = true;
                result.errorMessage += QStringLiteral("\n\n") + QObject::tr("(This usually indicates that the connection has been closed by the server after inactivity, but can also occur due to other conditions. The connection has been restored; please try again if the query is safe to re-run.)");
            }
        }
        if (!result.ok && result.errorNumber == ER_QUERY_INTERRUPTED) result.wasCancelled = true;
        const bool stillConnected = conn.isConnected();
        const QString currentDatabase = conn.currentDatabase();
        QMetaObject::invokeMethod(guard.isNull() ? nullptr : guard.data(), [guard, result, handler, flags, restored, stillConnected, currentDatabase]() {
            if (guard.isNull()) return;
            SADatabaseSession *self = guard.data();
            self->endTask();
            if (restored) Q_EMIT self->connectionLostAndRestored();
            if (!(flags & Silent)) {
                Q_EMIT self->queryPerformed(result.query, result.executionTime, !result.ok, result.errorMessage, currentDatabase);
            }
            if (!stillConnected && self->m_connected) {
                self->m_connected = false;
                Q_EMIT self->disconnected(result.errorMessage);
            }
            if (handler) handler(result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    Q_UNUSED(database);
}

void SADatabaseSession::queryBatch(const QStringList &statements, BatchHandler handler, QueryFlags flags, bool stopOnError)
{
    beginTask();
    QPointer<SADatabaseSession> guard(this);
    SASessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, statements, guard, flags, stopOnError, handler = std::move(handler)]() {
        SAMySQLConnection &conn = worker->connection();
        worker->m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        QVector<SAResult> results;
        results.reserve(statements.size());
        for (const QString &sql : statements) {
            SAResult r = conn.query(sql);
            if (!r.ok && (r.errorNumber == CR_SERVER_GONE_ERROR || r.errorNumber == CR_SERVER_LOST) && !(flags & (NoRetry | UserQuery))) {
                if (conn.reconnect()) r = conn.query(sql);
            }
            results.append(r);
            if (!r.ok && stopOnError) break;
        }
        const bool stillConnected = conn.isConnected();
        const QString currentDatabase = conn.currentDatabase();
        QMetaObject::invokeMethod(guard.isNull() ? nullptr : guard.data(), [guard, results, handler, flags, stillConnected, currentDatabase]() {
            if (guard.isNull()) return;
            SADatabaseSession *self = guard.data();
            self->endTask();
            if (!(flags & Silent)) {
                for (const SAResult &r : results)
                    Q_EMIT self->queryPerformed(r.query, r.executionTime, !r.ok, r.errorMessage, currentDatabase);
            }
            if (!stillConnected && self->m_connected) {
                self->m_connected = false;
                Q_EMIT self->disconnected(results.isEmpty() ? QString() : results.last().errorMessage);
            }
            if (handler) handler(results);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SADatabaseSession::selectDatabase(const QString &database, ConnectHandler done)
{
    beginTask();
    QPointer<SADatabaseSession> guard(this);
    SASessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, database, guard, done = std::move(done)]() {
        SAMySQLConnection &conn = worker->connection();
        bool ok = true;
        QString error;
        if (!database.isEmpty()) {
            ok = conn.selectDatabase(database);
            if (!ok) error = conn.lastErrorMessage();
        }
        const QString current = conn.currentDatabase();
        QMetaObject::invokeMethod(guard.isNull() ? nullptr : guard.data(), [guard, ok, error, current, database, done]() {
            if (guard.isNull()) return;
            SADatabaseSession *self = guard.data();
            self->endTask();
            if (ok) {
                self->m_database = database.isEmpty() ? current : database;
                Q_EMIT self->databaseChanged(self->m_database);
            }
            if (done) done(ok, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SADatabaseSession::setEncoding(const QString &encoding, ConnectHandler done)
{
    beginTask();
    QPointer<SADatabaseSession> guard(this);
    SASessionWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, encoding, guard, done = std::move(done)]() {
        SAMySQLConnection &conn = worker->connection();
        const bool ok = conn.setEncoding(encoding);
        const QString error = ok ? QString() : conn.lastErrorMessage();
        QMetaObject::invokeMethod(guard.isNull() ? nullptr : guard.data(), [guard, ok, error, encoding, done]() {
            if (guard.isNull()) return;
            SADatabaseSession *self = guard.data();
            self->endTask();
            if (ok) self->m_serverInfo.encoding = encoding;
            if (done) done(ok, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SADatabaseSession::cancelCurrentQuery()
{
    if (!m_connected || !m_threadIdForKill) return;
    // Runs on the calling (main) thread with a throw-away connection; the
    // worker's handle is never touched.
    QString error;
    if (!SAMySQLConnection::killQuery(m_effectiveOptions, m_threadIdForKill, &error))
        qWarning() << "Sequel Ace: KILL QUERY failed:" << error;
}

void SADatabaseSession::refreshCurrentDatabase(ConnectHandler done)
{
    QPointer<SADatabaseSession> guard(this);
    query(QStringLiteral("SELECT DATABASE()"), [guard, done](const SAResult &r) {
        if (guard.isNull()) return;
        if (r.ok) {
            const QString db = r.rows.isEmpty() || r.rows.first().first().isNull ? QString() : r.firstValue();
            if (db != guard->m_database) {
                guard->m_database = db;
                Q_EMIT guard->databaseChanged(db);
            }
        }
        if (done) done(r.ok, r.errorMessage);
    }, Silent);
}
