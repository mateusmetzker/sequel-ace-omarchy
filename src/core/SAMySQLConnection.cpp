//
//  SAMySQLConnection.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAMySQLConnection.h"
#include "SASQLTypes.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimeZone>
#include <QDebug>
#include <QMutex>

#include <mysql.h>
#include <errmsg.h>

#include <cstring>

namespace {

// Client flags mirror SPMySQLConnectionOptions: compression (when requested),
// interactive client, and multi-result support for stored procedures.
unsigned long clientFlagsFor(const SAMySQLOptions &options)
{
    unsigned long flags = CLIENT_INTERACTIVE | CLIENT_MULTI_RESULTS;
    if (options.useCompression) flags |= CLIENT_COMPRESS;
    return flags;
}

QString fromServer(const char *bytes)
{
    return bytes ? QString::fromUtf8(bytes) : QString();
}

QMutex &libraryMutex()
{
    static QMutex m;
    return m;
}

} // namespace

// ---- lifecycle ---------------------------------------------------------------

SAMySQLConnection::SAMySQLConnection()
{
    initialiseLibrary();
}

SAMySQLConnection::~SAMySQLConnection()
{
    disconnect();
}

void SAMySQLConnection::initialiseLibrary()
{
    static bool initialised = false;
    QMutexLocker lock(&libraryMutex());
    if (!initialised) {
        mysql_library_init(0, nullptr, nullptr);
        initialised = true;
    }
}

void SAMySQLConnection::initialiseThread()
{
    initialiseLibrary();
    mysql_thread_init();
}

void SAMySQLConnection::finaliseThread()
{
    mysql_thread_end();
}

// ---- connecting --------------------------------------------------------------

st_mysql *SAMySQLConnection::makeRawConnection(QString *error) const
{
    MYSQL *handle = mysql_init(nullptr);
    if (!handle) {
        if (error) *error = QStringLiteral("mysql_init() failed: out of memory");
        return nullptr;
    }

    // Never let the library reconnect behind our back; the session layer handles it.
    my_bool falseValue = 0;
    my_bool trueValue = 1;
    mysql_options(handle, MYSQL_OPT_RECONNECT, &falseValue);

    // Choose the transport explicitly: a host name of "localhost" would make the
    // library silently switch to the default UNIX socket.
    unsigned int protocol = m_options.socketPath.isEmpty() ? MYSQL_PROTOCOL_TCP : MYSQL_PROTOCOL_SOCKET;
    mysql_options(handle, MYSQL_OPT_PROTOCOL, &protocol);

    unsigned int timeout = m_options.connectTimeout ? m_options.connectTimeout : 10;
    mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    const QByteArray encoding = (m_options.encoding.isEmpty() ? QStringLiteral("utf8mb4") : m_options.encoding).toUtf8();
    mysql_options(handle, MYSQL_SET_CHARSET_NAME, encoding.constData());

    if (m_options.allowLocalInfile) {
        unsigned int on = 1;
        mysql_options(handle, MYSQL_OPT_LOCAL_INFILE, &on);
    }
    if (m_options.enableClearTextPlugin) {
        mysql_options(handle, MYSQL_ENABLE_CLEARTEXT_PLUGIN, &trueValue);
    }
#ifdef MYSQL_OPT_GET_SERVER_PUBLIC_KEY
    if (m_options.requestServerPublicKey) {
        mysql_options(handle, MYSQL_OPT_GET_SERVER_PUBLIC_KEY, &trueValue);
    }
#endif

    if (m_options.useSSL) {
        const QByteArray key = m_options.sslKeyPath.toUtf8();
        const QByteArray cert = m_options.sslCertPath.toUtf8();
        const QByteArray ca = m_options.sslCAPath.toUtf8();
        const QByteArray ciphers = m_options.sslCipherList.toUtf8();
        if (!key.isEmpty()) mysql_options(handle, MYSQL_OPT_SSL_KEY, key.constData());
        if (!cert.isEmpty()) mysql_options(handle, MYSQL_OPT_SSL_CERT, cert.constData());
        if (!ca.isEmpty()) mysql_options(handle, MYSQL_OPT_SSL_CA, ca.constData());
        if (!ciphers.isEmpty()) mysql_options(handle, MYSQL_OPT_SSL_CIPHER, ciphers.constData());
#ifdef MYSQL_OPT_SSL_ENFORCE
        // Abort instead of silently falling back to a clear-text connection.
        mysql_options(handle, MYSQL_OPT_SSL_ENFORCE, &trueValue);
#endif
#ifdef MYSQL_OPT_SSL_VERIFY_SERVER_CERT
        // Only verify the server certificate when a CA was supplied, matching the macOS app.
        my_bool verify = ca.isEmpty() ? 0 : 1;
        mysql_options(handle, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
#endif
    }

    const QByteArray host = m_options.host.toUtf8();
    const QByteArray user = m_options.user.toUtf8();
    const QByteArray password = m_options.password.toUtf8();
    const QByteArray socket = m_options.socketPath.toUtf8();

    MYSQL *connected = mysql_real_connect(handle,
                                          m_options.socketPath.isEmpty() ? (host.isEmpty() ? "127.0.0.1" : host.constData()) : nullptr,
                                          user.constData(),
                                          password.constData(),
                                          nullptr,
                                          m_options.socketPath.isEmpty() ? m_options.port : 0,
                                          socket.isEmpty() ? nullptr : socket.constData(),
                                          clientFlagsFor(m_options));
    if (!connected) {
        if (error) *error = QStringLiteral("%1 (%2)").arg(fromServer(mysql_error(handle))).arg(mysql_errno(handle));
        mysql_close(handle);
        return nullptr;
    }
    return connected;
}

bool SAMySQLConnection::connect()
{
    if (m_mysql) return true;
    clearErrorState();
    m_lastQueryWasCancelled = false;

    QString error;
    m_mysql = makeRawConnection(&error);
    if (!m_mysql) {
        m_lastErrorNumber = CR_CONN_HOST_ERROR;
        m_lastErrorMessage = error;
        return false;
    }

    m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_threadId = mysql_thread_id(m_mysql);
    m_serverVersionString = QString::fromLatin1(mysql_get_server_info(m_mysql));
    m_serverVersionNumber = mysql_get_server_version(m_mysql);
    m_isMariaDB = m_serverVersionString.contains(QLatin1String("mariadb"), Qt::CaseInsensitive);
    const char *cipher = mysql_get_ssl_cipher(m_mysql);
    m_sslCipher = cipher ? QString::fromLatin1(cipher) : QString();
    m_encoding = QString::fromLatin1(mysql_character_set_name(m_mysql));

    updateConnectionVariables();
    if (!m_mysql) return false;
    updateMaxAllowedPacket();
    loadCharsetMetadata();

    if (!m_options.database.isEmpty()) {
        // A missing database is reported through the error state but does not
        // fail the connection itself, matching the macOS behaviour.
        selectDatabase(m_options.database);
    }
    applyTimeZone();
    clearErrorState();
    return true;
}

bool SAMySQLConnection::reconnect()
{
    const QString database = m_database.isEmpty() ? m_options.database : m_database;
    const QString encoding = m_encoding;
    disconnect();
    if (!connect()) return false;
    if (!database.isEmpty() && database != m_database) selectDatabase(database);
    if (!encoding.isEmpty() && encoding != m_encoding) setEncoding(encoding);
    return true;
}

void SAMySQLConnection::disconnect()
{
    if (m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
    m_threadId = 0;
    m_sslCipher.clear();
}

bool SAMySQLConnection::ping()
{
    if (!m_mysql) return false;
    if (mysql_ping(m_mysql) != 0) {
        updateErrorState();
        return false;
    }
    return true;
}

bool SAMySQLConnection::checkConnection()
{
    if (ping()) return true;
    return reconnect();
}

double SAMySQLConnection::timeConnected() const
{
    if (!m_mysql || !m_connectedAtMs) return 0.0;
    return (QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs) / 1000.0;
}

bool SAMySQLConnection::serverVersionIsGreaterThanOrEqualTo(unsigned int major, unsigned int minor, unsigned int release) const
{
    return m_serverVersionNumber >= (major * 10000UL + minor * 100UL + release);
}

// ---- error bookkeeping -------------------------------------------------------

void SAMySQLConnection::clearErrorState()
{
    m_lastErrorNumber = 0;
    m_lastErrorMessage.clear();
    m_lastSqlState.clear();
}

void SAMySQLConnection::updateErrorState()
{
    if (!m_mysql) return;
    m_lastErrorNumber = mysql_errno(m_mysql);
    m_lastErrorMessage = fromServer(mysql_error(m_mysql));
    m_lastSqlState = QString::fromLatin1(mysql_sqlstate(m_mysql));
}

void SAMySQLConnection::handleConnectionLoss()
{
    // CR_SERVER_GONE_ERROR / CR_SERVER_LOST: the handle is unusable. Keep the
    // error text but drop the handle so callers can decide to reconnect.
    if (m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
}

// ---- post-connect setup ------------------------------------------------------

void SAMySQLConnection::updateConnectionVariables()
{
    // Port of -[SPMySQLConnection _updateConnectionVariables].
    SAResult vars = query(QStringLiteral("SHOW VARIABLES"));
    if (!vars.ok || !m_mysql) return;
    m_variables.clear();
    for (int i = 0; i < vars.rowCount(); ++i)
        m_variables.insert(vars.stringAt(i, 0), vars.stringAt(i, 1));

    if (m_variables.contains(QStringLiteral("character_set_results")))
        m_encoding = m_variables.value(QStringLiteral("character_set_results"));
    else if (m_variables.contains(QStringLiteral("character_set_client")))
        m_encoding = m_variables.value(QStringLiteral("character_set_client"));

    // Raise short interactive timeouts so keep-alive pings have a chance.
    if (m_variables.contains(QStringLiteral("interactive_timeout"))
        && m_variables.value(QStringLiteral("interactive_timeout")).toInt() < 300) {
        execute(QStringLiteral("SET interactive_timeout=600"));
        execute(QStringLiteral("SET wait_timeout=600"));
    }

    // MySQL 8 caches information_schema statistics for a day by default; the
    // table info panel would lag behind reality. ProxySQL does not know this
    // variable and a SET would pin the connection to a hostgroup, so skip it there.
    if (m_variables.contains(QStringLiteral("information_schema_stats_expiry"))
        && m_variables.value(QStringLiteral("information_schema_stats_expiry")).toInt() != 0
        && !m_variables.contains(QStringLiteral("admin-version"))) {
        execute(QStringLiteral("SET information_schema_stats_expiry=0"));
    }
}

void SAMySQLConnection::updateMaxAllowedPacket()
{
    bool ok = false;
    QString value = queryScalar(QStringLiteral("SELECT @@global.max_allowed_packet"), &ok);
    if (!ok || value.isEmpty()) {
        SAResult r = query(QStringLiteral("SHOW VARIABLES LIKE 'max_allowed_packet'"));
        if (r.ok && r.rowCount()) value = r.stringAt(0, 1);
    }
    m_maxAllowedPacket = value.toULongLong();
    clearErrorState();
}

void SAMySQLConnection::loadCharsetMetadata()
{
    m_charsets.clear();
    SAResult r = query(QStringLiteral(
        "SELECT c.ID, c.COLLATION_NAME, c.CHARACTER_SET_NAME, s.MAXLEN "
        "FROM information_schema.COLLATIONS c "
        "JOIN information_schema.CHARACTER_SETS s ON s.CHARACTER_SET_NAME = c.CHARACTER_SET_NAME"));
    if (r.ok) {
        for (int i = 0; i < r.rowCount(); ++i) {
            CharsetInfo info;
            info.collation = r.stringAt(i, 1);
            info.charset = r.stringAt(i, 2);
            info.maxBytes = qMax(1u, r.stringAt(i, 3).toUInt());
            m_charsets.insert(r.stringAt(i, 0).toUInt(), info);
        }
    }
    // The binary pseudo-charset always exists.
    CharsetInfo binary;
    binary.charset = QStringLiteral("binary");
    binary.collation = QStringLiteral("binary");
    binary.maxBytes = 1;
    m_charsets.insert(SASQLTypes::BinaryCharsetNumber, binary);
    clearErrorState();
}

bool SAMySQLConnection::selectDatabase(const QString &database)
{
    if (!m_mysql) return false;
    const QByteArray name = database.toUtf8();
    if (mysql_select_db(m_mysql, name.constData()) != 0) {
        updateErrorState();
        return false;
    }
    m_database = database;
    clearErrorState();
    return true;
}

bool SAMySQLConnection::setEncoding(const QString &encoding)
{
    if (!execute(QStringLiteral("SET NAMES %1").arg(escapeAndQuoteString(encoding)))) return false;
    // Keep libmysqlclient's escaping in sync with the server's expectations.
    mysql_set_character_set(m_mysql, encoding.toUtf8().constData());
    m_encoding = encoding;
    return true;
}

bool SAMySQLConnection::applyTimeZone(QString *warning)
{
    if (!m_mysql) return false;
    QString identifier;
    switch (m_options.timeZoneMode) {
    case 1:
        identifier = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        break;
    case 2:
        identifier = m_options.timeZoneIdentifier;
        break;
    default:
        return true;   // keep the server default
    }
    if (identifier.isEmpty()) return true;
    if (!execute(QStringLiteral("SET time_zone = %1").arg(escapeAndQuoteString(identifier)))) {
        if (warning) *warning = QStringLiteral("Failed to set time_zone to %1: %2").arg(identifier, m_lastErrorMessage);
        clearErrorState();
        return false;
    }
    return true;
}

// ---- querying ----------------------------------------------------------------

void SAMySQLConnection::fillFieldsFromResult(SAResult &result, void *mysqlResult) const
{
    MYSQL_RES *res = static_cast<MYSQL_RES *>(mysqlResult);
    const unsigned int count = mysql_num_fields(res);
    MYSQL_FIELD *fields = mysql_fetch_fields(res);
    result.fields.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const MYSQL_FIELD &f = fields[i];
        SAField field;
        field.name = fromServer(f.name);
        field.orgName = fromServer(f.org_name);
        field.table = fromServer(f.table);
        field.orgTable = fromServer(f.org_table);
        field.db = fromServer(f.db);
        field.type = f.type;
        field.flags = f.flags;
        field.charsetnr = f.charsetnr;
        field.length = f.length;
        field.decimals = f.decimals;
        const CharsetInfo info = m_charsets.value(f.charsetnr);
        field.maxBytesPerChar = info.maxBytes ? info.maxBytes : 1;
        field.charset = info.charset;
        field.collation = info.collation;
        field.typeName = SASQLTypes::typeNameForWireType(f.type, f.charsetnr, f.flags, f.length, field.maxBytesPerChar);
        field.typeGroup = SASQLTypes::typeGroupForWireType(f.type, f.charsetnr, f.flags);
        result.fields.append(field);
    }
}

void SAMySQLConnection::flushRemainingResults()
{
    // Stored procedures return several result sets; discard the rest so the
    // connection is ready for the next statement.
    while (m_mysql && mysql_more_results(m_mysql)) {
        if (mysql_next_result(m_mysql) > 0) break;
        MYSQL_RES *extra = mysql_store_result(m_mysql);
        if (extra) mysql_free_result(extra);
    }
}

SAResult SAMySQLConnection::query(const QString &sql)
{
    SAResult result;
    result.query = sql;
    m_lastQueryWasCancelled = false;
    m_lastAffectedRows = ~0ULL;
    m_lastInsertId = 0;

    if (!m_mysql) {
        m_lastErrorNumber = CR_SERVER_GONE_ERROR;
        m_lastErrorMessage = QStringLiteral("Not connected to a MySQL server.");
        return SAResult::errorResult(sql, m_lastErrorNumber, m_lastErrorMessage);
    }

    const QByteArray bytes = sql.toUtf8();
    QElapsedTimer timer;
    timer.start();
    const int rc = mysql_real_query(m_mysql, bytes.constData(), static_cast<unsigned long>(bytes.size()));
    result.executionTime = timer.nsecsElapsed() / 1e9;
    m_lastExecutionTime = result.executionTime;

    if (rc != 0) {
        updateErrorState();
        result.errorNumber = m_lastErrorNumber;
        result.errorMessage = m_lastErrorMessage;
        result.sqlState = m_lastSqlState;
        result.ok = false;
        result.affectedRows = ~0ULL;
        if (m_lastErrorNumber == CR_SERVER_GONE_ERROR || m_lastErrorNumber == CR_SERVER_LOST) handleConnectionLoss();
        return result;
    }

    clearErrorState();
    MYSQL_RES *res = mysql_store_result(m_mysql);
    if (res) {
        result.hasResultSet = true;
        fillFieldsFromResult(result, res);
        const int fieldCount = result.fields.size();
        result.rows.reserve(static_cast<int>(mysql_num_rows(res)));
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            unsigned long *lengths = mysql_fetch_lengths(res);
            SARow cells;
            cells.reserve(fieldCount);
            for (int i = 0; i < fieldCount; ++i) {
                if (row[i] == nullptr) cells.append(SACell::null());
                else cells.append(SACell::of(QByteArray(row[i], static_cast<int>(lengths[i]))));
            }
            result.rows.append(std::move(cells));
        }
        mysql_free_result(res);
        result.affectedRows = ~0ULL;
    } else {
        if (mysql_field_count(m_mysql) != 0) {
            // A result set was expected but could not be stored (out of memory / lost connection).
            updateErrorState();
            result.errorNumber = m_lastErrorNumber;
            result.errorMessage = m_lastErrorMessage;
            result.sqlState = m_lastSqlState;
            result.ok = false;
            if (m_lastErrorNumber == CR_SERVER_GONE_ERROR || m_lastErrorNumber == CR_SERVER_LOST) handleConnectionLoss();
            return result;
        }
        result.hasResultSet = false;
        result.affectedRows = mysql_affected_rows(m_mysql);
        result.insertId = mysql_insert_id(m_mysql);
    }
    result.warningCount = mysql_warning_count(m_mysql);
    m_lastAffectedRows = result.affectedRows;
    m_lastInsertId = result.insertId;
    result.ok = true;
    flushRemainingResults();
    return result;
}

bool SAMySQLConnection::execute(const QString &sql, QString *errorMessage)
{
    SAResult r = query(sql);
    if (!r.ok && errorMessage) *errorMessage = r.errorMessage;
    return r.ok;
}

QString SAMySQLConnection::queryScalar(const QString &sql, bool *ok)
{
    SAResult r = query(sql);
    if (ok) *ok = r.ok;
    if (!r.ok) return QString();
    return r.firstValue();
}

// ---- KILL --------------------------------------------------------------------

bool SAMySQLConnection::killQuery(const SAMySQLOptions &options, unsigned long threadId, QString *error)
{
    SAMySQLConnection aux;
    SAMySQLOptions opts = options;
    opts.database.clear();
    aux.setOptions(opts);
    if (!aux.connect()) {
        if (error) *error = aux.lastErrorMessage();
        return false;
    }
    // TiDB uses a different syntax; the macOS app special-cases it too.
    const bool tidb = aux.serverVersionString().contains(QLatin1String("TiDB"), Qt::CaseInsensitive);
    const QString sql = tidb ? QStringLiteral("KILL TIDB QUERY %1").arg(threadId) : QStringLiteral("KILL QUERY %1").arg(threadId);
    const bool ok = aux.execute(sql, error);
    aux.disconnect();
    return ok;
}

bool SAMySQLConnection::killConnection(const SAMySQLOptions &options, unsigned long threadId, QString *error)
{
    SAMySQLConnection aux;
    SAMySQLOptions opts = options;
    opts.database.clear();
    aux.setOptions(opts);
    if (!aux.connect()) {
        if (error) *error = aux.lastErrorMessage();
        return false;
    }
    const bool tidb = aux.serverVersionString().contains(QLatin1String("TiDB"), Qt::CaseInsensitive);
    const QString sql = tidb ? QStringLiteral("KILL TIDB CONNECTION %1").arg(threadId) : QStringLiteral("KILL CONNECTION %1").arg(threadId);
    const bool ok = aux.execute(sql, error);
    aux.disconnect();
    return ok;
}

// ---- escaping ----------------------------------------------------------------

QString SAMySQLConnection::escapeString(const QString &value, bool includingQuotes) const
{
    const QByteArray utf8 = value.toUtf8();
    QByteArray buffer;
    buffer.resize(utf8.size() * 2 + 1);
    unsigned long written;
    if (m_mysql) {
        written = mysql_real_escape_string(m_mysql, buffer.data(), utf8.constData(), static_cast<unsigned long>(utf8.size()));
    } else {
        // Manual escape matching mysql_real_escape_string's behaviour for utf8.
        QByteArray out;
        out.reserve(utf8.size() * 2);
        for (char c : utf8) {
            switch (c) {
            case '\0': out += "\\0"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '"': out += "\\\""; break;
            case '\x1a': out += "\\Z"; break;
            default: out += c;
            }
        }
        buffer = out;
        written = static_cast<unsigned long>(out.size());
    }
    buffer.truncate(static_cast<int>(written));
    QString escaped = QString::fromUtf8(buffer);
    return includingQuotes ? QLatin1Char('\'') + escaped + QLatin1Char('\'') : escaped;
}

QString SAMySQLConnection::escapeAndQuoteData(const QByteArray &data) const
{
    if (data.isEmpty()) return QStringLiteral("''");
    return QStringLiteral("X'") + QString::fromLatin1(data.toHex()) + QLatin1Char('\'');
}

QString SAMySQLConnection::quoteIdentifier(const QString &identifier)
{
    QString escaped = identifier;
    escaped.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QLatin1Char('`') + escaped + QLatin1Char('`');
}

QString SAMySQLConnection::quoteIdentifierList(const QStringList &identifiers, const QString &separator)
{
    QStringList quoted;
    quoted.reserve(identifiers.size());
    for (const QString &id : identifiers) quoted << quoteIdentifier(id);
    return quoted.join(separator);
}

QString SAMySQLConnection::escapeLikePattern(const QString &value)
{
    QString out = value;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    out.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return out;
}
