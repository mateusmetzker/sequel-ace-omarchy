//
//  SAUserManagerQueries.h
//  Sequel Ace (Linux port)
//
//  SQL statements and DDL builders for the MySQL/MariaDB account manager:
//  reading mysql.user/mysql.db, the server's supported privilege list, the
//  MariaDB global_priv JSON bitmask and the MySQL 8 global_grants table, and
//  building CREATE/ALTER/DROP USER, SET PASSWORD, GRANT/REVOKE statements.
//  Ported from SPUserManager.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace SAUserManager {

using EscapeFunction = std::function<QString(const QString &)>;   // returns a quoted, escaped literal

// One user@host row plus every privilege the server reports as supported,
// keyed the same way as the "xxx_priv" columns of mysql.user (lower-case,
// underscored, "_priv" suffix) so the same key set can drive mysql.db rows.
struct SAUserAccount {
    QString user;
    QString host;
    bool hasPassword = false;
    QString plugin;                    // authentication_string plugin, when known (post-5.7.6 servers)
    QHash<QString, bool> globalPrivs;   // e.g. "select_priv" -> true
    int maxQueries = 0;                 // MAX_QUERIES_PER_HOUR
    int maxUpdates = 0;                 // MAX_UPDATES_PER_HOUR
    int maxConnections = 0;             // MAX_CONNECTIONS_PER_HOUR
    int maxUserConnections = 0;         // MAX_USER_CONNECTIONS

    QString userHostKey() const { return user + QLatin1Char('@') + host; }
};

// mysql.db privileges for one user@host on one schema.
struct SASchemaPrivilege {
    QString user;
    QString host;
    QString database;                  // already un-escaped ("\_" -> "_")
    QHash<QString, bool> privs;
};

// ---- statement builders: reading --------------------------------------------
QString listUsers();                                    // SELECT * FROM mysql.user ORDER BY User
QString listSchemaPrivileges();                          // SELECT * FROM mysql.db
QString showPrivileges();                                // SHOW PRIVILEGES
QString showColumnsFromUserTable();                       // fallback when SHOW PRIVILEGES is unavailable
QString mariaDBGlobalPrivAccess();                        // SELECT User, Host, Priv FROM mysql.global_priv
QString mysqlGlobalGrants();                              // SELECT USER,HOST,PRIV,WITH_GRANT_OPTION FROM mysql.global_grants

// ---- statement builders: DDL --------------------------------------------------
// `existingHash` is either empty (no password), a MySQL password hash copied
// from another host of the same user, or unused when `plainPassword` is set.
QString createUser(const QString &user, const QString &host, const QString &plainPassword,
                    const QString &existingHash, const QString &plugin, bool post576, const EscapeFunction &escape);
QString dropUser(const QString &user, const QString &host, const EscapeFunction &escape);
QString renameUser(const QString &oldUser, const QString &oldHost, const QString &newUser, const QString &newHost, const EscapeFunction &escape);

// Pre-5.7.6 servers only.
QString setPasswordLegacy(const QString &user, const QString &host, const QString &plainPassword, const EscapeFunction &escape);
// Post-5.7.6 servers: one ALTER USER per plugin+password combination is enough
// even for several hosts of the same user; callers batch identical hosts.
// Each pair in `userAtHostPairs` must already be a "escape(user)@escape(host)" literal.
QString alterUserPassword(const QStringList &userAtHostPairs, const QString &plugin, const QString &plainPassword, const EscapeFunction &escape);

// Modern syntax first (ALTER USER ... WITH ...); callers fall back to
// alterUserResourcesLegacy() when the server rejects it.
QString alterUserResources(const QString &user, const QString &host, int maxQueries, int maxUpdates, int maxConnections, int maxUserConnections, const EscapeFunction &escape);
QString alterUserResourcesLegacy(const QString &user, const QString &host, int maxQueries, int maxUpdates, int maxConnections, int maxUserConnections, const EscapeFunction &escape);

// `database` empty means "*.*" (global). `privileges` are GRANT-style names
// (e.g. "SELECT", "ALL PRIVILEGES"), already translated from the internal keys.
QString grantStatement(const QStringList &privileges, const QString &database, const QString &user, const QString &host,
                       bool withGrantOption, const EscapeFunction &escape);
QString revokeStatement(const QStringList &privileges, const QString &database, const QString &user, const QString &host,
                        const EscapeFunction &escape);
QString revokeAllStatement(const QString &database, const QString &user, const QString &host, const EscapeFunction &escape);
QString revokeGrantOptionStatement(const QString &database, const QString &user, const QString &host, const EscapeFunction &escape);
QString flushPrivileges();
// Builds an "escape(user)@escape(host)" literal for use in alterUserPassword()'s list.
QString userAtHost(const QString &user, const QString &host, const EscapeFunction &escape);

// ---- result parsers ----------------------------------------------------------
QVector<SAUserAccount> parseUsers(const SAResult &result, bool post576);
QVector<SASchemaPrivilege> parseSchemaPrivileges(const SAResult &result);
// Every "xxx_priv" column name SHOW PRIVILEGES/mysql.user reports as supported,
// already lower-cased to match SAUserAccount::globalPrivs keys.
QStringList parseSupportedPrivileges(const SAResult &showPrivilegesResult);
QStringList parseSupportedPrivilegesFromColumns(const SAResult &showColumnsResult);
// MariaDB mysql.global_priv: user@host -> set of dynamic privilege keys granted via the JSON "access" bitmask.
QHash<QString, QStringList> parseMariaDBGlobalPriv(const SAResult &result);
// MySQL 8 mysql.global_grants: user@host -> set of dynamic privilege keys.
QHash<QString, QStringList> parseMySQLGlobalGrants(const SAResult &result);

// ---- naming -------------------------------------------------------------------
// Column name (e.g. "Grant_priv") -> internal key (e.g. "grant_priv") -> GRANT
// keyword (e.g. "GRANT OPTION"). Handles the handful of columns whose GRANT
// name differs from a naive underscore-to-space conversion.
QString privilegeKeyForColumn(const QString &column);         // "Select_priv" -> "select_priv"
QString grantNameForPrivilegeKey(const QString &key);          // "grant_priv" -> "GRANT OPTION"
// "CONNECTION_ADMIN" (MySQL global_grants) / camel text -> "connection_admin_priv"
QString privilegeKeyForServerPrivilegeName(const QString &name);
// Escapes "_" and "%" LIKE wildcards for storage in mysql.db.Db, and the reverse.
QString escapeSchemaWildcards(const QString &database);
QString unescapeSchemaWildcards(const QString &database);

} // namespace SAUserManager
