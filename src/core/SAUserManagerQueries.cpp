//
//  SAUserManagerQueries.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAUserManagerQueries.h"
#include "SAMySQLConnection.h"

#include <QRegularExpression>

namespace SAUserManager {

namespace {
QString q(const QString &identifier) { return SAMySQLConnection::quoteIdentifier(identifier); }

// Column name (mysql.user / mysql.db) -> GRANT-syntax privilege key, for the
// handful that do not follow the naive "lowercase + _priv" rule. Ported from
// SPUserManager.m's privColumnToGrantMap.
const QHash<QString, QString> &columnToGrantMap()
{
    static const QHash<QString, QString> map{
        {QStringLiteral("grant_priv"), QStringLiteral("grant_option_priv")},
        {QStringLiteral("show_db_priv"), QStringLiteral("show_databases_priv")},
        {QStringLiteral("create_tmp_table_priv"), QStringLiteral("create_temporary_tables_priv")},
        {QStringLiteral("repl_slave_priv"), QStringLiteral("replication_slave_priv")},
        {QStringLiteral("repl_client_priv"), QStringLiteral("replication_client_priv")},
        {QStringLiteral("replication_replica_priv"), QStringLiteral("replication_slave_priv")},
        {QStringLiteral("repl_replica_priv"), QStringLiteral("replication_slave_priv")},
        {QStringLiteral("repl_slave_admin_priv"), QStringLiteral("replication_slave_admin_priv")},
        {QStringLiteral("repl_master_admin_priv"), QStringLiteral("replication_master_admin_priv")},
        {QStringLiteral("slave_monitor_priv"), QStringLiteral("replica_monitor_priv")},
        {QStringLiteral("truncate_versioning_priv"), QStringLiteral("delete_versioning_rows_priv")},
        {QStringLiteral("delete_history_priv"), QStringLiteral("delete_versioning_rows_priv")},
        {QStringLiteral("show_create_routine_priv"), QStringLiteral("show_create_routine_priv")},
    };
    return map;
}

// MariaDB mysql.global_priv "access" bitmask -> privilege key. Ported from
// SPUserManager.m's _initializeMariaDBGlobalPrivsForChild:accessValue:.
const QHash<QString, int> &mariaDBAccessBits()
{
    static const QHash<QString, int> bits{
        {QStringLiteral("binlog_monitor_priv"), 20},
        {QStringLiteral("set_user_priv"), 30},
        {QStringLiteral("federated_admin_priv"), 31},
        {QStringLiteral("connection_admin_priv"), 32},
        {QStringLiteral("read_only_admin_priv"), 33},
        {QStringLiteral("replication_slave_admin_priv"), 34},
        {QStringLiteral("replication_master_admin_priv"), 35},
        {QStringLiteral("binlog_admin_priv"), 36},
        {QStringLiteral("binlog_replay_priv"), 37},
        {QStringLiteral("replica_monitor_priv"), 38},
        {QStringLiteral("show_create_routine_priv"), 39},
    };
    return bits;
}
}

QString listUsers() { return QStringLiteral("SELECT * FROM mysql.user ORDER BY User"); }
QString listSchemaPrivileges() { return QStringLiteral("SELECT * FROM mysql.db"); }
QString showPrivileges() { return QStringLiteral("SHOW PRIVILEGES"); }
QString showColumnsFromUserTable() { return QStringLiteral("SHOW COLUMNS FROM mysql.user"); }
QString mariaDBGlobalPrivAccess() { return QStringLiteral("SELECT User, Host, Priv FROM mysql.global_priv"); }
QString mysqlGlobalGrants() { return QStringLiteral("SELECT USER,HOST,PRIV,WITH_GRANT_OPTION FROM mysql.global_grants"); }

QString createUser(const QString &user, const QString &host, const QString &plainPassword,
                    const QString &existingHash, const QString &plugin, bool post576, const EscapeFunction &escape)
{
    const QString target = QStringLiteral("%1@%2").arg(escape(user), escape(host));
    if (!plainPassword.isEmpty()) {
        if (post576 && !plugin.isEmpty())
            return QStringLiteral("CREATE USER %1 IDENTIFIED WITH %2 BY %3").arg(target, plugin, escape(plainPassword));
        return QStringLiteral("CREATE USER %1 IDENTIFIED BY %2").arg(target, escape(plainPassword));
    }
    if (!existingHash.isEmpty()) {
        if (post576 && !plugin.isEmpty())
            return QStringLiteral("CREATE USER %1 IDENTIFIED WITH %2 AS %3").arg(target, plugin, escape(existingHash));
        return QStringLiteral("CREATE USER %1 IDENTIFIED BY PASSWORD %2").arg(target, escape(existingHash));
    }
    return QStringLiteral("CREATE USER %1").arg(target);
}

QString dropUser(const QString &user, const QString &host, const EscapeFunction &escape)
{
    return QStringLiteral("DROP USER %1@%2").arg(escape(user), escape(host));
}

QString renameUser(const QString &oldUser, const QString &oldHost, const QString &newUser, const QString &newHost, const EscapeFunction &escape)
{
    return QStringLiteral("RENAME USER %1@%2 TO %3@%4").arg(escape(oldUser), escape(oldHost), escape(newUser), escape(newHost));
}

QString setPasswordLegacy(const QString &user, const QString &host, const QString &plainPassword, const EscapeFunction &escape)
{
    return QStringLiteral("SET PASSWORD FOR %1@%2 = PASSWORD(%3)").arg(escape(user), escape(host), escape(plainPassword));
}

QString userAtHost(const QString &user, const QString &host, const EscapeFunction &escape)
{
    return QStringLiteral("%1@%2").arg(escape(user), escape(host));
}

QString alterUserPassword(const QStringList &userAtHostPairs, const QString &plugin, const QString &plainPassword, const EscapeFunction &escape)
{
    QStringList targets;
    for (const QString &pair : userAtHostPairs)
        targets << QStringLiteral("%1 IDENTIFIED WITH %2 BY %3").arg(pair, plugin, escape(plainPassword));
    return QStringLiteral("ALTER USER %1").arg(targets.join(QStringLiteral(", ")));
}

QString alterUserResources(const QString &user, const QString &host, int maxQueries, int maxUpdates, int maxConnections, int maxUserConnections, const EscapeFunction &escape)
{
    return QStringLiteral("ALTER USER %1@%2 WITH MAX_QUERIES_PER_HOUR %3 MAX_UPDATES_PER_HOUR %4 "
                          "MAX_CONNECTIONS_PER_HOUR %5 MAX_USER_CONNECTIONS %6")
        .arg(escape(user), escape(host)).arg(maxQueries).arg(maxUpdates).arg(maxConnections).arg(maxUserConnections);
}

QString alterUserResourcesLegacy(const QString &user, const QString &host, int maxQueries, int maxUpdates, int maxConnections, int maxUserConnections, const EscapeFunction &escape)
{
    return QStringLiteral("UPDATE mysql.user SET max_questions=%1, max_updates=%2, max_connections=%3, "
                          "max_user_connections=%4 WHERE User=%5 AND Host=%6")
        .arg(maxQueries).arg(maxUpdates).arg(maxConnections).arg(maxUserConnections).arg(escape(user), escape(host));
}

QString grantStatement(const QStringList &privileges, const QString &database, const QString &user, const QString &host,
                       bool withGrantOption, const EscapeFunction &escape)
{
    const QString on = database.isEmpty() ? QStringLiteral("*.*") : QStringLiteral("%1.*").arg(q(database));
    QString sql = QStringLiteral("GRANT %1 ON %2 TO %3@%4").arg(privileges.join(QStringLiteral(", ")), on, escape(user), escape(host));
    if (withGrantOption) sql += QStringLiteral(" WITH GRANT OPTION");
    return sql;
}

QString revokeStatement(const QStringList &privileges, const QString &database, const QString &user, const QString &host,
                        const EscapeFunction &escape)
{
    const QString on = database.isEmpty() ? QStringLiteral("*.*") : QStringLiteral("%1.*").arg(q(database));
    return QStringLiteral("REVOKE %1 ON %2 FROM %3@%4").arg(privileges.join(QStringLiteral(", ")), on, escape(user), escape(host));
}

QString revokeAllStatement(const QString &database, const QString &user, const QString &host, const EscapeFunction &escape)
{
    const QString on = database.isEmpty() ? QStringLiteral("*.*") : QStringLiteral("%1.*").arg(q(database));
    return QStringLiteral("REVOKE ALL PRIVILEGES ON %1 FROM %2@%3").arg(on, escape(user), escape(host));
}

QString revokeGrantOptionStatement(const QString &database, const QString &user, const QString &host, const EscapeFunction &escape)
{
    const QString on = database.isEmpty() ? QStringLiteral("*.*") : QStringLiteral("%1.*").arg(q(database));
    return QStringLiteral("REVOKE GRANT OPTION ON %1 FROM %2@%3").arg(on, escape(user), escape(host));
}

QString flushPrivileges() { return QStringLiteral("FLUSH PRIVILEGES"); }

QString privilegeKeyForColumn(const QString &column)
{
    QString key = column.toLower();
    return columnToGrantMap().value(key, key);
}

QString grantNameForPrivilegeKey(const QString &key)
{
    QString name = key;
    if (name.endsWith(QLatin1String("_priv"))) name.chop(5);
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.toUpper();
}

QString privilegeKeyForServerPrivilegeName(const QString &name)
{
    QString key = name.toLower();
    key.replace(QLatin1Char(' '), QLatin1Char('_'));
    key += QStringLiteral("_priv");
    // The server-reported name may already be a synonym column name (e.g. "grant"),
    // so route it through the same remapping table as columns.
    return columnToGrantMap().value(key, key);
}

QString escapeSchemaWildcards(const QString &database)
{
    QString escaped = database;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    escaped.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    return escaped;
}

QString unescapeSchemaWildcards(const QString &database)
{
    QString plain = database;
    plain.replace(QStringLiteral("\\_"), QStringLiteral("_"));
    plain.replace(QStringLiteral("\\%"), QStringLiteral("%"));
    plain.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return plain;
}

QVector<SAUserAccount> parseUsers(const SAResult &result, bool post576)
{
    QVector<SAUserAccount> accounts;
    if (!result.ok) return accounts;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QMap<QString, QString> row = result.rowAsMap(r);
        SAUserAccount account;
        account.user = row.value(QStringLiteral("User"));
        account.host = row.value(QStringLiteral("Host"));
        if (post576) {
            account.plugin = row.value(QStringLiteral("plugin"));
            account.hasPassword = !row.value(QStringLiteral("authentication_string")).isEmpty();
        } else {
            account.hasPassword = !row.value(QStringLiteral("Password")).isEmpty();
        }
        account.maxQueries = row.value(QStringLiteral("max_questions")).toInt();
        account.maxUpdates = row.value(QStringLiteral("max_updates")).toInt();
        account.maxConnections = row.value(QStringLiteral("max_connections")).toInt();
        account.maxUserConnections = row.value(QStringLiteral("max_user_connections")).toInt();
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            if (!it.key().endsWith(QLatin1String("_priv"), Qt::CaseInsensitive)) continue;
            const QString key = privilegeKeyForColumn(it.key());
            account.globalPrivs[key] = it.value().compare(QLatin1String("Y"), Qt::CaseInsensitive) == 0;
        }
        accounts.append(account);
    }
    return accounts;
}

QVector<SASchemaPrivilege> parseSchemaPrivileges(const SAResult &result)
{
    QVector<SASchemaPrivilege> privileges;
    if (!result.ok) return privileges;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QMap<QString, QString> row = result.rowAsMap(r);
        SASchemaPrivilege priv;
        priv.user = row.value(QStringLiteral("User"));
        priv.host = row.value(QStringLiteral("Host"));
        priv.database = unescapeSchemaWildcards(row.value(QStringLiteral("Db")));
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            if (!it.key().endsWith(QLatin1String("_priv"), Qt::CaseInsensitive)) continue;
            const QString key = privilegeKeyForColumn(it.key());
            priv.privs[key] = it.value().compare(QLatin1String("Y"), Qt::CaseInsensitive) == 0;
        }
        privileges.append(priv);
    }
    return privileges;
}

QStringList parseSupportedPrivileges(const SAResult &showPrivilegesResult)
{
    QStringList keys;
    if (!showPrivilegesResult.ok) return keys;
    for (int r = 0; r < showPrivilegesResult.rowCount(); ++r)
        keys << privilegeKeyForServerPrivilegeName(showPrivilegesResult.stringAt(r, 0));
    return keys;
}

QStringList parseSupportedPrivilegesFromColumns(const SAResult &showColumnsResult)
{
    QStringList keys;
    if (!showColumnsResult.ok) return keys;
    for (int r = 0; r < showColumnsResult.rowCount(); ++r) {
        const QString field = showColumnsResult.stringAt(r, 0);
        if (field.endsWith(QLatin1String("_priv"), Qt::CaseInsensitive)) keys << privilegeKeyForColumn(field);
    }
    return keys;
}

QHash<QString, QStringList> parseMariaDBGlobalPriv(const SAResult &result)
{
    QHash<QString, QStringList> byAccount;
    if (!result.ok) return byAccount;
    static const QRegularExpression accessRe(QStringLiteral("\"access\"\\s*:\\s*(\\d+)"));
    for (int r = 0; r < result.rowCount(); ++r) {
        const QString user = result.stringAt(r, 0);
        const QString host = result.stringAt(r, 1);
        const QString priv = result.stringAt(r, 2);
        const QRegularExpressionMatch match = accessRe.match(priv);
        if (!match.hasMatch()) continue;
        const qulonglong access = match.captured(1).toULongLong();
        QStringList keys;
        for (auto it = mariaDBAccessBits().constBegin(); it != mariaDBAccessBits().constEnd(); ++it)
            if ((access & (1ULL << it.value())) != 0) keys << it.key();
        if (!keys.isEmpty()) byAccount.insert(user + QLatin1Char('@') + host, keys);
    }
    return byAccount;
}

QHash<QString, QStringList> parseMySQLGlobalGrants(const SAResult &result)
{
    QHash<QString, QStringList> byAccount;
    if (!result.ok) return byAccount;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QString user = result.stringAt(r, 0);
        const QString host = result.stringAt(r, 1);
        const QString priv = privilegeKeyForServerPrivilegeName(result.stringAt(r, 2));
        const QString accountKey = user + QLatin1Char('@') + host;
        byAccount[accountKey].append(priv);
    }
    return byAccount;
}

} // namespace SAUserManager
