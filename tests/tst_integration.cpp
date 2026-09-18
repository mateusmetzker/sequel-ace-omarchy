//
//  tst_integration.cpp
//  Sequel Ace (Linux port) - integration tests against a live MySQL/MariaDB.
//
//  Enable with SA_TEST_MYSQL=1 and point SA_TEST_MYSQL_HOST/PORT/USER/PASSWORD
//  at a server. The tests create and drop their own database.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include "SAConnectionInfo.h"
#include "SADatabaseSession.h"
#include "SAMCPDataSource.h"
#include "SAMCPServer.h"
#include "SAMySQLConnection.h"
#include "SASchemaQueries.h"

class IntegrationTests : public QObject {
    Q_OBJECT

    SAMySQLOptions m_options;
    bool m_enabled = false;
    const QString m_db = QStringLiteral("sa_it_%1").arg(QCoreApplication::applicationPid());

    static QString env(const char *name, const QString &fallback)
    {
        const QString v = qEnvironmentVariable(name);
        return v.isEmpty() ? fallback : v;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_enabled = qEnvironmentVariableIntValue("SA_TEST_MYSQL") == 1;
        if (!m_enabled) QSKIP("Set SA_TEST_MYSQL=1 to run the integration tests against a live server.");
        m_options.host = env("SA_TEST_MYSQL_HOST", QStringLiteral("127.0.0.1"));
        m_options.port = env("SA_TEST_MYSQL_PORT", QStringLiteral("3306")).toUInt();
        m_options.user = env("SA_TEST_MYSQL_USER", QStringLiteral("root"));
        m_options.password = env("SA_TEST_MYSQL_PASSWORD", QString());
        m_options.connectTimeout = 5;

        SAMySQLConnection c;
        c.setOptions(m_options);
        QVERIFY2(c.connect(), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.execute(QStringLiteral("CREATE DATABASE %1 CHARACTER SET utf8mb4").arg(SAMySQLConnection::quoteIdentifier(m_db))));
        QVERIFY(c.selectDatabase(m_db));
        QVERIFY2(c.execute(QStringLiteral(
            "CREATE TABLE items (id INT UNSIGNED NOT NULL AUTO_INCREMENT, name VARCHAR(50) NOT NULL, "
            "price DECIMAL(8,2) DEFAULT NULL, notes TEXT, blob_col BLOB, flag BIT(1) NOT NULL DEFAULT b'0', "
            "kind ENUM('a','b') DEFAULT 'a', created TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP, "
            "PRIMARY KEY (id), KEY idx_name (name)) ENGINE=InnoDB")), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.execute(QStringLiteral("INSERT INTO items (name, price, notes, blob_col, flag) VALUES ('Ção', 1.50, NULL, X'89504E47', b'1'), ('two', NULL, 'note', NULL, b'0')")));
    }

    void cleanupTestCase()
    {
        if (!m_enabled) return;
        SAMySQLConnection c;
        c.setOptions(m_options);
        if (c.connect()) c.execute(QStringLiteral("DROP DATABASE IF EXISTS %1").arg(SAMySQLConnection::quoteIdentifier(m_db)));
    }

    void connectAndServerInfo()
    {
        SAMySQLConnection c;
        c.setOptions(m_options);
        QVERIFY2(c.connect(), qPrintable(c.lastErrorMessage()));
        QVERIFY(c.serverVersionNumber() >= 50700UL);
        QVERIFY(!c.serverVersionString().isEmpty());
        QVERIFY(c.threadId() > 0);
        QCOMPARE(c.encoding(), QStringLiteral("utf8mb4"));
        QVERIFY(c.maxAllowedPacket() > 0);
        QVERIFY(c.ping());
        QVERIFY(c.serverVariables().contains(QStringLiteral("version")));
    }

    void queryTypesAndNulls()
    {
        SAMySQLConnection c;
        SAMySQLOptions o = m_options;
        o.database = m_db;
        c.setOptions(o);
        QVERIFY2(c.connect(), qPrintable(c.lastErrorMessage()));
        SAResult r = c.query(QStringLiteral("SELECT * FROM items ORDER BY id"));
        QVERIFY2(r.ok, qPrintable(r.errorMessage));
        QVERIFY(r.hasResultSet);
        QCOMPARE(r.rowCount(), 2);
        QCOMPARE(r.fieldCount(), 8);
        QCOMPARE(r.fields[0].typeName, QStringLiteral("INT"));
        QCOMPARE(r.fields[0].typeGroup, QStringLiteral("integer"));
        QVERIFY(r.fields[0].isPrimaryKey());
        QVERIFY(r.fields[0].isAutoIncrement());
        QCOMPARE(r.fields[1].typeName, QStringLiteral("VARCHAR"));
        QCOMPARE(r.fields[1].displayLength(), 50ULL);
        QCOMPARE(r.fields[2].typeName, QStringLiteral("DECIMAL"));
        QCOMPARE(r.fields[2].typeGroup, QStringLiteral("float"));
        QCOMPARE(r.fields[3].typeName, QStringLiteral("TEXT"));
        QCOMPARE(r.fields[3].typeGroup, QStringLiteral("textdata"));
        QCOMPARE(r.fields[4].typeName, QStringLiteral("BLOB"));
        QCOMPARE(r.fields[4].typeGroup, QStringLiteral("blobdata"));
        QVERIFY(r.fields[4].isBinary());
        QCOMPARE(r.fields[5].typeName, QStringLiteral("BIT"));
        QCOMPARE(r.fields[6].typeName, QStringLiteral("ENUM"));
        QCOMPARE(r.fields[6].typeGroup, QStringLiteral("enum"));
        QCOMPARE(r.fields[7].typeGroup, QStringLiteral("date"));
        QCOMPARE(r.fields[0].orgTable, QStringLiteral("items"));
        QCOMPARE(r.fields[0].db, m_db);

        QCOMPARE(r.stringAt(0, "name"), QStringLiteral("Ção"));
        QVERIFY(r.isNull(0, 3));
        QCOMPARE(r.cell(0, 4).data, QByteArray("\x89PNG", 4));
        QCOMPARE(r.cell(0, 5).data, QByteArray("\x01", 1));
        QVERIFY(r.isNull(1, 2));
        QCOMPARE(r.stringAt(1, "notes"), QStringLiteral("note"));
    }

    void dmlAffectedRowsAndErrors()
    {
        SAMySQLConnection c;
        SAMySQLOptions o = m_options;
        o.database = m_db;
        c.setOptions(o);
        QVERIFY(c.connect());
        SAResult ins = c.query(QStringLiteral("INSERT INTO items (name) VALUES ('three')"));
        QVERIFY(ins.ok);
        QVERIFY(!ins.hasResultSet);
        QCOMPARE(ins.affectedRows, 1ULL);
        QVERIFY(ins.insertId >= 3ULL);
        SAResult upd = c.query(QStringLiteral("UPDATE items SET price = 2 WHERE name = 'three'"));
        QCOMPARE(upd.affectedRows, 1ULL);
        SAResult bad = c.query(QStringLiteral("SELEC nonsense"));
        QVERIFY(!bad.ok);
        QCOMPARE(bad.errorNumber, 1064u);
        QVERIFY(bad.errorMessage.contains(QLatin1String("syntax")));
        QCOMPARE(bad.sqlState, QStringLiteral("42000"));
        QVERIFY(c.isConnected());
        // Unicode round trip through escaping.
        const QString text = QStringLiteral("it's \"ç\" 😀 \\ ok");
        SAResult rt = c.query(QStringLiteral("SELECT %1 AS v").arg(c.escapeAndQuoteString(text)));
        QVERIFY(rt.ok);
        QCOMPARE(rt.stringAt(0, 0), text);
        SAResult del = c.query(QStringLiteral("DELETE FROM items WHERE name = 'three'"));
        QCOMPARE(del.affectedRows, 1ULL);
    }

    void schemaQueries()
    {
        SAMySQLConnection c;
        SAMySQLOptions o = m_options;
        o.database = m_db;
        c.setOptions(o);
        QVERIFY(c.connect());
        auto esc = [&c](const QString &s) { return c.escapeAndQuoteString(s); };

        const QVector<SASchema::ObjectEntry> tables = SASchema::parseTables(c.query(SASchema::showFullTables()));
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables[0].name, QStringLiteral("items"));
        QCOMPARE(tables[0].type, SASchema::ObjectType::Table);

        const QVector<SASchema::Column> columns = SASchema::parseColumns(c.query(SASchema::showFullColumns("items")), c.isMariaDB());
        QCOMPARE(columns.size(), 8);
        QCOMPARE(columns[0].type, QStringLiteral("INT"));
        QVERIFY(columns[0].isUnsigned);
        QVERIFY(columns[0].isPrimary());
        QVERIFY(columns[0].isAutoIncrement());
        QCOMPARE(columns[1].length, QStringLiteral("50"));
        QVERIFY(!columns[1].nullable);
        QCOMPARE(columns[1].charset, QStringLiteral("utf8mb4"));
        QCOMPARE(columns[6].type, QStringLiteral("ENUM"));
        QCOMPARE(columns[6].length, QStringLiteral("'a','b'"));
        QCOMPARE(columns[6].defaultValue, QStringLiteral("a"));
        QVERIFY(columns[7].defaultValue.contains(QLatin1String("current_timestamp"), Qt::CaseInsensitive));
        QCOMPARE(SASchema::primaryKeyColumns(columns), QStringList{"id"});

        const QVector<SASchema::Index> indexes = SASchema::parseIndexes(c.query(SASchema::showIndex("items")));
        QCOMPARE(indexes.size(), 2);
        QCOMPARE(indexes[0].name, QStringLiteral("PRIMARY"));
        QVERIFY(indexes[0].unique);
        QCOMPARE(indexes[1].columns, QStringList{"name"});

        SAResult status = c.query(SASchema::tableStatusLike("items", esc));
        QVERIFY(status.ok);
        QCOMPARE(status.rowCount(), 1);
        QCOMPARE(status.stringAt(0, "Engine"), QStringLiteral("InnoDB"));

        // Round trip a column definition through ALTER TABLE.
        SASchema::Column col = columns[1];
        col.length = QStringLiteral("80");
        col.comment = QStringLiteral("renamed");
        QVERIFY2(c.execute(SASchema::changeColumn("items", col.name, col, "NULL", esc)), qPrintable(c.lastErrorMessage()));
        const QVector<SASchema::Column> after = SASchema::parseColumns(c.query(SASchema::showFullColumns("items")), c.isMariaDB());
        QCOMPARE(after[1].length, QStringLiteral("80"));
        QCOMPARE(after[1].comment, QStringLiteral("renamed"));

        QVERIFY(c.execute(SASchema::addIndex("items", "UNIQUE", "uq_name", {"name"}, {})));
        QVERIFY(c.execute(SASchema::dropIndex("items", "uq_name")));
        QVERIFY(c.execute(SASchema::createTrigger("trg_items", "BEFORE", "INSERT", "items", "SET NEW.name = UPPER(NEW.name)")));
        const QVector<SASchema::Trigger> triggers = SASchema::parseTriggers(c.query(SASchema::triggersFor(m_db, "items", esc)));
        QCOMPARE(triggers.size(), 1);
        QCOMPARE(triggers[0].timing, QStringLiteral("BEFORE"));
        QVERIFY(c.execute(SASchema::dropTrigger(m_db, "trg_items")));
    }

    void multiResultProcedure()
    {
        SAMySQLConnection c;
        SAMySQLOptions o = m_options;
        o.database = m_db;
        c.setOptions(o);
        QVERIFY(c.connect());
        QVERIFY2(c.execute(QStringLiteral("CREATE PROCEDURE p_two() BEGIN SELECT 1 AS a; SELECT 2 AS b; END")), qPrintable(c.lastErrorMessage()));
        SAResult r = c.query(QStringLiteral("CALL p_two()"));
        QVERIFY2(r.ok, qPrintable(r.errorMessage));
        QCOMPARE(r.stringAt(0, 0), QStringLiteral("1"));
        // The connection must be usable right away (remaining results flushed).
        SAResult next = c.query(QStringLiteral("SELECT 3"));
        QVERIFY(next.ok);
        QCOMPARE(next.firstValue(), QStringLiteral("3"));
        QVERIFY(c.execute(QStringLiteral("DROP PROCEDURE p_two")));
    }

    void killQueryFromAnotherThread()
    {
        SAMySQLConnection c;
        c.setOptions(m_options);
        QVERIFY(c.connect());
        const unsigned long threadId = c.threadId();
        QThread *killer = QThread::create([this, threadId]() {
            QThread::msleep(400);
            QString error;
            SAMySQLConnection::killQuery(m_options, threadId, &error);
        });
        killer->start();
        SAResult r = c.query(QStringLiteral("SELECT SLEEP(10)"));
        killer->wait();
        delete killer;
        // Either the statement was interrupted (MariaDB returns 1317, MySQL may return SLEEP() = 1)
        QVERIFY(r.errorNumber == 1317 || (r.ok && r.firstValue() == QLatin1String("1")));
        QVERIFY(c.isConnected());
    }

    // A client that fires several tool calls at once and drops a slow one used
    // to take the server with it: the response was written to a QTcpSocket that
    // had been deleted while the query was still in flight.
    void mcpServerSurvivesClientDisconnectMidQuery()
    {
        SAConnectionInfo info;
        info.host = m_options.host;
        info.port = QString::number(m_options.port);
        info.user = m_options.user;
        info.password = m_options.password;
        info.database = m_db;

        SADatabaseSession session;
        session.setConnectionInfo(info);
        bool done = false;
        bool connectedOk = false;
        session.connectToServer([&](bool ok, const QString &) { connectedOk = ok; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(connectedOk);

        struct SingleSession : SAMCPDataSource {
            SADatabaseSession *session = nullptr;
            SAMCPConnectionInfo info;
            QVector<SAMCPConnectionInfo> openConnections() const override { return {info}; }
            SADatabaseSession *sessionFor(const QString &) const override { return session; }
            const SAMCPConnectionInfo *connectionInfoFor(const QString &) const override { return &info; }
        } dataSource;
        dataSource.session = &session;
        dataSource.info.id = QStringLiteral("c1");
        dataSource.info.active = true;

        SAMCPServer server(&dataSource);
        QString error;
        QVERIFY2(server.start(0, &error), qPrintable(error));

        auto request = [](const QString &sql) {
            const QJsonObject rpc{
                {"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                {"params", QJsonObject{{"name", "run_query"},
                                        {"arguments", QJsonObject{{"sql", sql}, {"connection", "c1"}}}}},
            };
            const QByteArray payload = QJsonDocument(rpc).toJson(QJsonDocument::Compact);
            return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                   "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
        };

        constexpr int kClients = 3;
        QTcpSocket clients[kClients];
        for (int i = 0; i < kClients; ++i) {
            clients[i].connectToHost(QHostAddress::LocalHost, server.port());
            QVERIFY(clients[i].waitForConnected(5000));
            clients[i].write(request(QStringLiteral("SELECT SLEEP(1) AS s")));
            QVERIFY(clients[i].waitForBytesWritten(5000));
        }

        // Fired from a timer so it lands while the server is still serving the
        // first request: the client gives up, and the socket its response is
        // owed is torn down mid-query. The extra connections churn the heap so
        // a write to that freed socket hits reused memory rather than passing
        // unnoticed.
        QTcpSocket churn[8];
        QTimer::singleShot(300, [&]() {
            clients[0].abort();
            for (QTcpSocket &socket : churn) {
                socket.connectToHost(QHostAddress::LocalHost, server.port());
                socket.write("GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
            }
        });

        for (int i = 1; i < kClients; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(clients[i].bytesAvailable() > 0, 30000);
            const QByteArray response = clients[i].readAll();
            QVERIFY2(response.startsWith("HTTP/1.1 200"), response.left(64).constData());
            QVERIFY2(response.contains("\"isError\":false"), response.constData());
        }
        QVERIFY(server.isRunning());
        server.stop();
        session.disconnectFromServer();
    }

    void asyncSession()
    {
        SAConnectionInfo info;
        info.host = m_options.host;
        info.port = QString::number(m_options.port);
        info.user = m_options.user;
        info.password = m_options.password;
        info.database = m_db;

        SADatabaseSession session;
        session.setConnectionInfo(info);
        bool connectedOk = false;
        QString connectError;
        bool done = false;
        session.connectToServer([&](bool ok, const QString &error) { connectedOk = ok; connectError = error; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(connectedOk, qPrintable(connectError));
        QVERIFY(session.isConnected());
        QCOMPARE(session.currentDatabase(), m_db);
        QVERIFY(session.serverInfo().versionNumber > 0);

        QSignalSpy performed(&session, &SADatabaseSession::queryPerformed);
        SAResult result;
        done = false;
        session.query(QStringLiteral("SELECT COUNT(*) FROM items"), [&](const SAResult &r) { result = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.firstValue().toInt(), 2);
        QCOMPARE(performed.count(), 1);
        QVERIFY(!session.isBusy());

        QVector<SAResult> batch;
        done = false;
        session.queryBatch({QStringLiteral("SELECT 1"), QStringLiteral("SELECT bad syntax from"), QStringLiteral("SELECT 3")},
                           [&](const QVector<SAResult> &r) { batch = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(batch.size(), 3);
        QVERIFY(batch[0].ok);
        QVERIFY(!batch[1].ok);
        QVERIFY(batch[2].ok);

        done = false;
        bool selOk = false;
        session.selectDatabase(QStringLiteral("information_schema"), [&](bool ok, const QString &) { selOk = ok; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(selOk);
        QCOMPARE(session.currentDatabase(), QStringLiteral("information_schema"));
        session.disconnectFromServer();
        QVERIFY(!session.isConnected());
    }
};

QTEST_GUILESS_MAIN(IntegrationTests)
#include "tst_integration.moc"
