//
//  SAMCPServer.h
//  Sequel Ace (Linux port)
//
//  A local-only Model Context Protocol server (SPMCPServer.swift on macOS):
//  a loopback-bound HTTP server speaking both MCP transports — Streamable
//  HTTP (POST /mcp) and the legacy SSE transport (GET /sse + POST /message)
//  — exposing the 19 read/introspection/execution tools declared in
//  SAMCPToolDefinitions against whichever connection tabs SAMCPDataSource
//  reports open.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>

class QTcpServer;
class QTcpSocket;
class SAMCPDataSource;
class SADatabaseSession;

class SAMCPServer : public QObject {
    Q_OBJECT
public:
    // `dataSource` is not owned; it must outlive the server.
    explicit SAMCPServer(SAMCPDataSource *dataSource, QObject *parent = nullptr);
    ~SAMCPServer() override;

    // Binds QHostAddress::LocalHost:port. Returns false and fills `error` on failure
    // (e.g. the port is already in use).
    bool start(quint16 port, QString *error = nullptr);
    void stop();
    bool isRunning() const;
    quint16 port() const;

    // When true, tools without the readOnlyHint annotation are refused, and
    // any tool that carries a "sql" argument has that SQL (after `?` binding)
    // additionally checked by SAMCPReadOnlyGuard::checkReadOnly.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool readOnly() const { return m_readOnly; }

    // Destination folder for export_results; relative "path" arguments are
    // resolved (and confined) inside it.
    void setExportPath(const QString &path) { m_exportPath = path; }

Q_SIGNALS:
    void started(quint16 port);
    void stopped();
    void errorOccurred(const QString &message);

private Q_SLOTS:
    void handleNewConnection();
    void handleReadyRead();
    void handleSocketDisconnected();

private:
    struct ConnectionState {
        QByteArray buffer;
        bool headersParsed = false;
        QString method;
        QString path;
        QString query;
        QHash<QString, QString> headers;
        int headerEnd = -1;
        qint64 contentLength = 0;
        bool isSseStream = false;
        // Set when the peer went away. A request already on the stack keeps
        // running but its response is dropped instead of written to a socket
        // that is being deleted.
        bool closed = false;
    };
    // Held by shared_ptr so a request that is being served survives its
    // connection being removed from m_connections (and the hash rehashing on
    // a new connection) while a tool call runs.
    using ConnectionStatePtr = std::shared_ptr<ConnectionState>;

    // Serves complete buffered requests, one at a time and never re-entrantly:
    // a tool call spins a nested event loop, and whatever arrives while it is
    // on the stack is only buffered, then picked up here when it returns.
    void processPendingRequests();
    // Consumes at most one complete request from `state->buffer`; returns true
    // when one was served, so the caller retries for a pipelined next request.
    bool processBuffer(const QPointer<QTcpSocket> &socket, const ConnectionStatePtr &state);
    bool tryParseHeaders(ConnectionState &state);
    bool originIsLoopback(const ConnectionState &state, QTcpSocket *socket) const;
    void dispatch(const QPointer<QTcpSocket> &socket, ConnectionState &state, const QByteArray &body);
    // True while `socket` is alive and still registered; false once the peer
    // disconnected, which may have happened at any point inside a tool call.
    bool socketIsLive(const QPointer<QTcpSocket> &socket) const;
    // Disposes of the sockets whose peer went away, but only once no request
    // is on the stack: a socket destroyed from inside the nested event loop
    // that is running under its own readyRead leaves Qt's socket notifier
    // working on freed memory.
    void releaseClosedSockets();

    void handleHealth(const QPointer<QTcpSocket> &socket);
    void handleMcpPost(const QPointer<QTcpSocket> &socket, const QByteArray &body);
    void handleSseGet(const QPointer<QTcpSocket> &socket, ConnectionState &state);
    void handleMessagePost(const QPointer<QTcpSocket> &socket, const ConnectionState &state, const QByteArray &body);

    void writeHttpResponse(const QPointer<QTcpSocket> &socket, int statusCode, const QByteArray &contentType,
                            const QByteArray &body, bool keepAlive = false);
    void writeHttpError(const QPointer<QTcpSocket> &socket, int statusCode, const QString &message);

    // Returns std::nullopt for a notification (no "id" — no response is sent).
    std::optional<QJsonObject> handleRpcRequest(const QJsonObject &request);
    QJsonObject handleInitialize(const QJsonObject &params);
    QJsonObject handleToolsList();
    QJsonObject handleToolsCall(const QJsonObject &params);
    QJsonObject handleResourcesList();
    QJsonObject handleResourcesRead(const QJsonObject &params);
    QJsonObject handlePromptsList();
    QJsonObject handlePromptsGet(const QJsonObject &params);
    QJsonObject handleCompletionComplete(const QJsonObject &params);

    // Tool dispatch. Returns the MCP tools/call "result" object
    // ({"content":[...], "isError": bool}).
    QJsonObject callTool(const QString &name, const QJsonObject &arguments);
    SADatabaseSession *resolveSession(const QJsonObject &arguments, QString *error) const;
    // Runs `sql` synchronously (nested QEventLoop bridging SADatabaseSession's
    // async callback) and returns its SAResult; `result.ok` is false and
    // `result.errorMessage` set both for a guard rejection and for a real
    // server error.
    SAResult runSql(SADatabaseSession *session, const QString &sql, bool userQuery) const;

    QTcpServer *m_server = nullptr;
    SAMCPDataSource *m_dataSource = nullptr;
    QHash<QTcpSocket *, ConnectionStatePtr> m_connections;
    QHash<QString, QPointer<QTcpSocket>> m_sseSessions;   // sessionId -> the open GET /sse socket
    QList<QPointer<QTcpSocket>> m_closedSockets;
    bool m_processing = false;
    bool m_readOnly = false;
    QString m_exportPath;
};
