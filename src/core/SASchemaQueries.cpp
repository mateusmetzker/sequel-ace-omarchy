//
//  SASchemaQueries.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASchemaQueries.h"
#include "SAMySQLConnection.h"
#include "SASQLTypes.h"

#include <QRegularExpression>

namespace SASchema {

namespace {
QString q(const QString &identifier) { return SAMySQLConnection::quoteIdentifier(identifier); }
QString qq(const QString &database, const QString &name) { return q(database) + QLatin1Char('.') + q(name); }
}

QString objectTypeName(ObjectType type)
{
    switch (type) {
    case ObjectType::Table: return QStringLiteral("TABLE");
    case ObjectType::View: return QStringLiteral("VIEW");
    case ObjectType::Procedure: return QStringLiteral("PROCEDURE");
    case ObjectType::Function: return QStringLiteral("FUNCTION");
    case ObjectType::Event: return QStringLiteral("EVENT");
    default: return QString();
    }
}

// ---- reading -----------------------------------------------------------------

QString showDatabases() { return QStringLiteral("SHOW DATABASES"); }
QString showFullTables() { return QStringLiteral("SHOW FULL TABLES"); }
QString showTableStatus() { return QStringLiteral("SHOW TABLE STATUS"); }

QString routinesForDatabase(const QString &database, const EscapeFunction &escape)
{
    return QStringLiteral("SELECT SPECIFIC_NAME, ROUTINE_TYPE, ROUTINE_COMMENT FROM information_schema.ROUTINES "
                          "WHERE ROUTINE_SCHEMA = %1 ORDER BY SPECIFIC_NAME").arg(escape(database));
}

QString showFullColumns(const QString &table) { return QStringLiteral("SHOW FULL COLUMNS FROM %1").arg(q(table)); }
QString showFullColumns(const QString &database, const QString &table)
{
    return QStringLiteral("SHOW FULL COLUMNS FROM %1").arg(qq(database, table));
}
QString showIndex(const QString &table) { return QStringLiteral("SHOW INDEX FROM %1").arg(q(table)); }

QString showCreate(ObjectType type, const QString &name)
{
    return QStringLiteral("SHOW CREATE %1 %2").arg(objectTypeName(type), q(name));
}
QString showCreate(ObjectType type, const QString &database, const QString &name)
{
    return QStringLiteral("SHOW CREATE %1 %2").arg(objectTypeName(type), qq(database, name));
}

QString tableStatusLike(const QString &table, const EscapeFunction &escape)
{
    return QStringLiteral("SHOW TABLE STATUS LIKE %1").arg(escape(SAMySQLConnection::escapeLikePattern(table)));
}

QString foreignKeysFor(const QString &database, const QString &table, const EscapeFunction &escape)
{
    return QStringLiteral(
        "SELECT k.CONSTRAINT_NAME, k.COLUMN_NAME, k.REFERENCED_TABLE_SCHEMA, k.REFERENCED_TABLE_NAME, "
        "k.REFERENCED_COLUMN_NAME, r.UPDATE_RULE, r.DELETE_RULE, k.ORDINAL_POSITION "
        "FROM information_schema.KEY_COLUMN_USAGE k "
        "JOIN information_schema.REFERENTIAL_CONSTRAINTS r "
        "  ON r.CONSTRAINT_SCHEMA = k.CONSTRAINT_SCHEMA AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME AND r.TABLE_NAME = k.TABLE_NAME "
        "WHERE k.TABLE_SCHEMA = %1 AND k.TABLE_NAME = %2 AND k.REFERENCED_TABLE_NAME IS NOT NULL "
        "ORDER BY k.CONSTRAINT_NAME, k.ORDINAL_POSITION").arg(escape(database), escape(table));
}

QString triggersFor(const QString &database, const QString &table, const EscapeFunction &escape)
{
    return QStringLiteral(
        "SELECT TRIGGER_NAME, EVENT_MANIPULATION, EVENT_OBJECT_TABLE, ACTION_STATEMENT, ACTION_TIMING, CREATED, "
        "SQL_MODE, DEFINER, CHARACTER_SET_CLIENT, COLLATION_CONNECTION, DATABASE_COLLATION "
        "FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA = %1 AND EVENT_OBJECT_TABLE = %2 "
        "ORDER BY EVENT_OBJECT_TABLE, ACTION_TIMING, EVENT_MANIPULATION, ACTION_ORDER").arg(escape(database), escape(table));
}

QString viewDefinition(const QString &database, const QString &view, const EscapeFunction &escape)
{
    return QStringLiteral("SELECT * FROM information_schema.VIEWS WHERE TABLE_SCHEMA = %1 AND TABLE_NAME = %2")
        .arg(escape(database), escape(view));
}

QString routineDefinition(ObjectType type, const QString &database, const QString &name, const EscapeFunction &escape)
{
    return QStringLiteral("SELECT * FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA = %1 AND SPECIFIC_NAME = %2 AND ROUTINE_TYPE = %3")
        .arg(escape(database), escape(name), escape(objectTypeName(type)));
}

QString countRows(const QString &table) { return QStringLiteral("SELECT COUNT(1) FROM %1").arg(q(table)); }

QString storageEngines()
{
    return QStringLiteral("SELECT Engine, Support, Comment FROM information_schema.ENGINES "
                          "WHERE SUPPORT IN ('DEFAULT', 'YES') AND Engine <> 'PERFORMANCE_SCHEMA' ORDER BY Engine");
}
QString characterSets()
{
    return QStringLiteral("SELECT CHARACTER_SET_NAME, DEFAULT_COLLATE_NAME, DESCRIPTION, MAXLEN FROM information_schema.CHARACTER_SETS ORDER BY CHARACTER_SET_NAME");
}
QString collations()
{
    return QStringLiteral("SELECT COLLATION_NAME, CHARACTER_SET_NAME, IS_DEFAULT FROM information_schema.COLLATIONS ORDER BY COLLATION_NAME");
}
QString collationsForCharset(const QString &charset, const EscapeFunction &escape)
{
    return QStringLiteral("SELECT COLLATION_NAME, CHARACTER_SET_NAME, IS_DEFAULT FROM information_schema.COLLATIONS WHERE CHARACTER_SET_NAME = %1 ORDER BY COLLATION_NAME").arg(escape(charset));
}
QString innodbTablesInDatabase(const QString &database, const EscapeFunction &escape)
{
    return QStringLiteral("SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_TYPE = 'BASE TABLE' AND ENGINE = 'InnoDB' AND TABLE_SCHEMA = %1 ORDER BY TABLE_NAME").arg(escape(database));
}

// ---- parsers -----------------------------------------------------------------

QVector<ObjectEntry> parseTables(const SAResult &result)
{
    QVector<ObjectEntry> entries;
    if (!result.ok) return entries;
    const int nameCol = 0;
    int typeCol = -1;
    int commentCol = -1;
    for (int i = 0; i < result.fieldCount(); ++i) {
        const QString name = result.fields.at(i).name;
        if (name == QLatin1String("Table_type")) typeCol = i;
        else if (name == QLatin1String("Comment")) commentCol = i;
    }
    for (int r = 0; r < result.rowCount(); ++r) {
        ObjectEntry e;
        e.name = result.stringAt(r, nameCol);
        if (typeCol >= 0) {
            e.type = result.stringAt(r, typeCol).compare(QLatin1String("VIEW"), Qt::CaseInsensitive) == 0 ? ObjectType::View : ObjectType::Table;
        } else if (commentCol >= 0) {
            // SHOW TABLE STATUS reports views with Comment = "VIEW" and NULL engine.
            const QString comment = result.stringAt(r, commentCol);
            const bool isView = comment == QLatin1String("VIEW") && result.stringAt(r, QStringLiteral("Engine")).isEmpty();
            e.type = isView ? ObjectType::View : ObjectType::Table;
            if (!isView) e.comment = comment;
        } else {
            e.type = ObjectType::Table;
        }
        entries.append(e);
    }
    return entries;
}

QVector<ObjectEntry> parseRoutines(const SAResult &result)
{
    QVector<ObjectEntry> entries;
    if (!result.ok) return entries;
    for (int r = 0; r < result.rowCount(); ++r) {
        ObjectEntry e;
        e.name = result.stringAt(r, 0);
        e.type = result.stringAt(r, 1).compare(QLatin1String("PROCEDURE"), Qt::CaseInsensitive) == 0 ? ObjectType::Procedure : ObjectType::Function;
        if (result.fieldCount() > 2) e.comment = result.stringAt(r, 2);
        entries.append(e);
    }
    return entries;
}

QVector<Column> parseColumns(const SAResult &result, bool serverIsMariaDB)
{
    QVector<Column> columns;
    if (!result.ok) return columns;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QMap<QString, QString> row = result.rowAsMap(r);
        Column c;
        c.name = row.value(QStringLiteral("Field"));
        c.fullType = row.value(QStringLiteral("Type"));
        const SASQLTypes::ParsedColumnType parsed = SASQLTypes::parseColumnType(c.fullType);
        c.type = parsed.name;
        c.length = parsed.length;
        c.isUnsigned = parsed.isUnsigned;
        c.isZerofill = parsed.isZerofill;
        c.isBinary = parsed.isBinary;
        c.collation = row.value(QStringLiteral("Collation"));
        if (!c.collation.isEmpty()) c.charset = c.collation.section(QLatin1Char('_'), 0, 0);
        c.nullable = row.value(QStringLiteral("Null")).compare(QLatin1String("YES"), Qt::CaseInsensitive) == 0;
        c.key = row.value(QStringLiteral("Key"));
        const int defaultCol = result.fieldIndex(QStringLiteral("Default"));
        if (defaultCol >= 0) {
            const SACell &cell = result.cell(r, defaultCol);
            if (cell.isNull) {
                c.defaultIsNull = true;
                c.hasDefault = c.nullable;   // "DEFAULT NULL" and "no default" are indistinguishable here
            } else {
                c.hasDefault = true;
                c.defaultValue = cell.toString();
                // MariaDB 10.2.7+ quotes literal defaults ('abc') and reports NULL as the word NULL.
                if (serverIsMariaDB) {
                    if (c.defaultValue == QLatin1String("NULL")) {
                        c.defaultIsNull = true;
                        c.defaultValue.clear();
                    } else if (c.defaultValue.size() >= 2 && c.defaultValue.startsWith(QLatin1Char('\'')) && c.defaultValue.endsWith(QLatin1Char('\''))) {
                        c.defaultValue = c.defaultValue.mid(1, c.defaultValue.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
                    }
                }
            }
        }
        c.extra = row.value(QStringLiteral("Extra"));
        c.privileges = row.value(QStringLiteral("Privileges"));
        c.comment = row.value(QStringLiteral("Comment"));
        c.typeGroup = SASQLTypes::typeGroupForTypeName(c.type);
        columns.append(c);
    }
    return columns;
}

QVector<Index> parseIndexes(const SAResult &result)
{
    QVector<Index> indexes;
    if (!result.ok) return indexes;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QMap<QString, QString> row = result.rowAsMap(r);
        const QString name = row.value(QStringLiteral("Key_name"));
        Index *index = nullptr;
        for (Index &existing : indexes)
            if (existing.name == name) { index = &existing; break; }
        if (!index) {
            Index fresh;
            fresh.name = name;
            fresh.unique = row.value(QStringLiteral("Non_unique")) == QLatin1String("0");
            fresh.type = row.value(QStringLiteral("Index_type"));
            fresh.comment = row.value(QStringLiteral("Index_comment"));
            indexes.append(fresh);
            index = &indexes.last();
        }
        index->columns << row.value(QStringLiteral("Column_name"));
        index->subParts << row.value(QStringLiteral("Sub_part"));
    }
    return indexes;
}

QVector<ForeignKey> parseForeignKeys(const SAResult &result)
{
    QVector<ForeignKey> keys;
    if (!result.ok) return keys;
    for (int r = 0; r < result.rowCount(); ++r) {
        const QString name = result.stringAt(r, 0);
        ForeignKey *key = nullptr;
        for (ForeignKey &existing : keys)
            if (existing.name == name) { key = &existing; break; }
        if (!key) {
            ForeignKey fresh;
            fresh.name = name;
            fresh.referencedDatabase = result.stringAt(r, 2);
            fresh.referencedTable = result.stringAt(r, 3);
            fresh.onUpdate = result.stringAt(r, 5);
            fresh.onDelete = result.stringAt(r, 6);
            keys.append(fresh);
            key = &keys.last();
        }
        key->columns << result.stringAt(r, 1);
        key->referencedColumns << result.stringAt(r, 4);
    }
    return keys;
}

QVector<Trigger> parseTriggers(const SAResult &result)
{
    QVector<Trigger> triggers;
    if (!result.ok) return triggers;
    for (int r = 0; r < result.rowCount(); ++r) {
        Trigger t;
        t.name = result.stringAt(r, 0);
        t.event = result.stringAt(r, 1);
        t.table = result.stringAt(r, 2);
        t.statement = result.stringAt(r, 3);
        t.timing = result.stringAt(r, 4);
        t.created = result.stringAt(r, 5);
        t.sqlMode = result.stringAt(r, 6);
        t.definer = result.stringAt(r, 7);
        t.charset = result.stringAt(r, 8);
        t.collation = result.stringAt(r, 9);
        t.databaseCollation = result.stringAt(r, 10);
        triggers.append(t);
    }
    return triggers;
}

QStringList primaryKeyColumns(const QVector<Column> &columns)
{
    QStringList names;
    for (const Column &c : columns)
        if (c.isPrimary()) names << c.name;
    return names;
}

QStringList systemDatabases()
{
    return {QStringLiteral("information_schema"), QStringLiteral("performance_schema"), QStringLiteral("mysql"), QStringLiteral("sys")};
}

// ---- DDL builders ------------------------------------------------------------

QString columnDefinition(const Column &column, const QString &nullValueString, const EscapeFunction &escape)
{
    // Port of -[SPTableStructure _buildPartialColumnDefinitionString:].
    const QString type = column.type.trimmed().toUpper();
    const QString extra = column.extra.trimmed().toUpper();
    const bool isGenerated = extra.contains(QLatin1String("GENERATED"));
    bool fieldDefIncludesLength = false;

    QString def = q(column.name) + QLatin1Char(' ') + type;

    if (type == QLatin1String("SERIAL")) {
        return def + (column.comment.isEmpty() ? QString() : QStringLiteral(" COMMENT %1").arg(escape(column.comment)));
    }
    if (type == QLatin1String("BOOL") || type == QLatin1String("BOOLEAN")) {
        def += column.nullable ? QStringLiteral(" NULL") : QStringLiteral(" NOT NULL");
        if (column.defaultIsNull || column.defaultValue == nullValueString) {
            if (column.nullable) def += QStringLiteral(" DEFAULT NULL");
        } else if (!column.defaultValue.isEmpty()) {
            def += QStringLiteral(" DEFAULT %1").arg(escape(column.defaultValue));
        }
        if (!column.comment.isEmpty()) def += QStringLiteral(" COMMENT %1").arg(escape(column.comment));
        return def;
    }

    if (!column.length.trimmed().isEmpty()) {
        fieldDefIncludesLength = true;
        def += QStringLiteral("(%1)").arg(column.length.trimmed());
    }

    if (type == QLatin1String("JSON") || type == QLatin1String("UUID") || type == QLatin1String("INET4") || type == QLatin1String("INET6")) {
        // No CHARACTER SET / BINARY / COLLATE for these.
    } else if (SASQLTypes::isStringType(type)) {
        if (!column.charset.isEmpty()) def += QStringLiteral(" CHARACTER SET %1").arg(column.charset);
        if (column.isBinary) def += QStringLiteral(" BINARY");
        else if (!column.collation.isEmpty()) def += QStringLiteral(" COLLATE %1").arg(column.collation);
    } else if (SASQLTypes::isNumericType(type) && type != QLatin1String("BIT")) {
        if (column.isUnsigned) def += QStringLiteral(" UNSIGNED");
        if (column.isZerofill) def += QStringLiteral(" ZEROFILL");
    }

    if (isGenerated) {
        // GENERATED ALWAYS AS (expr) VIRTUAL|STORED — the expression lives in defaultValue.
        const QString kind = extra.startsWith(QLatin1String("STORED")) ? QStringLiteral("STORED") : QStringLiteral("VIRTUAL");
        QString expression = column.defaultValue.trimmed();
        if (!expression.startsWith(QLatin1Char('('))) expression = QLatin1Char('(') + expression + QLatin1Char(')');
        def += QStringLiteral(" GENERATED ALWAYS AS %1 %2").arg(expression, kind);
        if (!column.comment.isEmpty()) def += QStringLiteral(" COMMENT %1").arg(escape(column.comment));
        return def;
    }

    def += column.nullable ? QStringLiteral(" NULL") : QStringLiteral(" NOT NULL");

    if (!extra.contains(QLatin1String("AUTO_INCREMENT"))) {
        const QString defaultValue = column.defaultValue;
        if (column.defaultIsNull || defaultValue == nullValueString) {
            if (column.nullable) def += QStringLiteral(" DEFAULT NULL");
        } else if (!defaultValue.isEmpty()) {
            static const QRegularExpression currentTimestamp(QStringLiteral("^CURRENT_TIMESTAMP(?:\\((\\d*)\\))?$"), QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch match = currentTimestamp.match(defaultValue.trimmed());
            const QString trimmed = defaultValue.trimmed();
            const bool isExpression = trimmed.endsWith(QLatin1Char(')')) && trimmed.count(QLatin1Char('(')) == trimmed.count(QLatin1Char(')'));
            const bool isQuoted = trimmed.size() >= 2 && ((trimmed.startsWith(QLatin1Char('\'')) && trimmed.endsWith(QLatin1Char('\'')))
                                                          || (trimmed.startsWith(QLatin1Char('"')) && trimmed.endsWith(QLatin1Char('"'))));
            if ((type == QLatin1String("TIMESTAMP") || type == QLatin1String("DATETIME")) && match.hasMatch()) {
                def += QStringLiteral(" DEFAULT CURRENT_TIMESTAMP");
                const QString userLen = match.captured(1);
                if (!userLen.isEmpty() || fieldDefIncludesLength) def += QStringLiteral("(%1)").arg(userLen.isEmpty() ? column.length.trimmed() : userLen);
            } else if (type == QLatin1String("BIT")) {
                def += QStringLiteral(" DEFAULT %1").arg(defaultValue);
            } else if (type.endsWith(QLatin1String("CHAR")) || type.endsWith(QLatin1String("TEXT")) || type.endsWith(QLatin1String("ENUM"))
                       || type == QLatin1String("SET") || type == QLatin1String("TIMESTAMP") || type == QLatin1String("DATETIME")
                       || type == QLatin1String("DATE") || type == QLatin1String("INET4") || type == QLatin1String("INET6") || type == QLatin1String("JSON")) {
                if (!isExpression && !isQuoted) def += QStringLiteral(" DEFAULT %1").arg(escape(defaultValue));
                else def += QStringLiteral(" DEFAULT %1").arg(defaultValue);
            } else {
                def += QStringLiteral(" DEFAULT %1").arg(defaultValue);
            }
        }
    }

    if (!extra.isEmpty() && extra != QLatin1String("NONE")) {
        def += QLatin1Char(' ') + extra;
        if (extra == QLatin1String("ON UPDATE CURRENT_TIMESTAMP") && fieldDefIncludesLength)
            def += QStringLiteral("(%1)").arg(column.length.trimmed());
    }

    if (!column.comment.isEmpty()) def += QStringLiteral(" COMMENT %1").arg(escape(column.comment));
    return def;
}

QString addColumn(const QString &table, const Column &column, const QString &afterColumn, const QString &nullValueString, const EscapeFunction &escape)
{
    QString sql = QStringLiteral("ALTER TABLE %1 ADD %2").arg(q(table), columnDefinition(column, nullValueString, escape));
    if (!afterColumn.isEmpty()) sql += QStringLiteral(" AFTER %1").arg(q(afterColumn));
    return sql;
}

QString changeColumn(const QString &table, const QString &oldName, const Column &column, const QString &nullValueString, const EscapeFunction &escape)
{
    return QStringLiteral("ALTER TABLE %1 CHANGE %2 %3").arg(q(table), q(oldName), columnDefinition(column, nullValueString, escape));
}

QString dropColumn(const QString &table, const QString &column)
{
    return QStringLiteral("ALTER TABLE %1 DROP %2").arg(q(table), q(column));
}

QString addIndex(const QString &table, const QString &indexType, const QString &name, const QStringList &columns, const QStringList &subParts, const QString &storageType)
{
    QStringList parts;
    for (int i = 0; i < columns.size(); ++i) {
        QString part = q(columns.at(i));
        if (i < subParts.size() && !subParts.at(i).trimmed().isEmpty()) part += QStringLiteral("(%1)").arg(subParts.at(i).trimmed());
        parts << part;
    }
    QString sql = QStringLiteral("ALTER TABLE %1 ADD ").arg(q(table));
    const QString type = indexType.trimmed().toUpper();
    if (type == QLatin1String("PRIMARY KEY")) {
        sql += QStringLiteral("PRIMARY KEY (%1)").arg(parts.join(QStringLiteral(", ")));
    } else {
        sql += type.isEmpty() || type == QLatin1String("INDEX") ? QStringLiteral("INDEX") : type + QStringLiteral(" INDEX");
        if (!name.trimmed().isEmpty()) sql += QLatin1Char(' ') + q(name.trimmed());
        sql += QStringLiteral(" (%1)").arg(parts.join(QStringLiteral(", ")));
    }
    if (!storageType.trimmed().isEmpty()) sql += QStringLiteral(" USING %1").arg(storageType.trimmed().toUpper());
    return sql;
}

QString dropIndex(const QString &table, const QString &name)
{
    if (name == QLatin1String("PRIMARY")) return QStringLiteral("ALTER TABLE %1 DROP PRIMARY KEY").arg(q(table));
    return QStringLiteral("ALTER TABLE %1 DROP INDEX %2").arg(q(table), q(name));
}

QString addForeignKey(const QString &table, const QString &name, const QStringList &columns, const QString &refDatabase,
                      const QString &refTable, const QStringList &refColumns, const QString &onDelete, const QString &onUpdate)
{
    QString sql = QStringLiteral("ALTER TABLE %1 ADD ").arg(q(table));
    if (!name.trimmed().isEmpty()) sql += QStringLiteral("CONSTRAINT %1 ").arg(q(name.trimmed()));
    sql += QStringLiteral("FOREIGN KEY (%1) REFERENCES %2 (%3)")
               .arg(SAMySQLConnection::quoteIdentifierList(columns),
                    refDatabase.isEmpty() ? q(refTable) : qq(refDatabase, refTable),
                    SAMySQLConnection::quoteIdentifierList(refColumns));
    if (!onDelete.trimmed().isEmpty()) sql += QStringLiteral(" ON DELETE %1").arg(onDelete.trimmed().toUpper());
    if (!onUpdate.trimmed().isEmpty()) sql += QStringLiteral(" ON UPDATE %1").arg(onUpdate.trimmed().toUpper());
    return sql;
}

QString dropForeignKey(const QString &table, const QString &name)
{
    return QStringLiteral("ALTER TABLE %1 DROP FOREIGN KEY %2").arg(q(table), q(name));
}

QString createTrigger(const QString &name, const QString &timing, const QString &event, const QString &table, const QString &statement)
{
    return QStringLiteral("CREATE TRIGGER %1 %2 %3 ON %4 FOR EACH ROW %5")
        .arg(q(name), timing.trimmed().toUpper(), event.trimmed().toUpper(), q(table), statement.trimmed());
}

QString dropTrigger(const QString &database, const QString &name)
{
    return database.isEmpty() ? QStringLiteral("DROP TRIGGER %1").arg(q(name)) : QStringLiteral("DROP TRIGGER %1").arg(qq(database, name));
}

QString createTable(const QString &name, const QString &engine, const QString &charset, const QString &collation)
{
    QString sql = QStringLiteral("CREATE TABLE %1 (id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)").arg(q(name));
    if (!engine.isEmpty()) sql += QStringLiteral(" ENGINE=%1").arg(engine);
    if (!charset.isEmpty()) sql += QStringLiteral(" DEFAULT CHARACTER SET %1").arg(charset);
    if (!collation.isEmpty()) sql += QStringLiteral(" COLLATE %1").arg(collation);
    return sql;
}

QString createView(const QString &name, const QString &selectStatement)
{
    return QStringLiteral("CREATE VIEW %1 AS %2").arg(q(name), selectStatement.trimmed());
}

QString renameObject(ObjectType type, const QString &oldName, const QString &newName)
{
    if (type == ObjectType::Table || type == ObjectType::View)
        return QStringLiteral("RENAME TABLE %1 TO %2").arg(q(oldName), q(newName));
    return QString();   // procedures/functions need a re-create; handled by the caller
}

QString truncateTable(const QString &table) { return QStringLiteral("TRUNCATE TABLE %1").arg(q(table)); }

QString dropObject(ObjectType type, const QString &name, bool ifExists)
{
    return QStringLiteral("DROP %1 %2%3").arg(objectTypeName(type), ifExists ? QStringLiteral("IF EXISTS ") : QString(), q(name));
}

QString duplicateTable(const QString &source, const QString &target, bool copyContent)
{
    if (copyContent) return QStringLiteral("CREATE TABLE %1 SELECT * FROM %2").arg(q(target), q(source));
    return QStringLiteral("CREATE TABLE %1 LIKE %2").arg(q(target), q(source));
}

QString createDatabase(const QString &name, const QString &charset, const QString &collation)
{
    QString sql = QStringLiteral("CREATE DATABASE %1").arg(q(name));
    if (!charset.isEmpty()) sql += QStringLiteral(" DEFAULT CHARACTER SET %1").arg(charset);
    if (!collation.isEmpty()) sql += QStringLiteral(" DEFAULT COLLATE %1").arg(collation);
    return sql;
}

QString dropDatabase(const QString &name) { return QStringLiteral("DROP DATABASE %1").arg(q(name)); }

QString alterDatabase(const QString &name, const QString &charset, const QString &collation)
{
    QString sql = QStringLiteral("ALTER DATABASE %1").arg(q(name));
    if (!charset.isEmpty()) sql += QStringLiteral(" DEFAULT CHARACTER SET %1").arg(charset);
    if (!collation.isEmpty()) sql += QStringLiteral(" DEFAULT COLLATE %1").arg(collation);
    return sql;
}

QString maintenance(const QString &command, const QStringList &tables)
{
    return QStringLiteral("%1 TABLE %2").arg(command.trimmed().toUpper(), SAMySQLConnection::quoteIdentifierList(tables));
}

QString alterTableOption(const QString &table, const QString &option, const QString &value, bool quoteValue, const EscapeFunction &escape)
{
    return QStringLiteral("ALTER TABLE %1 %2 = %3").arg(q(table), option, quoteValue ? escape(value) : value);
}

QString setAutoIncrement(const QString &table, quint64 value)
{
    return QStringLiteral("ALTER TABLE %1 AUTO_INCREMENT = %2").arg(q(table)).arg(value);
}

QString killQuery(quint64 id, bool tidb) { return (tidb ? QStringLiteral("KILL TIDB QUERY %1") : QStringLiteral("KILL QUERY %1")).arg(id); }
QString killConnection(quint64 id, bool tidb) { return (tidb ? QStringLiteral("KILL TIDB CONNECTION %1") : QStringLiteral("KILL CONNECTION %1")).arg(id); }

} // namespace SASchema
