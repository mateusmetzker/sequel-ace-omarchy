//
//  SAMySQLConnection.h
//  Sequel Ace (Linux port)
//
//  Blocking wrapper around a libmariadb connection handle. Ports the
//  behaviour of SPMySQLConnection: connection options, utf8mb4 transport,
//  time-zone handling, post-connect variable fixes, escaping helpers, query
//  execution with timing, multi-result flushing and KILL QUERY via a second
//  connection. Instances are not thread-safe; SADatabaseSession owns one per
//  worker thread and serialises access.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QHash>
#include <QString>
#include <QStringList>

struct st_mysql;

struct SAMySQLOptions {
    QString host;
    unsigned int port = 3306;
    QString socketPath;            // when set, connect through the UNIX socket
    QString user;
    QString password;
    QString database;              // selected after connecting; empty = none

    unsigned int connectTimeout = 10;
    bool useCompression = true;
    bool allowLocalInfile = false;
    bool enableClearTextPlugin = false;
    bool requestServerPublicKey = false;

    bool useSSL = false;
    QString sslKeyPath;
    QString sslCertPath;
    QString sslCAPath;
    QString sslCipherList;        // empty = library default

    QString encoding = QStringLiteral("utf8mb4");

    // 0 = keep the server time zone, 1 = use the client's system zone, 2 = timeZoneIdentifier
    int timeZoneMode = 0;
    QString timeZoneIdentifier;
};

class SAMySQLConnection {
public:
    SAMySQLConnection();
    ~SAMySQLConnection();

    SAMySQLConnection(const SAMySQLConnection &) = delete;
    SAMySQLConnection &operator=(const SAMySQLConnection &) = delete;

    void setOptions(const SAMySQLOptions &options) { m_options = options; }
    const SAMySQLOptions &options() const { return m_options; }

    bool connect();
    bool reconnect();
    void disconnect();
    bool isConnected() const { return m_mysql != nullptr; }
    bool ping();
    bool checkConnection();     // ping and, on failure, attempt one reconnect

    // Executes a statement and materialises the (first) result set.
    SAResult query(const QString &sql);
    // Executes a statement discarding any result set. Returns ok.
    bool execute(const QString &sql, QString *errorMessage = nullptr);
    // Runs "SELECT ..." and returns the first column of the first row ("" on error/empty).
    QString queryScalar(const QString &sql, bool *ok = nullptr);

    bool selectDatabase(const QString &database);
    QString currentDatabase() const { return m_database; }

    bool setEncoding(const QString &encoding);
    QString encoding() const { return m_encoding; }
    void storeEncodingForRestoration() { m_storedEncoding = m_encoding; }
    void restoreStoredEncoding() { if (!m_storedEncoding.isEmpty() && m_storedEncoding != m_encoding) setEncoding(m_storedEncoding); m_storedEncoding.clear(); }

    // Applies the time zone policy from the options (SET time_zone ...).
    bool applyTimeZone(QString *warning = nullptr);

    // Issues KILL QUERY on a *separate* connection for the given thread id.
    // Safe to call from another thread while this connection is busy.
    static bool killQuery(const SAMySQLOptions &options, unsigned long threadId, QString *error = nullptr);
    static bool killConnection(const SAMySQLOptions &options, unsigned long threadId, QString *error = nullptr);
    void markLastQueryCancelled() { m_lastQueryWasCancelled = true; }
    bool lastQueryWasCancelled() const { return m_lastQueryWasCancelled; }

    // Last error state (of the most recent operation).
    unsigned int lastErrorNumber() const { return m_lastErrorNumber; }
    QString lastErrorMessage() const { return m_lastErrorMessage; }
    QString lastSqlState() const { return m_lastSqlState; }
    bool queryErrored() const { return m_lastErrorNumber != 0; }
    quint64 rowsAffectedByLastQuery() const { return m_lastAffectedRows; }
    quint64 lastInsertId() const { return m_lastInsertId; }
    double lastQueryExecutionTime() const { return m_lastExecutionTime; }

    // Server information.
    QString serverVersionString() const { return m_serverVersionString; }
    unsigned long serverVersionNumber() const { return m_serverVersionNumber; } // e.g. 80036, 101106
    unsigned int serverMajorVersion() const { return static_cast<unsigned int>(m_serverVersionNumber / 10000); }
    unsigned int serverMinorVersion() const { return static_cast<unsigned int>((m_serverVersionNumber / 100) % 100); }
    unsigned int serverReleaseVersion() const { return static_cast<unsigned int>(m_serverVersionNumber % 100); }
    bool serverVersionIsGreaterThanOrEqualTo(unsigned int major, unsigned int minor, unsigned int release) const;
    bool isMariaDB() const { return m_isMariaDB; }
    unsigned long threadId() const { return m_threadId; }
    bool isConnectedViaSSL() const { return !m_sslCipher.isEmpty(); }
    QString sslCipher() const { return m_sslCipher; }
    quint64 maxAllowedPacket() const { return m_maxAllowedPacket; }
    QHash<QString, QString> serverVariables() const { return m_variables; }
    double timeConnected() const;

    // Escaping helpers. Work without a connection (falling back to a manual escape).
    QString escapeString(const QString &value, bool includingQuotes = true) const;
    QString escapeAndQuoteString(const QString &value) const { return escapeString(value, true); }
    QString escapeAndQuoteData(const QByteArray &data) const;   // X'hex'
    static QString quoteIdentifier(const QString &identifier);  // `name`
    static QString quoteIdentifierList(const QStringList &identifiers, const QString &separator = QStringLiteral(", "));
    static QString escapeLikePattern(const QString &value);     // escapes % _ and (backslash)

    // Called once per thread that will use a connection.
    static void initialiseThread();
    static void finaliseThread();
    static void initialiseLibrary();

    // Charset metadata (charsetnr -> charset name / collation / max bytes per char)
    void loadCharsetMetadata();

private:
    st_mysql *makeRawConnection(QString *error) const;
    void updateErrorState();
    void clearErrorState();
    void updateConnectionVariables();
    void updateMaxAllowedPacket();
    void fillFieldsFromResult(SAResult &result, void *mysqlResult) const;
    void flushRemainingResults();
    void handleConnectionLoss();

    SAMySQLOptions m_options;
    st_mysql *m_mysql = nullptr;
    QString m_database;
    QString m_encoding;
    QString m_storedEncoding;
    QString m_serverVersionString;
    unsigned long m_serverVersionNumber = 0;
    bool m_isMariaDB = false;
    unsigned long m_threadId = 0;
    QString m_sslCipher;
    quint64 m_maxAllowedPacket = 0;
    QHash<QString, QString> m_variables;
    qint64 m_connectedAtMs = 0;

    unsigned int m_lastErrorNumber = 0;
    QString m_lastErrorMessage;
    QString m_lastSqlState;
    quint64 m_lastAffectedRows = ~0ULL;
    quint64 m_lastInsertId = 0;
    double m_lastExecutionTime = 0.0;
    bool m_lastQueryWasCancelled = false;

    struct CharsetInfo { QString charset; QString collation; unsigned int maxBytes = 1; };
    QHash<unsigned int, CharsetInfo> m_charsets;
};
