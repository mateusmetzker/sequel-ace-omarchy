//
//  SAContentEditing.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAContentEditing.h"
#include "SAMySQLConnection.h"

#include <QRegularExpression>

namespace SAContentEditing {

namespace {

int columnIndex(const QVector<SASchema::Column> &columns, const QString &name)
{
    for (int i = 0; i < columns.size(); ++i)
        if (columns.at(i).name == name) return i;
    return -1;
}

QString q(const QString &identifier) { return SAMySQLConnection::quoteIdentifier(identifier); }

bool looksLikeUtf8Text(const QByteArray &bytes)
{
    // Reject NUL bytes and invalid UTF-8; used to decide how to show binary columns.
    if (bytes.contains('\0')) return false;
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}

} // namespace

QString whereArgumentForRow(const QVector<SASchema::Column> &columns, const SARow &row, const QStringList &keyColumns,
                            const Escaper &escaper, bool *usesLimit, bool excludeLimit)
{
    QStringList keys = keyColumns;
    bool limit = false;
    if (keys.isEmpty()) {
        for (const SASchema::Column &c : columns) keys << c.name;
        limit = true;
    }
    if (usesLimit) *usesLimit = limit;
    if (keys.isEmpty()) return QString();

    QStringList parts;
    for (const QString &key : keys) {
        const int index = columnIndex(columns, key);
        if (index < 0 || index >= row.size()) return QString();
        const SASchema::Column &column = columns.at(index);
        const SACell &value = row.at(index);
        if (value.isNull) {
            parts << QStringLiteral("%1 IS NULL").arg(q(key));
            continue;
        }
        QString literal;
        if (column.type == QLatin1String("BIT")) {
            literal = QStringLiteral("b'%1'").arg(QString::fromUtf8(value.data));
        } else if (column.typeGroup == QLatin1String("geometry") || column.typeGroup == QLatin1String("blobdata")
                   || column.typeGroup == QLatin1String("binary")) {
            literal = escaper.data(value.data);
        } else {
            literal = escaper.string(QString::fromUtf8(value.data));
        }
        parts << QStringLiteral("%1 = %2").arg(q(key), literal);
    }
    QString argument = parts.join(QStringLiteral(" AND "));
    if (limit && !excludeLimit) argument += QStringLiteral(" LIMIT 1");
    return argument;
}

QString valueLiteral(const SASchema::Column &column, const SACell &cell, bool cellIsBinary,
                     const QString &nullValueString, const Escaper &escaper)
{
    const QString &group = column.typeGroup;
    if (cell.isNull) return QStringLiteral("NULL");
    if (cellIsBinary) return escaper.data(cell.data);

    const QString text = QString::fromUtf8(cell.data);
    if (text == nullValueString && column.nullable) return QStringLiteral("NULL");

    // Empty numeric or date fields become NULL when the column allows it.
    if ((group == QLatin1String("float") || group == QLatin1String("integer") || group == QLatin1String("date"))
        && text.isEmpty() && column.nullable) {
        return QStringLiteral("NULL");
    }
    if (group == QLatin1String("geometry")) {
        // Accept WKT typed by the user; geometry read from the server is binary and handled above.
        return QStringLiteral("ST_GeomFromText(%1)").arg(escaper.string(text));
    }
    if (group == QLatin1String("bit")) {
        // SHOW COLUMNS reports BIT defaults as b'1'; accept that form as is.
        static const QRegularExpression bitLiteral(QStringLiteral("^[bB]'[01]*'$"));
        if (bitLiteral.match(text.trimmed()).hasMatch()) {
            const QString bits = text.trimmed().mid(2, text.trimmed().size() - 3);
            return QStringLiteral("b'%1'").arg(bits.isEmpty() ? QStringLiteral("0") : bits);
        }
        return QStringLiteral("b'%1'").arg(text.isEmpty() || text == QLatin1String("0") ? QStringLiteral("0") : text);
    }
    if (group == QLatin1String("date") && text.compare(QLatin1String("NOW()"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("NOW()");
    }
    if (group == QLatin1String("date") && text.compare(QLatin1String("CURRENT_TIMESTAMP"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("CURRENT_TIMESTAMP");
    }
    if (group == QLatin1String("string") && (text.compare(QLatin1String("UUID()"), Qt::CaseInsensitive) == 0
                                             || text.compare(QLatin1String("UUID_v4()"), Qt::CaseInsensitive) == 0)) {
        return text;
    }
    // Default values that are functions (CURRENT_TIMESTAMP...) shown in new rows are kept verbatim.
    if (column.hasDefault && !column.defaultValue.isEmpty() && text == column.defaultValue
        && column.defaultValue.endsWith(QLatin1Char(')')) && (group == QLatin1String("date"))) {
        return text;
    }
    return escaper.string(text);
}

QString updateStatement(const QString &table, const QVector<SASchema::Column> &columns, const SARow &oldRow,
                        const EditedRow &newRow, const QStringList &keyColumns, const QString &nullValueString,
                        const Escaper &escaper, QString *problem)
{
    QStringList assignments;
    for (int i = 0; i < columns.size() && i < newRow.cells.size(); ++i) {
        const SASchema::Column &column = columns.at(i);
        if (column.isGenerated()) continue;
        const SACell &value = newRow.cells.at(i);
        if (i < oldRow.size() && value == oldRow.at(i)) continue;
        assignments << QStringLiteral("%1 = %2").arg(q(column.name), valueLiteral(column, value, newRow.binaryColumns.contains(i), nullValueString, escaper));
    }
    if (assignments.isEmpty()) return QString();

    bool usesLimit = false;
    const QString where = whereArgumentForRow(columns, oldRow, keyColumns, escaper, &usesLimit);
    if (where.isEmpty()) {
        if (problem) *problem = QStringLiteral("Did not find a plausible WHERE condition for the UPDATE.");
        return QString();
    }
    return QStringLiteral("UPDATE %1 SET %2 WHERE %3").arg(q(table), assignments.join(QStringLiteral(", ")), where);
}

QString insertStatement(const QString &table, const QVector<SASchema::Column> &columns, const EditedRow &newRow,
                        const QString &nullValueString, const Escaper &escaper)
{
    QStringList names;
    QStringList values;
    for (int i = 0; i < columns.size() && i < newRow.cells.size(); ++i) {
        const SASchema::Column &column = columns.at(i);
        if (column.isGenerated()) continue;
        names << q(column.name);
        values << valueLiteral(column, newRow.cells.at(i), newRow.binaryColumns.contains(i), nullValueString, escaper);
    }
    return QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(q(table), names.join(QStringLiteral(", ")), values.join(QStringLiteral(", ")));
}

QStringList deleteStatements(const QString &table, const QVector<SASchema::Column> &columns, const QVector<SARow> &rows,
                             const QStringList &keyColumns, const Escaper &escaper, QString *problem)
{
    QStringList statements;
    if (rows.isEmpty()) return statements;

    if (keyColumns.size() == 1) {
        const int index = columnIndex(columns, keyColumns.first());
        if (index >= 0) {
            const SASchema::Column &column = columns.at(index);
            QStringList values;
            QStringList nullRows;
            for (const SARow &row : rows) {
                if (index >= row.size()) continue;
                const SACell &cell = row.at(index);
                if (cell.isNull) { nullRows << QStringLiteral("%1 IS NULL").arg(q(column.name)); continue; }
                if (column.type == QLatin1String("BIT")) values << QStringLiteral("b'%1'").arg(QString::fromUtf8(cell.data));
                else if (column.typeGroup == QLatin1String("blobdata") || column.typeGroup == QLatin1String("binary")) values << escaper.data(cell.data);
                else values << escaper.string(QString::fromUtf8(cell.data));
            }
            // Keep statements below the packet size by chunking large selections.
            const int chunk = 500;
            for (int i = 0; i < values.size(); i += chunk) {
                statements << QStringLiteral("DELETE FROM %1 WHERE %2 IN (%3)").arg(q(table), q(column.name), values.mid(i, chunk).join(QStringLiteral(", ")));
            }
            if (!nullRows.isEmpty()) statements << QStringLiteral("DELETE FROM %1 WHERE %2").arg(q(table), nullRows.first());
            return statements;
        }
    }

    QStringList conditions;
    for (const SARow &row : rows) {
        bool usesLimit = false;
        const QString where = whereArgumentForRow(columns, row, keyColumns, escaper, &usesLimit, true);
        if (where.isEmpty()) {
            if (problem) *problem = QStringLiteral("A row could not be identified for deletion.");
            continue;
        }
        if (usesLimit) {
            // Without a primary key every row is deleted individually with LIMIT 1.
            statements << QStringLiteral("DELETE FROM %1 WHERE %2 LIMIT 1").arg(q(table), where);
        } else {
            conditions << QStringLiteral("(%1)").arg(where);
        }
    }
    if (!conditions.isEmpty()) statements << QStringLiteral("DELETE FROM %1 WHERE %2").arg(q(table), conditions.join(QStringLiteral(" OR ")));
    return statements;
}

QString insertStatementForRows(const QString &table, const QVector<SAField> &fields, const QVector<SARow> &rows,
                               bool skipAutoIncrement, const Escaper &escaper)
{
    if (rows.isEmpty() || fields.isEmpty()) return QString();
    QStringList names;
    QVector<int> indexes;
    for (int i = 0; i < fields.size(); ++i) {
        if (skipAutoIncrement && fields.at(i).isAutoIncrement()) continue;
        names << q(fields.at(i).orgName.isEmpty() ? fields.at(i).name : fields.at(i).orgName);
        indexes << i;
    }
    QStringList tuples;
    for (const SARow &row : rows) {
        QStringList values;
        for (int i : indexes) {
            if (i >= row.size() || row.at(i).isNull) { values << QStringLiteral("NULL"); continue; }
            const SAField &f = fields.at(i);
            const SACell &cell = row.at(i);
            if (f.typeGroup == QLatin1String("bit")) values << QStringLiteral("b'%1'").arg(QString::fromUtf8(cell.data));
            else if (f.isBinary() || f.typeGroup == QLatin1String("geometry")) values << escaper.data(cell.data);
            else if (f.typeGroup == QLatin1String("integer") || f.typeGroup == QLatin1String("float")) values << QString::fromUtf8(cell.data);
            else values << escaper.string(QString::fromUtf8(cell.data));
        }
        tuples << QStringLiteral("(%1)").arg(values.join(QStringLiteral(", ")));
    }
    return QStringLiteral("INSERT INTO %1 (%2)\nVALUES\n%3;").arg(q(table), names.join(QStringLiteral(", ")), tuples.join(QStringLiteral(",\n")));
}

QString displayString(const SAField &field, const SACell &cell, const QString &nullValueString, bool binaryAsHex, int maxLength)
{
    if (cell.isNull) return nullValueString;
    QString text;
    if (field.typeGroup == QLatin1String("bit")) {
        // BIT values arrive as raw bytes; show them as a binary string like the macOS app.
        QString bits;
        for (unsigned char byte : cell.data) bits += QString::number(byte, 2).rightJustified(8, QLatin1Char('0'));
        const int width = field.length ? static_cast<int>(field.length) : 1;
        text = bits.right(qMax(width, 1));
        if (text.isEmpty()) text = QStringLiteral("0");
    } else if (field.typeGroup == QLatin1String("geometry")) {
        text = QStringLiteral("0x") + QString::fromLatin1(cell.data.toHex().toUpper());
    } else if (field.isBinary() && (binaryAsHex || !looksLikeUtf8Text(cell.data))) {
        text = QStringLiteral("0x") + QString::fromLatin1(cell.data.toHex().toUpper());
    } else {
        text = QString::fromUtf8(cell.data);
    }
    if (maxLength > 0 && text.size() > maxLength) text = text.left(maxLength) + QStringLiteral("…");
    return text;
}

} // namespace SAContentEditing
