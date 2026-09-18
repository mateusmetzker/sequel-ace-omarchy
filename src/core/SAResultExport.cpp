//
//  SAResultExport.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAResultExport.h"

namespace SAResultExport {

QString csvEscape(const QString &value, const QString &enclosure)
{
    if (enclosure.isEmpty()) return value;
    QString out = value;
    out.replace(enclosure, enclosure + enclosure);
    return enclosure + out + enclosure;
}

namespace {

QString headerLine(const QStringList &names, const CsvOptions &options)
{
    if (!options.header) return QString();
    QStringList escaped;
    escaped.reserve(names.size());
    for (const QString &name : names) escaped << csvEscape(name, options.enclosure);
    return escaped.join(options.separator) + QStringLiteral("\n");
}

} // namespace

QString csvHeaderLine(const QVector<SAField> &fields, const CsvOptions &options)
{
    QStringList names;
    names.reserve(fields.size());
    for (const SAField &f : fields) names << f.name;
    return headerLine(names, options);
}

QString csvHeaderLine(const QVector<SASchema::Column> &columns, const CsvOptions &options)
{
    QStringList names;
    names.reserve(columns.size());
    for (const SASchema::Column &c : columns) names << c.name;
    return headerLine(names, options);
}

QString csvRows(const QVector<SAField> &fields, const QVector<SARow> &rows, const CsvOptions &options)
{
    QString out;
    for (const SARow &row : rows) {
        QStringList cells;
        cells.reserve(row.size());
        for (int c = 0; c < row.size(); ++c) {
            const SACell &cell = row.at(c);
            if (cell.isNull) { cells << options.nullString; continue; }
            if (c >= fields.size()) { cells << csvEscape(QString::fromUtf8(cell.data), options.enclosure); continue; }
            const SAField &f = fields.at(c);
            // Numerics are tested before binary on purpose: MySQL reports the
            // binary charset (63) for every numeric column, so isBinary() alone
            // would hex-encode INT and DECIMAL values.
            if (f.typeGroup == QLatin1String("bit"))
                cells << QString::number(cell.data.isEmpty() ? 0 : static_cast<unsigned char>(cell.data.at(cell.data.size() - 1)));
            else if (f.isNumeric())
                cells << QString::fromUtf8(cell.data);
            else if (f.isBinary() || f.typeGroup == QLatin1String("geometry"))
                cells << csvEscape(QStringLiteral("0x") + QString::fromLatin1(cell.data.toHex()), options.enclosure);
            else
                cells << csvEscape(QString::fromUtf8(cell.data), options.enclosure);
        }
        out += cells.join(options.separator) + QStringLiteral("\n");
    }
    return out;
}

QString batchSelect(const QString &quotedTable, const QStringList &quotedColumns,
                    const QString &where, const QString &orderBy, quint64 offset, quint64 limit)
{
    QString sql = QStringLiteral("SELECT %1 FROM %2").arg(quotedColumns.join(QStringLiteral(", ")), quotedTable);
    if (!where.trimmed().isEmpty()) sql += QStringLiteral(" WHERE ") + where.trimmed();
    if (!orderBy.trimmed().isEmpty()) sql += QStringLiteral(" ORDER BY ") + orderBy.trimmed();
    sql += QStringLiteral(" LIMIT %1, %2").arg(offset).arg(limit);
    return sql;
}

} // namespace SAResultExport
