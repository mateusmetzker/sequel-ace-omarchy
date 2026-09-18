//
//  SAContentEditing.h
//  Sequel Ace (Linux port)
//
//  Turns edits made in the content grid into SQL, porting the row-identity and
//  value-conversion rules of SPTableContent (argumentForRow:, deriveQueryString,
//  removeRow:). Pure functions so they can be unit tested without a server.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"
#include "SASchemaQueries.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace SAContentEditing {

struct Escaper {
    std::function<QString(const QString &)> string;      // quoted + escaped literal
    std::function<QString(const QByteArray &)> data;     // X'..' literal
};

// Cells the user typed: text (UTF-8) unless `binary` marks raw bytes edited in the hex editor.
struct EditedRow {
    SARow cells;
    QSet<int> binaryColumns;
};

// Builds the WHERE argument identifying `row` (without the WHERE keyword).
// Uses the primary key columns when present, otherwise every column and a
// LIMIT 1 (reported through *usesLimit). Returns an empty string when the row
// cannot be identified safely (missing key values, unloaded blobs...).
QString whereArgumentForRow(const QVector<SASchema::Column> &columns, const SARow &row, const QStringList &keyColumns,
                            const Escaper &escaper, bool *usesLimit, bool excludeLimit = false);

// Literal for a value typed into a cell, applying the same conversions as the
// macOS app: NULL marker, empty numeric/date -> NULL when nullable, BIT b'',
// NOW()/UUID() functions, geometry WKT, binary -> X''.
QString valueLiteral(const SASchema::Column &column, const SACell &cell, bool cellIsBinary,
                     const QString &nullValueString, const Escaper &escaper);

// UPDATE with only the changed columns; empty when nothing changed or the row
// cannot be identified.
QString updateStatement(const QString &table, const QVector<SASchema::Column> &columns, const SARow &oldRow,
                        const EditedRow &newRow, const QStringList &keyColumns, const QString &nullValueString,
                        const Escaper &escaper, QString *problem = nullptr);

// INSERT for a new row (all non-generated columns).
QString insertStatement(const QString &table, const QVector<SASchema::Column> &columns, const EditedRow &newRow,
                        const QString &nullValueString, const Escaper &escaper);

// DELETE statements for the given rows: a single "IN (...)" when a single
// primary key column exists, otherwise one WHERE per row (joined with OR).
QStringList deleteStatements(const QString &table, const QVector<SASchema::Column> &columns, const QVector<SARow> &rows,
                             const QStringList &keyColumns, const Escaper &escaper, QString *problem = nullptr);

// "Copy as SQL INSERT" for selected rows.
QString insertStatementForRows(const QString &table, const QVector<SAField> &fields, const QVector<SARow> &rows,
                               bool skipAutoIncrement, const Escaper &escaper);

// Display text for a cell (NULL marker, hex for binary when requested, geometry hex).
QString displayString(const SAField &field, const SACell &cell, const QString &nullValueString, bool binaryAsHex, int maxLength = -1);

} // namespace SAContentEditing
