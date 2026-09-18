//
//  SASchemaQueries.h
//  Sequel Ace (Linux port)
//
//  SQL statements the schema views issue (tables list, structure, indexes,
//  relations, triggers, table status) and parsers for their results, plus the
//  DDL builders ported from SPTableStructure, SPTableRelations, SPTableTriggers
//  and SPTablesList.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace SASchema {

enum class ObjectType { None, Table, View, Procedure, Function, Event };

struct ObjectEntry {
    QString name;
    ObjectType type = ObjectType::None;
    QString comment;
};

struct Column {
    QString name;
    QString fullType;        // as shown by SHOW COLUMNS, e.g. "int(11) unsigned"
    QString type;            // upper-case base type, e.g. "INT"
    QString length;          // "11", "10,2", "'a','b'"
    bool isUnsigned = false;
    bool isZerofill = false;
    bool isBinary = false;
    QString collation;       // may be empty
    QString charset;         // derived from the collation
    bool nullable = true;
    QString key;             // "PRI", "UNI", "MUL" or ""
    QString defaultValue;    // literal text; empty when none
    bool defaultIsNull = false;
    bool hasDefault = false;
    QString extra;           // "auto_increment", "on update CURRENT_TIMESTAMP", "VIRTUAL GENERATED", ...
    QString privileges;
    QString comment;
    QString typeGroup;       // see SASQLTypes
    bool isAutoIncrement() const { return extra.contains(QLatin1String("auto_increment"), Qt::CaseInsensitive); }
    bool isGenerated() const { return extra.contains(QLatin1String("GENERATED"), Qt::CaseInsensitive); }
    bool isPrimary() const { return key == QLatin1String("PRI"); }
    bool isBlobOrText() const { return typeGroup == QLatin1String("textdata") || typeGroup == QLatin1String("blobdata"); }
};

struct Index {
    QString name;
    bool unique = false;
    QString type;            // BTREE, HASH, FULLTEXT, SPATIAL
    QStringList columns;
    QStringList subParts;    // parallel to columns; empty string when none
    QString comment;
    bool isPrimary() const { return name == QLatin1String("PRIMARY"); }
};

struct ForeignKey {
    QString name;
    QStringList columns;
    QString referencedDatabase;
    QString referencedTable;
    QStringList referencedColumns;
    QString onUpdate;
    QString onDelete;
};

struct Trigger {
    QString name;
    QString event;           // INSERT, UPDATE, DELETE
    QString table;
    QString statement;
    QString timing;          // BEFORE, AFTER
    QString created;
    QString sqlMode;
    QString definer;
    QString charset;
    QString collation;
    QString databaseCollation;
};

using EscapeFunction = std::function<QString(const QString &)>;   // returns a quoted, escaped literal

// ---- statement builders: reading -------------------------------------------
QString showDatabases();
QString showFullTables();
QString showTableStatus();
QString routinesForDatabase(const QString &database, const EscapeFunction &escape);
QString showFullColumns(const QString &table);
QString showFullColumns(const QString &database, const QString &table);
QString showIndex(const QString &table);
QString showCreate(ObjectType type, const QString &name);
QString showCreate(ObjectType type, const QString &database, const QString &name);
QString tableStatusLike(const QString &table, const EscapeFunction &escape);
QString foreignKeysFor(const QString &database, const QString &table, const EscapeFunction &escape);
QString triggersFor(const QString &database, const QString &table, const EscapeFunction &escape);
QString viewDefinition(const QString &database, const QString &view, const EscapeFunction &escape);
QString routineDefinition(ObjectType type, const QString &database, const QString &name, const EscapeFunction &escape);
QString countRows(const QString &table);
QString storageEngines();
QString characterSets();
QString collations();
QString collationsForCharset(const QString &charset, const EscapeFunction &escape);
QString innodbTablesInDatabase(const QString &database, const EscapeFunction &escape);

// ---- result parsers ----------------------------------------------------------
QVector<ObjectEntry> parseTables(const SAResult &result);       // SHOW FULL TABLES or SHOW TABLE STATUS
QVector<ObjectEntry> parseRoutines(const SAResult &result);     // information_schema.ROUTINES
QVector<Column> parseColumns(const SAResult &result, bool serverIsMariaDB);
QVector<Index> parseIndexes(const SAResult &result);
QVector<ForeignKey> parseForeignKeys(const SAResult &result);
QVector<Trigger> parseTriggers(const SAResult &result);
QStringList primaryKeyColumns(const QVector<Column> &columns);
QStringList systemDatabases();

// ---- statement builders: DDL --------------------------------------------------
// Port of -[SPTableStructure _buildPartialColumnDefinitionString:].
QString columnDefinition(const Column &column, const QString &nullValueString, const EscapeFunction &escape);
QString addColumn(const QString &table, const Column &column, const QString &afterColumn, const QString &nullValueString, const EscapeFunction &escape);
QString changeColumn(const QString &table, const QString &oldName, const Column &column, const QString &nullValueString, const EscapeFunction &escape);
QString dropColumn(const QString &table, const QString &column);
QString addIndex(const QString &table, const QString &indexType, const QString &name, const QStringList &columns, const QStringList &subParts, const QString &storageType = QString());
QString dropIndex(const QString &table, const QString &name);
QString addForeignKey(const QString &table, const QString &name, const QStringList &columns, const QString &refDatabase,
                      const QString &refTable, const QStringList &refColumns, const QString &onDelete, const QString &onUpdate);
QString dropForeignKey(const QString &table, const QString &name);
QString createTrigger(const QString &name, const QString &timing, const QString &event, const QString &table, const QString &statement);
QString dropTrigger(const QString &database, const QString &name);
QString createTable(const QString &name, const QString &engine, const QString &charset, const QString &collation);
QString createView(const QString &name, const QString &selectStatement);
QString renameObject(ObjectType type, const QString &oldName, const QString &newName);
QString truncateTable(const QString &table);
QString dropObject(ObjectType type, const QString &name, bool ifExists = false);
QString duplicateTable(const QString &source, const QString &target, bool copyContent);
QString createDatabase(const QString &name, const QString &charset, const QString &collation);
QString dropDatabase(const QString &name);
QString alterDatabase(const QString &name, const QString &charset, const QString &collation);
QString maintenance(const QString &command, const QStringList &tables);   // CHECK/REPAIR/ANALYZE/OPTIMIZE/FLUSH/CHECKSUM
QString alterTableOption(const QString &table, const QString &option, const QString &value, bool quoteValue, const EscapeFunction &escape);
QString setAutoIncrement(const QString &table, quint64 value);
QString killQuery(quint64 id, bool tidb);
QString killConnection(quint64 id, bool tidb);

QString objectTypeName(ObjectType type);

} // namespace SASchema
