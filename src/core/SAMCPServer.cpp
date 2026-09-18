//
//  SAMCPServer.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAMCPServer.h"

#include "SAMCPDataSource.h"
#include "SAMCPReadOnlyGuard.h"
#include "SAMCPToolDefinitions.h"
#include "SADatabaseSession.h"
#include "SASchemaQueries.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace {

constexpr qint64 kMaxRequestBytes = 16 * 1024 * 1024;   // 16 MiB, matches the macOS server
constexpr int kMaxResultRows = 10000;

QString qi(const QString &identifier) { return SADatabaseSession::quoteIdentifier(identifier); }
QString qualified(const QString &database, const QString &name) { return qi(database) + QLatin1Char('.') + qi(name); }
QString sqlEscape(const QString &s) { return SADatabaseSession::escapeString(s); }

bool hostIsLoopback(const QString &hostPart)
{
    QString host = hostPart;
    // Strip a port suffix ("127.0.0.1:8765", "[::1]:8765").
    if (host.startsWith(QLatin1Char('['))) {
        const int close = host.indexOf(QLatin1Char(']'));
        if (close >= 0)
            host = host.mid(1, close - 1);
    } else {
        const int colon = host.lastIndexOf(QLatin1Char(':'));
        if (colon > 0)
            host = host.left(colon);
    }
    return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
        || host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("::1");
}

QJsonObject jsonRpcError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject error{{"code", code}, {"message", message}};
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", error}};
}

QJsonObject jsonRpcResult(const QJsonValue &id, const QJsonValue &result)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject toolTextResult(const QString &text, bool isError = false)
{
    QJsonArray content;
    content.append(QJsonObject{{"type", "text"}, {"text", text}});
    return QJsonObject{{"content", content}, {"isError", isError}};
}

QJsonObject toolErrorResult(const QString &message)
{
    return toolTextResult(message, true);
}

QJsonObject toolJsonResult(const QJsonObject &data)
{
    return toolTextResult(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
}

// Picks whichever field looks like the DDL text out of a SHOW CREATE ... row
// ("Create Table", "Create View", "Create Procedure", ...); the column
// holding it is not at a fixed index across object types.
QString ddlColumnValue(const SAResult &result)
{
    if (!result.ok || result.rowCount() == 0)
        return QString();
    for (int c = 0; c < result.fieldCount(); ++c) {
        if (result.fields[c].name.contains(QStringLiteral("Create"), Qt::CaseInsensitive))
            return result.stringAt(0, c);
    }
    return result.fieldCount() > 1 ? result.stringAt(0, 1) : result.stringAt(0, 0);
}

QJsonObject resultToJson(const SAResult &r)
{
    QJsonObject o;
    if (!r.ok) {
        o["error"] = r.errorMessage;
        return o;
    }
    QJsonArray columns;
    for (const auto &f : r.fields)
        columns.append(f.name);
    QJsonArray rows;
    for (int row = 0; row < r.rowCount(); ++row) {
        QJsonArray jsonRow;
        for (int col = 0; col < r.fieldCount(); ++col) {
            if (r.isNull(row, col))
                jsonRow.append(QJsonValue());
            else
                jsonRow.append(r.stringAt(row, col));
        }
        rows.append(jsonRow);
    }
    o["columns"] = columns;
    o["rows"] = rows;
    o["rowCount"] = r.rowCount();
    if (r.affectedRows != ~0ULL)
        o["rowsAffected"] = static_cast<double>(r.affectedRows);
    return o;
}

// True when `sql` (already comment-stripped, trimmed, upper-cased) is a
// SELECT/WITH statement — the only shapes it is safe to wrap in
// "SELECT * FROM (...) AS mcp_limited LIMIT n OFFSET m" to cap row count.
bool isWrappableSelect(const QString &strippedSql)
{
    const QString trimmed = strippedSql.trimmed();
    return trimmed.startsWith(QStringLiteral("SELECT"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("WITH"), Qt::CaseInsensitive);
}

// Wraps `sql` in an outer "SELECT * FROM (...) LIMIT n [OFFSET m]" so the
// server never materializes more than `kMaxResultRows` rows, regardless of
// what the caller asked for. `appliedLimit`, when given, receives the cap
// that was actually applied (or 0 when `sql` wasn't wrappable) so the caller
// can tell a genuinely truncated result apart from one that simply had fewer
// rows than the cap.
QString applyRowCap(const QString &sql, int requestedLimit, int offset, int *appliedLimit = nullptr)
{
    const QString stripped = SAMCPReadOnlyGuard::stripComments(sql);
    if (!isWrappableSelect(stripped)) {
        if (appliedLimit) *appliedLimit = 0;
        return sql;
    }
    const int limit = (requestedLimit > 0) ? qMin(requestedLimit, kMaxResultRows) : kMaxResultRows;
    if (appliedLimit) *appliedLimit = limit;
    QString wrapped = QStringLiteral("SELECT * FROM (%1) AS mcp_limited LIMIT %2").arg(sql).arg(limit);
    if (offset > 0)
        wrapped += QStringLiteral(" OFFSET %1").arg(offset);
    return wrapped;
}

} // namespace

SAMCPServer::SAMCPServer(SAMCPDataSource *dataSource, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_dataSource(dataSource)
{
    connect(m_server, &QTcpServer::newConnection, this, &SAMCPServer::handleNewConnection);
}

SAMCPServer::~SAMCPServer()
{
    stop();
}

bool SAMCPServer::start(quint16 port, QString *error)
{
    if (m_server->isListening())
        return true;
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (error)
            *error = m_server->errorString();
        Q_EMIT errorOccurred(m_server->errorString());
        return false;
    }
    Q_EMIT started(m_server->serverPort());
    return true;
}

void SAMCPServer::stop()
{
    if (!m_server->isListening())
        return;
    m_server->close();
    for (auto it = m_connections.cbegin(), end = m_connections.cend(); it != end; ++it) {
        it.value()->closed = true;
        m_closedSockets.append(QPointer<QTcpSocket>(it.key()));
    }
    m_connections.clear();
    m_sseSessions.clear();
    releaseClosedSockets();
    Q_EMIT stopped();
}

bool SAMCPServer::isRunning() const { return m_server->isListening(); }
quint16 SAMCPServer::port() const { return m_server->serverPort(); }

void SAMCPServer::handleNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        m_connections.insert(socket, std::make_shared<ConnectionState>());
        connect(socket, &QTcpSocket::readyRead, this, &SAMCPServer::handleReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &SAMCPServer::handleSocketDisconnected);
    }
}

void SAMCPServer::handleSocketDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    for (auto it = m_sseSessions.begin(); it != m_sseSessions.end(); ) {
        if (it.value().isNull() || it.value() == socket)
            it = m_sseSessions.erase(it);
        else
            ++it;
    }
    // Flagging the state (which a request in flight holds by shared_ptr) is
    // what makes that request drop its response instead of writing it to the
    // socket being deleted here.
    const auto it = m_connections.constFind(socket);
    if (it != m_connections.constEnd()) {
        it.value()->closed = true;
        m_connections.erase(it);
    }
    m_closedSockets.append(QPointer<QTcpSocket>(socket));
    releaseClosedSockets();
}

void SAMCPServer::handleReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    const auto it = m_connections.constFind(socket);
    if (it == m_connections.constEnd())
        return;
    // Always drain the socket, even while a request is being served: leaving
    // the bytes in QIODevice's buffer would not raise readyRead again.
    it.value()->buffer += socket->readAll();
    processPendingRequests();
}

bool SAMCPServer::socketIsLive(const QPointer<QTcpSocket> &socket) const
{
    return !socket.isNull() && m_connections.contains(socket.data());
}

void SAMCPServer::releaseClosedSockets()
{
    if (m_processing)
        return;   // still inside a request: the socket may be on the stack
    const QList<QPointer<QTcpSocket>> sockets = std::move(m_closedSockets);
    m_closedSockets.clear();
    for (const QPointer<QTcpSocket> &socket : sockets) {
        if (socket)
            socket->deleteLater();
    }
}

void SAMCPServer::processPendingRequests()
{
    if (m_processing)
        return;   // a request is on the stack; it drains the buffers on its way out
    m_processing = true;
    bool served = true;
    while (served) {
        served = false;
        QList<QPointer<QTcpSocket>> sockets;
        sockets.reserve(m_connections.size());
        for (auto it = m_connections.cbegin(), end = m_connections.cend(); it != end; ++it)
            sockets.append(QPointer<QTcpSocket>(it.key()));
        for (const QPointer<QTcpSocket> &socket : std::as_const(sockets)) {
            if (!socketIsLive(socket))
                continue;
            if (processBuffer(socket, m_connections.value(socket.data())))
                served = true;
        }
    }
    m_processing = false;
    releaseClosedSockets();
}

bool SAMCPServer::tryParseHeaders(ConnectionState &state)
{
    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return false;

    const QByteArray head = state.buffer.left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
        return false;

    QByteArray requestLine = lines.first();
    requestLine = requestLine.trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2)
        return false;
    state.headers.clear();
    state.method = QString::fromLatin1(parts[0]);
    const QString target = QString::fromUtf8(parts[1]);
    const int qmark = target.indexOf(QLatin1Char('?'));
    state.path = (qmark >= 0) ? target.left(qmark) : target;
    state.query = (qmark >= 0) ? target.mid(qmark + 1) : QString();

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        state.headers.insert(key, value);
    }

    state.contentLength = state.headers.value(QStringLiteral("content-length")).toLongLong();
    state.headerEnd = headerEnd + 4;
    state.headersParsed = true;
    return true;
}

bool SAMCPServer::originIsLoopback(const ConnectionState &state, QTcpSocket *socket) const
{
    if (!socket->peerAddress().isLoopback())
        return false;
    const QString origin = state.headers.value(QStringLiteral("origin"));
    if (!origin.isEmpty()) {
        const QUrl url(origin);
        if (!url.host().isEmpty() && !hostIsLoopback(url.host()))
            return false;
    }
    const QString host = state.headers.value(QStringLiteral("host"));
    if (!host.isEmpty() && !hostIsLoopback(host))
        return false;
    return true;
}

bool SAMCPServer::processBuffer(const QPointer<QTcpSocket> &socket, const ConnectionStatePtr &state)
{
    if (!state || state->closed || state->buffer.isEmpty())
        return false;

    if (state->buffer.size() > kMaxRequestBytes) {
        state->buffer.clear();
        writeHttpError(socket, 413, QStringLiteral("Payload too large"));
        return false;
    }

    if (!state->headersParsed && !tryParseHeaders(*state))
        return false; // wait for more data

    if (!originIsLoopback(*state, socket)) {
        state->buffer.clear();
        writeHttpError(socket, 403, QStringLiteral("Forbidden: loopback connections only"));
        return false;
    }

    const qint64 bodyAvailable = state->buffer.size() - state->headerEnd;
    if (bodyAvailable < state->contentLength)
        return false; // wait for the rest of the body

    const QByteArray body = state->buffer.mid(state->headerEnd, state->contentLength);
    // Consumed before dispatching: serving the request can take seconds, and
    // whatever the peer sends meanwhile must not be read back as part of it.
    state->buffer.remove(0, state->headerEnd + state->contentLength);
    state->headersParsed = false;
    dispatch(socket, *state, body);
    return true;
}

void SAMCPServer::dispatch(const QPointer<QTcpSocket> &socket, ConnectionState &state, const QByteArray &body)
{
    if (state.method == QStringLiteral("GET") && state.path == QStringLiteral("/health")) {
        handleHealth(socket);
        return;
    }
    if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/mcp")) {
        handleMcpPost(socket, body);
        return;
    }
    if (state.method == QStringLiteral("GET") && state.path == QStringLiteral("/sse")) {
        handleSseGet(socket, state);
        return;
    }
    if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/message")) {
        handleMessagePost(socket, state, body);
        return;
    }
    writeHttpError(socket, 404, QStringLiteral("Not found"));
}

void SAMCPServer::writeHttpResponse(const QPointer<QTcpSocket> &socket, int statusCode, const QByteArray &contentType,
                                     const QByteArray &body, bool keepAlive)
{
    if (!socketIsLive(socket))
        return;   // peer disconnected while the request was being served
    static const QHash<int, QByteArray> reasons = {
        {200, "OK"}, {202, "Accepted"}, {403, "Forbidden"}, {404, "Not Found"},
        {405, "Method Not Allowed"}, {413, "Payload Too Large"}, {500, "Internal Server Error"},
    };
    QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + ' '
        + reasons.value(statusCode, "Error") + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    response += "\r\n";
    response += body;
    socket->write(response);
    if (!keepAlive)
        socket->disconnectFromHost();
}

void SAMCPServer::writeHttpError(const QPointer<QTcpSocket> &socket, int statusCode, const QString &message)
{
    writeHttpResponse(socket, statusCode, "text/plain; charset=utf-8", message.toUtf8());
}

void SAMCPServer::handleHealth(const QPointer<QTcpSocket> &socket)
{
    writeHttpResponse(socket, 200, "text/plain; charset=utf-8", "OK");
}

void SAMCPServer::handleMcpPost(const QPointer<QTcpSocket> &socket, const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        writeHttpResponse(socket, 200, "application/json",
                           QJsonDocument(jsonRpcError(QJsonValue(), -32700, QStringLiteral("Parse error"))).toJson());
        return;
    }
    const std::optional<QJsonObject> response = handleRpcRequest(doc.object());
    if (!response) {
        // Notification: HTTP spec for Streamable HTTP wants a bare 202 back.
        writeHttpResponse(socket, 202, "application/json", QByteArray());
        return;
    }
    writeHttpResponse(socket, 200, "application/json", QJsonDocument(*response).toJson(QJsonDocument::Compact));
}

void SAMCPServer::handleSseGet(const QPointer<QTcpSocket> &socket, ConnectionState &state)
{
    if (!socketIsLive(socket))
        return;
    state.isSseStream = true;
    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sseSessions.insert(sessionId, socket);

    QByteArray head = "HTTP/1.1 200 OK\r\n";
    head += "Content-Type: text/event-stream\r\n";
    head += "Cache-Control: no-cache\r\n";
    head += "Connection: keep-alive\r\n";
    head += "\r\n";
    socket->write(head);

    const QByteArray endpoint = QByteArray("/message?sessionId=") + sessionId.toUtf8();
    socket->write("event: endpoint\ndata: " + endpoint + "\n\n");
    socket->flush();
    // Socket is intentionally kept open; cleaned up in handleSocketDisconnected().
}

void SAMCPServer::handleMessagePost(const QPointer<QTcpSocket> &socket, const ConnectionState &state, const QByteArray &body)
{
    QUrlQuery query(state.query);
    const QString sessionId = query.queryItemValue(QStringLiteral("sessionId"));
    const QPointer<QTcpSocket> sseSocket = m_sseSessions.value(sessionId);
    if (!socketIsLive(sseSocket)) {
        writeHttpError(socket, 404, QStringLiteral("Unknown MCP session"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const std::optional<QJsonObject> response = handleRpcRequest(doc.object());
        // The SSE stream may have been closed while the tool call was running.
        if (response && socketIsLive(sseSocket) && m_sseSessions.value(sessionId) == sseSocket) {
            const QByteArray payload = QJsonDocument(*response).toJson(QJsonDocument::Compact);
            sseSocket->write("event: message\ndata: " + payload + "\n\n");
            sseSocket->flush();
        }
    }
    writeHttpResponse(socket, 202, "application/json", QByteArray());
}

std::optional<QJsonObject> SAMCPServer::handleRpcRequest(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method.startsWith(QStringLiteral("notifications/")))
        return std::nullopt; // fire-and-forget, no response
    if (!request.contains(QStringLiteral("id")))
        return std::nullopt; // any other notification (no id)

    if (method == QStringLiteral("initialize"))
        return jsonRpcResult(id, handleInitialize(params));
    if (method == QStringLiteral("ping"))
        return jsonRpcResult(id, QJsonObject());
    if (method == QStringLiteral("tools/list"))
        return jsonRpcResult(id, handleToolsList());
    if (method == QStringLiteral("tools/call"))
        return jsonRpcResult(id, handleToolsCall(params));
    if (method == QStringLiteral("resources/list"))
        return jsonRpcResult(id, handleResourcesList());
    if (method == QStringLiteral("resources/read"))
        return jsonRpcResult(id, handleResourcesRead(params));
    if (method == QStringLiteral("prompts/list"))
        return jsonRpcResult(id, handlePromptsList());
    if (method == QStringLiteral("prompts/get"))
        return jsonRpcResult(id, handlePromptsGet(params));
    if (method == QStringLiteral("completion/complete"))
        return jsonRpcResult(id, handleCompletionComplete(params));

    return jsonRpcError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject SAMCPServer::handleInitialize(const QJsonObject &params)
{
    static const QStringList supported = {QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
    QString negotiated = supported.first();
    const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
    if (supported.contains(requested))
        negotiated = requested;

    QJsonObject capabilities{
        {"tools", QJsonObject{{"listChanged", false}}},
        {"resources", QJsonObject{{"listChanged", false}}},
        {"prompts", QJsonObject{{"listChanged", false}}},
        {"completions", QJsonObject()},
    };
    QJsonObject serverInfo{{"name", "sequel-ace-mcp"}, {"version", "1.0.0"}};
    return QJsonObject{
        {"protocolVersion", negotiated},
        {"capabilities", capabilities},
        {"serverInfo", serverInfo},
        {"instructions", QStringLiteral(
            "Sequel Ace exposes the MySQL/MariaDB connections currently open in the application. "
            "Call list_connections first to find a connection id, then list_databases/list_tables/"
            "describe_table to explore schema, or run_query/sample_table to read data. "
            "The server may be running in read-only mode, in which case only SELECT/SHOW/DESCRIBE/"
            "EXPLAIN statements are accepted.")},
    };
}

QJsonObject SAMCPServer::handleToolsList()
{
    QJsonArray tools;
    for (const auto &tool : SAMCPToolDefinitions::all()) {
        tools.append(QJsonObject{
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema},
            {"annotations", tool.annotations},
        });
    }
    return QJsonObject{{"tools", tools}};
}

QJsonObject SAMCPServer::handleToolsCall(const QJsonObject &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();
    return callTool(name, arguments);
}

QJsonObject SAMCPServer::handleResourcesList()
{
    // Enumerating every table of every open connection as a resource would be
    // expensive and rarely used; clients are expected to use the tools
    // (list_tables/describe_table) instead. Kept minimal but spec-valid.
    return QJsonObject{{"resources", QJsonArray()}};
}

QJsonObject SAMCPServer::handleResourcesRead(const QJsonObject &params)
{
    const QString uri = params.value(QStringLiteral("uri")).toString();
    static const QRegularExpression re(QStringLiteral("^sequelace://([^/]+)/([^/]+)/([^/]+)$"));
    const auto match = re.match(uri);
    if (!match.hasMatch())
        return QJsonObject{{"contents", QJsonArray()}};

    const QString connectionId = match.captured(1);
    const QString database = match.captured(2);
    const QString table = match.captured(3);
    QString error;
    SADatabaseSession *session = resolveSession(QJsonObject{{"connection", connectionId}}, &error);
    if (!session)
        return QJsonObject{{"contents", QJsonArray()}};

    const SAResult columns = runSql(session, SASchema::showFullColumns(database, table), false);
    QJsonObject contentItem{
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", QString::fromUtf8(QJsonDocument(resultToJson(columns)).toJson(QJsonDocument::Compact))},
    };
    return QJsonObject{{"contents", QJsonArray{contentItem}}};
}

QJsonObject SAMCPServer::handlePromptsList()
{
    auto prompt = [](const QString &name, const QString &description, std::initializer_list<QString> args) {
        QJsonArray jsonArgs;
        for (const auto &a : args)
            jsonArgs.append(QJsonObject{{"name", a}, {"required", true}});
        return QJsonObject{{"name", name}, {"description", description}, {"arguments", jsonArgs}};
    };
    QJsonArray prompts{
        prompt(QStringLiteral("analyze_schema"), QStringLiteral("Summarise a database's schema."), {"database"}),
        prompt(QStringLiteral("summarize_table"), QStringLiteral("Summarise a table's structure and sample data."), {"database", "table"}),
        prompt(QStringLiteral("optimize_query"), QStringLiteral("Suggest optimisations for a SQL query."), {"sql"}),
    };
    return QJsonObject{{"prompts", prompts}};
}

QJsonObject SAMCPServer::handlePromptsGet(const QJsonObject &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();
    QString text;
    if (name == QStringLiteral("analyze_schema")) {
        text = QStringLiteral("Use describe_table and list_tables on database \"%1\" and summarise its schema, "
                              "relationships and any normalisation issues.")
                   .arg(arguments.value(QStringLiteral("database")).toString());
    } else if (name == QStringLiteral("summarize_table")) {
        text = QStringLiteral("Use describe_table and sample_table on \"%1\".\"%2\" and summarise its purpose, "
                              "columns and typical data.")
                   .arg(arguments.value(QStringLiteral("database")).toString(),
                        arguments.value(QStringLiteral("table")).toString());
    } else if (name == QStringLiteral("optimize_query")) {
        text = QStringLiteral("Use explain_query on the following SQL and suggest optimisations:\n%1")
                   .arg(arguments.value(QStringLiteral("sql")).toString());
    } else {
        return QJsonObject{{"description", QString()}, {"messages", QJsonArray()}};
    }
    QJsonObject message{{"role", "user"}, {"content", QJsonObject{{"type", "text"}, {"text", text}}}};
    return QJsonObject{{"description", text}, {"messages", QJsonArray{message}}};
}

QJsonObject SAMCPServer::handleCompletionComplete(const QJsonObject &params)
{
    Q_UNUSED(params);
    // Argument autocompletion (connection/database/table/type) is a nice-to-have
    // the macOS server offers; not implemented here — an empty completion list
    // is a valid, spec-conformant response.
    return QJsonObject{{"completion", QJsonObject{{"values", QJsonArray()}, {"hasMore", false}}}};
}

SADatabaseSession *SAMCPServer::resolveSession(const QJsonObject &arguments, QString *error) const
{
    if (!m_dataSource) {
        if (error) *error = QStringLiteral("MCP server has no data source configured.");
        return nullptr;
    }
    const QString connectionId = arguments.value(QStringLiteral("connection")).toString();
    SADatabaseSession *session = m_dataSource->sessionFor(connectionId);
    if (!session) {
        if (error) *error = QStringLiteral("No open connection matches \"%1\".").arg(connectionId);
        return nullptr;
    }
    return session;
}

SAResult SAMCPServer::runSql(SADatabaseSession *session, const QString &sql, bool userQuery) const
{
    QEventLoop loop;
    SAResult result;
    session->query(sql, [&](const SAResult &r) {
        result = r;
        loop.quit();
    }, SADatabaseSession::Silent | (userQuery ? SADatabaseSession::UserQuery : SADatabaseSession::NoFlags));
    loop.exec();
    return result;
}

QJsonObject SAMCPServer::callTool(const QString &name, const QJsonObject &arguments)
{
    const SAMCPToolDefinition *tool = SAMCPToolDefinitions::find(name);
    if (!tool)
        return toolErrorResult(QStringLiteral("Unknown tool: %1").arg(name));

    const bool hasSqlArgument = tool->inputSchema.value(QStringLiteral("properties")).toObject()
                                    .contains(QStringLiteral("sql"));
    if (m_readOnly && !hasSqlArgument && !tool->annotations.value(QStringLiteral("readOnlyHint")).toBool())
        return toolErrorResult(QStringLiteral("\"%1\" is disabled while the MCP server is in read-only mode.").arg(name));

    QString error;
    SADatabaseSession *session = resolveSession(arguments, &error);
    if (!session)
        return toolErrorResult(error);

    // Statements carrying a "sql" argument are bound (?) then, if read-only,
    // reclassified on the bound text — never on the raw text (see
    // SAMCPReadOnlyGuard.h for why order matters).
    QString sql = arguments.value(QStringLiteral("sql")).toString();
    if (hasSqlArgument) {
        QStringList params;
        for (const auto &v : arguments.value(QStringLiteral("params")).toArray())
            params.append(v.toString());
        sql = SAMCPReadOnlyGuard::bindParameters(sql, params);
        if (m_readOnly && name != QStringLiteral("explain_query")) {
            const auto verdict = SAMCPReadOnlyGuard::checkReadOnly(sql);
            if (!verdict.allowed)
                return toolErrorResult(verdict.reason);
        }
    }

    const QString database = arguments.value(QStringLiteral("database")).toString();
    const QString table = arguments.value(QStringLiteral("table")).toString();

    if (name == QStringLiteral("list_connections")) {
        QJsonArray conns;
        for (const auto &c : m_dataSource->openConnections()) {
            conns.append(QJsonObject{
                {"id", c.id}, {"name", c.name}, {"host", c.host}, {"database", c.database},
                {"active", c.active}, {"favoriteName", c.favoriteName}, {"favoritePath", c.favoritePath},
            });
        }
        return toolJsonResult(QJsonObject{{"connections", conns}});
    }

    if (name == QStringLiteral("list_databases")) {
        const SAResult r = runSql(session, SASchema::showDatabases(), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        QJsonArray dbs;
        for (int i = 0; i < r.rowCount(); ++i) dbs.append(r.stringAt(i, 0));
        return toolJsonResult(QJsonObject{{"databases", dbs}});
    }

    if (name == QStringLiteral("list_tables")) {
        const SAResult r = runSql(session, QStringLiteral("SHOW FULL TABLES FROM %1").arg(qi(database)), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        QJsonArray tables;
        for (int i = 0; i < r.rowCount(); ++i)
            tables.append(QJsonObject{{"name", r.stringAt(i, 0)}, {"type", r.fieldCount() > 1 ? r.stringAt(i, 1) : QString()}});
        return toolJsonResult(QJsonObject{{"tables", tables}});
    }

    if (name == QStringLiteral("describe_table")) {
        auto escape = [](const QString &s) { return sqlEscape(s); };
        const SAResult columns = runSql(session, SASchema::showFullColumns(database, table), false);
        const SAResult indexes = runSql(session, QStringLiteral("SHOW INDEX FROM %1").arg(qualified(database, table)), false);
        const SAResult keys = runSql(session, SASchema::foreignKeysFor(database, table, escape), false);
        if (!columns.ok) return toolErrorResult(columns.errorMessage);
        return toolJsonResult(QJsonObject{
            {"columns", resultToJson(columns).value("rows")},
            {"columnNames", resultToJson(columns).value("columns")},
            {"indexes", resultToJson(indexes).value("rows")},
            {"foreignKeys", resultToJson(keys).value("rows")},
        });
    }

    if (name == QStringLiteral("get_table_ddl")) {
        const SAResult r = runSql(session, SASchema::showCreate(SASchema::ObjectType::Table, database, table), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(QJsonObject{{"ddl", ddlColumnValue(r)}});
    }

    if (name == QStringLiteral("list_views") || name == QStringLiteral("list_procedures")
        || name == QStringLiteral("list_functions") || name == QStringLiteral("list_triggers")) {
        SAResult r;
        if (name == QStringLiteral("list_triggers")) {
            r = runSql(session, QStringLiteral(
                "SELECT TRIGGER_NAME FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA = %1 ORDER BY TRIGGER_NAME")
                .arg(sqlEscape(database)), false);
        } else if (name == QStringLiteral("list_views")) {
            r = runSql(session, QStringLiteral(
                "SELECT TABLE_NAME FROM information_schema.VIEWS WHERE TABLE_SCHEMA = %1 ORDER BY TABLE_NAME")
                .arg(sqlEscape(database)), false);
        } else {
            const QString routineType = (name == QStringLiteral("list_procedures")) ? QStringLiteral("PROCEDURE") : QStringLiteral("FUNCTION");
            r = runSql(session, QStringLiteral(
                "SELECT SPECIFIC_NAME FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = %1 AND ROUTINE_TYPE = %2 ORDER BY SPECIFIC_NAME")
                .arg(sqlEscape(database), sqlEscape(routineType)), false);
        }
        if (!r.ok) return toolErrorResult(r.errorMessage);
        QJsonArray items;
        for (int i = 0; i < r.rowCount(); ++i) items.append(r.stringAt(i, 0));
        return toolJsonResult(QJsonObject{{"items", items}});
    }

    if (name == QStringLiteral("get_routine_definition")) {
        const QString type = arguments.value(QStringLiteral("type")).toString().toLower();
        const QString routineName = arguments.value(QStringLiteral("name")).toString();
        SAResult r;
        if (type == QStringLiteral("view"))
            r = runSql(session, SASchema::showCreate(SASchema::ObjectType::View, database, routineName), false);
        else if (type == QStringLiteral("procedure"))
            r = runSql(session, SASchema::showCreate(SASchema::ObjectType::Procedure, database, routineName), false);
        else if (type == QStringLiteral("function"))
            r = runSql(session, SASchema::showCreate(SASchema::ObjectType::Function, database, routineName), false);
        else if (type == QStringLiteral("trigger"))
            r = runSql(session, QStringLiteral("SHOW CREATE TRIGGER %1").arg(qualified(database, routineName)), false);
        else
            return toolErrorResult(QStringLiteral("Unknown routine type \"%1\" (expected view/procedure/function/trigger).").arg(type));
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(QJsonObject{{"definition", ddlColumnValue(r)}});
    }

    if (name == QStringLiteral("run_query")) {
        const int limit = arguments.value(QStringLiteral("limit")).toInt(0);
        const int offset = arguments.value(QStringLiteral("offset")).toInt(0);
        int appliedLimit = 0;
        const QString capped = applyRowCap(sql, limit, offset, &appliedLimit);
        const SAResult r = runSql(session, capped, true);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        QJsonObject data = resultToJson(r);
        // Only a result that actually hit the cap is "truncated" — wrapping
        // every SELECT in an outer LIMIT (to bound memory regardless of what
        // the caller asked for) must not by itself mark a short result set
        // as cut off.
        if (appliedLimit > 0) data["truncated"] = (r.rowCount() >= appliedLimit);
        return toolJsonResult(data);
    }

    if (name == QStringLiteral("explain_query")) {
        if (SAMCPReadOnlyGuard::isExplainAnalyze(sql) || SAMCPReadOnlyGuard::isExplainAnalyze(QStringLiteral("EXPLAIN ") + sql))
            return toolErrorResult(QStringLiteral("EXPLAIN ANALYZE executes the query; use run_query with LIMIT instead."));
        const QString explainSql = sql.trimmed().startsWith(QStringLiteral("EXPLAIN"), Qt::CaseInsensitive)
            ? sql : (QStringLiteral("EXPLAIN ") + sql);
        const SAResult r = runSql(session, explainSql, false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(resultToJson(r));
    }

    if (name == QStringLiteral("sample_table")) {
        int limit = arguments.value(QStringLiteral("limit")).toInt(10);
        limit = qBound(1, limit, 1000);
        const int offset = qMax(0, arguments.value(QStringLiteral("offset")).toInt(0));
        const SAResult r = runSql(session, QStringLiteral("SELECT * FROM %1 LIMIT %2 OFFSET %3")
                                       .arg(qualified(database, table)).arg(limit).arg(offset), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(resultToJson(r));
    }

    if (name == QStringLiteral("count_rows")) {
        const SAResult r = runSql(session, QStringLiteral("SELECT COUNT(1) FROM %1").arg(qualified(database, table)), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(QJsonObject{{"count", r.stringAt(0, 0).toLongLong()}});
    }

    if (name == QStringLiteral("kill_query")) {
        const quint64 processId = static_cast<quint64>(arguments.value(QStringLiteral("process_id")).toDouble());
        const SAResult r = runSql(session, SASchema::killQuery(processId, false), false);
        return toolJsonResult(QJsonObject{{"killed", r.ok}});
    }

    if (name == QStringLiteral("export_results")) {
        const QString format = arguments.value(QStringLiteral("format")).toString(QStringLiteral("json")).toLower();
        const SAResult r = runSql(session, sql, true);
        if (!r.ok) return toolErrorResult(r.errorMessage);

        QDir exportDir(m_exportPath.isEmpty() ? QDir::tempPath() : m_exportPath);
        exportDir.mkpath(QStringLiteral("."));
        QString requestedName = arguments.value(QStringLiteral("path")).toString();
        if (requestedName.isEmpty())
            requestedName = QStringLiteral("mcp-export-%1.%2")
                                 .arg(QDateTime::currentMSecsSinceEpoch()).arg(format);
        // Confine to the export folder: strip any path components the client tried to sneak in.
        requestedName = QFileInfo(requestedName).fileName();
        const QString fullPath = exportDir.filePath(requestedName);

        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return toolErrorResult(QStringLiteral("Could not create \"%1\": %2").arg(fullPath, file.errorString()));
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);

        if (format == QStringLiteral("csv")) {
            QStringList fieldNames = r.fieldNames();
            file.write(fieldNames.join(QLatin1Char(',')).toUtf8() + "\n");
            for (int row = 0; row < r.rowCount(); ++row) {
                QStringList cells;
                for (int col = 0; col < r.fieldCount(); ++col) {
                    QString cell = r.stringAt(row, col);
                    // Anti-CSV-injection: neutralise leading =,+,-,@.
                    if (!cell.isEmpty() && QStringLiteral("=+-@").contains(cell.at(0)))
                        cell.prepend(QLatin1Char('\''));
                    cell.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                    cells.append(QLatin1Char('"') + cell + QLatin1Char('"'));
                }
                file.write(cells.join(QLatin1Char(',')).toUtf8() + "\n");
            }
        } else {
            file.write(QJsonDocument(resultToJson(r)).toJson(QJsonDocument::Indented));
        }
        file.close();
        return toolJsonResult(QJsonObject{{"path", fullPath}, {"rowCount", r.rowCount()}, {"format", format}});
    }

    if (name == QStringLiteral("server_info")) {
        const SAResult vars = runSql(session, QStringLiteral("SHOW VARIABLES"), false);
        QJsonObject variables;
        for (int i = 0; i < vars.rowCount(); ++i)
            variables[vars.stringAt(i, 0)] = vars.stringAt(i, 1);
        QString error2;
        const SAMCPConnectionInfo *info = m_dataSource->connectionInfoFor(arguments.value(QStringLiteral("connection")).toString());
        return toolJsonResult(QJsonObject{
            {"variables", variables},
            {"database", session->currentDatabase()},
            {"host", info ? info->host : QString()},
        });
    }

    if (name == QStringLiteral("table_sizes")) {
        const SAResult r = runSql(session, QStringLiteral("SHOW TABLE STATUS FROM %1").arg(qi(database)), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        QJsonArray tables;
        const int nameIdx = r.fieldIndex(QStringLiteral("Name"));
        const int rowsIdx = r.fieldIndex(QStringLiteral("Rows"));
        const int dataIdx = r.fieldIndex(QStringLiteral("Data_length"));
        const int indexIdx = r.fieldIndex(QStringLiteral("Index_length"));
        for (int i = 0; i < r.rowCount(); ++i) {
            tables.append(QJsonObject{
                {"name", nameIdx >= 0 ? r.stringAt(i, nameIdx) : QString()},
                {"row_estimate", rowsIdx >= 0 ? r.stringAt(i, rowsIdx).toDouble() : 0.0},
                {"data_bytes", dataIdx >= 0 ? r.stringAt(i, dataIdx).toDouble() : 0.0},
                {"index_bytes", indexIdx >= 0 ? r.stringAt(i, indexIdx).toDouble() : 0.0},
            });
        }
        return toolJsonResult(QJsonObject{{"tables", tables}});
    }

    if (name == QStringLiteral("process_list")) {
        const SAResult r = runSql(session, QStringLiteral("SHOW FULL PROCESSLIST"), false);
        if (!r.ok) return toolErrorResult(r.errorMessage);
        return toolJsonResult(QJsonObject{{"processes", resultToJson(r).value("rows")}});
    }

    return toolErrorResult(QStringLiteral("Tool \"%1\" is declared but not implemented.").arg(name));
}
