//
//  SAResult.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAResult.h"
#include "SASQLTypes.h"

#include <QMap>
#include <mysql.h>

bool SAField::isBinary() const { return charsetnr == SASQLTypes::BinaryCharsetNumber; }
bool SAField::isPrimaryKey() const { return flags & PRI_KEY_FLAG; }
bool SAField::isUniqueKey() const { return flags & UNIQUE_KEY_FLAG; }
bool SAField::isMultipleKey() const { return flags & MULTIPLE_KEY_FLAG; }
bool SAField::isNotNull() const { return flags & NOT_NULL_FLAG; }
bool SAField::isAutoIncrement() const { return flags & AUTO_INCREMENT_FLAG; }
bool SAField::isUnsigned() const { return flags & UNSIGNED_FLAG; }
bool SAField::isZerofill() const { return flags & ZEROFILL_FLAG; }
bool SAField::isBlobOrText() const { return typeGroup == QLatin1String("textdata") || typeGroup == QLatin1String("blobdata"); }
bool SAField::isNumeric() const
{
    return typeGroup == QLatin1String("integer") || typeGroup == QLatin1String("float") || typeGroup == QLatin1String("bit");
}
unsigned long long SAField::displayLength() const
{
    if (maxBytesPerChar > 1 && (typeGroup == QLatin1String("string") || typeGroup == QLatin1String("textdata") || typeGroup == QLatin1String("enum")))
        return length / maxBytesPerChar;
    return length;
}

int SAResult::fieldIndex(const QString &name) const
{
    for (int i = 0; i < fields.size(); ++i)
        if (fields.at(i).name == name) return i;
    return -1;
}

QStringList SAResult::fieldNames() const
{
    QStringList names;
    names.reserve(fields.size());
    for (const SAField &f : fields) names << f.name;
    return names;
}

const SACell &SAResult::cell(int row, int col) const
{
    static const SACell nullCell;
    if (row < 0 || row >= rows.size()) return nullCell;
    const SARow &r = rows.at(row);
    if (col < 0 || col >= r.size()) return nullCell;
    return r.at(col);
}

QString SAResult::stringAt(int row, int col) const
{
    return cell(row, col).toString();
}

QString SAResult::stringAt(int row, const QString &fieldName) const
{
    const int col = fieldIndex(fieldName);
    return col < 0 ? QString() : stringAt(row, col);
}

bool SAResult::isNull(int row, int col) const
{
    return cell(row, col).isNull;
}

QString SAResult::firstValue() const
{
    if (rows.isEmpty() || rows.first().isEmpty()) return QString();
    return rows.first().first().toString();
}

QMap<QString, QString> SAResult::rowAsMap(int row) const
{
    QMap<QString, QString> map;
    if (row < 0 || row >= rows.size()) return map;
    const SARow &r = rows.at(row);
    for (int i = 0; i < fields.size() && i < r.size(); ++i)
        map.insert(fields.at(i).name, r.at(i).isNull ? QString() : QString::fromUtf8(r.at(i).data));
    return map;
}

SAResult SAResult::errorResult(const QString &query, unsigned int number, const QString &message, const QString &sqlState)
{
    SAResult r;
    r.query = query;
    r.ok = false;
    r.errorNumber = number;
    r.errorMessage = message;
    r.sqlState = sqlState;
    r.affectedRows = ~0ULL;
    return r;
}
