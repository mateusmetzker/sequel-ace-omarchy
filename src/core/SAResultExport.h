//
//  SAResultExport.h
//  Sequel Ace (Linux port)
//
//  Serialisation of result rows for the export dialog: CSV formatting and the
//  paginated SELECT it fetches batches with. Kept out of the UI so the exact
//  bytes that reach the file can be unit tested without a widget or a server.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"
#include "SASchemaQueries.h"

#include <QString>
#include <QStringList>

namespace SAResultExport {

struct CsvOptions {
    QString separator = QStringLiteral(",");
    QString enclosure = QStringLiteral("\"");
    QString nullString = QStringLiteral("NULL");
    bool header = true;
};

// Wraps `value` in `enclosure`, doubling any occurrence of the enclosure inside
// it. An empty enclosure returns the value unchanged.
QString csvEscape(const QString &value, const QString &enclosure);

// Header row, terminated by a newline. Empty when options.header is false.
QString csvHeaderLine(const QVector<SAField> &fields, const CsvOptions &options);
QString csvHeaderLine(const QVector<SASchema::Column> &columns, const CsvOptions &options);

// One newline-terminated line per row. NULL is written as options.nullString
// *without* the enclosure, which is the only thing distinguishing it from a
// value whose text is literally "NULL". Binary and geometry become 0x hex,
// bit becomes a number, numerics are written bare, everything else is enclosed.
QString csvRows(const QVector<SAField> &fields, const QVector<SARow> &rows, const CsvOptions &options);

// SELECT for one batch. `where` is a WHERE clause without the keyword and
// `orderBy` an ORDER BY list without the keyword; either may be empty.
// Paginating with OFFSET needs a deterministic order, so passing an empty
// `orderBy` may repeat or skip rows between batches.
QString batchSelect(const QString &quotedTable, const QStringList &quotedColumns,
                    const QString &where, const QString &orderBy, quint64 offset, quint64 limit);

} // namespace SAResultExport
