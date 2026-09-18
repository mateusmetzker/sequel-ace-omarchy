//
//  SASQLTypes.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLTypes.h"

#include <QSet>
#include <QRegularExpression>

#include <mysql.h>

namespace SASQLTypes {

QString typeNameForWireType(unsigned int type, unsigned int charsetnr, unsigned int flags,
                            unsigned long long length, unsigned int maxBytesPerChar)
{
    switch (type) {
    case MYSQL_TYPE_BIT:        return QStringLiteral("BIT");
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL: return QStringLiteral("DECIMAL");
    case MYSQL_TYPE_TINY:       return QStringLiteral("TINYINT");
    case MYSQL_TYPE_SHORT:      return QStringLiteral("SMALLINT");
    case MYSQL_TYPE_LONG:       return QStringLiteral("INT");
    case MYSQL_TYPE_FLOAT:      return QStringLiteral("FLOAT");
    case MYSQL_TYPE_DOUBLE:     return QStringLiteral("DOUBLE");
    case MYSQL_TYPE_NULL:       return QStringLiteral("NULL");
    case MYSQL_TYPE_TIMESTAMP:  return QStringLiteral("TIMESTAMP");
    case MYSQL_TYPE_LONGLONG:   return QStringLiteral("BIGINT");
    case MYSQL_TYPE_INT24:      return QStringLiteral("MEDIUMINT");
    case MYSQL_TYPE_DATE:       return QStringLiteral("DATE");
    case MYSQL_TYPE_TIME:       return QStringLiteral("TIME");
    case MYSQL_TYPE_DATETIME:   return QStringLiteral("DATETIME");
    case MYSQL_TYPE_YEAR:       return QStringLiteral("YEAR");
    case MYSQL_TYPE_ENUM:       return QStringLiteral("ENUM");
    case MYSQL_TYPE_SET:        return QStringLiteral("SET");
    case MYSQL_TYPE_GEOMETRY:   return QStringLiteral("GEOMETRY");
    case MYSQL_TYPE_JSON:       return QStringLiteral("JSON");

    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB: {
        const bool isBlob = (charsetnr == BinaryCharsetNumber);
        const unsigned int divisor = maxBytesPerChar ? maxBytesPerChar : 1;
        auto classify = [isBlob](unsigned long long chars) -> QString {
            switch (chars) {
            case 255ULL:        return isBlob ? QStringLiteral("TINYBLOB")   : QStringLiteral("TINYTEXT");
            case 65535ULL:      return isBlob ? QStringLiteral("BLOB")       : QStringLiteral("TEXT");
            case 16777215ULL:   return isBlob ? QStringLiteral("MEDIUMBLOB") : QStringLiteral("MEDIUMTEXT");
            case 4294967295ULL: return isBlob ? QStringLiteral("LONGBLOB")   : QStringLiteral("LONGTEXT");
            default:            return QString();
            }
        };
        QString name = classify(length / divisor);
        if (name.isEmpty()) name = classify(length);
        if (name.isEmpty()) name = isBlob ? QStringLiteral("BLOB") : QStringLiteral("TEXT");
        return name;
    }

    case MYSQL_TYPE_VAR_STRING:
        if (flags & ENUM_FLAG) return QStringLiteral("ENUM");
        if (flags & SET_FLAG)  return QStringLiteral("SET");
        if (charsetnr == BinaryCharsetNumber) return QStringLiteral("VARBINARY");
        return QStringLiteral("VARCHAR");

    case MYSQL_TYPE_STRING:
        if (flags & ENUM_FLAG) return QStringLiteral("ENUM");
        if (flags & SET_FLAG)  return QStringLiteral("SET");
        if ((flags & BINARY_FLAG) && charsetnr == BinaryCharsetNumber) return QStringLiteral("BINARY");
        return QStringLiteral("CHAR");

    default:
        return QStringLiteral("UNKNOWN");
    }
}

QString typeGroupForWireType(unsigned int type, unsigned int charsetnr, unsigned int flags)
{
    switch (type) {
    case MYSQL_TYPE_BIT:
        return QStringLiteral("bit");
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_INT24:
        return QStringLiteral("integer");
    case MYSQL_TYPE_FLOAT:
    case MYSQL_TYPE_DOUBLE:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
        return QStringLiteral("float");
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_TIMESTAMP:
        return QStringLiteral("date");
    case MYSQL_TYPE_VAR_STRING:
        if (flags & (ENUM_FLAG | SET_FLAG)) return QStringLiteral("enum");
        if (charsetnr == BinaryCharsetNumber) return QStringLiteral("binary");
        return QStringLiteral("string");
    case MYSQL_TYPE_STRING:
        if (flags & (ENUM_FLAG | SET_FLAG)) return QStringLiteral("enum");
        if ((flags & BINARY_FLAG) && charsetnr == BinaryCharsetNumber) return QStringLiteral("binary");
        return QStringLiteral("string");
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
        return (charsetnr == BinaryCharsetNumber) ? QStringLiteral("blobdata") : QStringLiteral("textdata");
    case MYSQL_TYPE_JSON:
        return QStringLiteral("textdata");
    case MYSQL_TYPE_GEOMETRY:
        return QStringLiteral("geometry");
    default:
        return QStringLiteral("blobdata");
    }
}

namespace {

const QSet<QString> &integerTypes()
{
    static const QSet<QString> s = {"TINYINT", "SMALLINT", "MEDIUMINT", "INT", "INTEGER", "BIGINT", "SERIAL", "BOOL", "BOOLEAN"};
    return s;
}
const QSet<QString> &floatTypes()
{
    static const QSet<QString> s = {"REAL", "DOUBLE", "FLOAT", "DECIMAL", "NUMERIC", "DEC", "FIXED"};
    return s;
}
const QSet<QString> &dateTypes()
{
    static const QSet<QString> s = {"DATE", "TIME", "TIMESTAMP", "DATETIME", "YEAR"};
    return s;
}
const QSet<QString> &charTypes()
{
    static const QSet<QString> s = {"CHAR", "VARCHAR", "INET4", "INET6", "UUID"};
    return s;
}
const QSet<QString> &textTypes()
{
    static const QSet<QString> s = {"TINYTEXT", "TEXT", "MEDIUMTEXT", "LONGTEXT", "JSON"};
    return s;
}
const QSet<QString> &blobTypes()
{
    static const QSet<QString> s = {"TINYBLOB", "BLOB", "MEDIUMBLOB", "LONGBLOB"};
    return s;
}
const QSet<QString> &binaryTypes()
{
    static const QSet<QString> s = {"BINARY", "VARBINARY"};
    return s;
}
const QSet<QString> &geometryTypes()
{
    static const QSet<QString> s = {"POINT", "GEOMETRY", "LINESTRING", "POLYGON", "MULTIPOLYGON",
                                    "GEOMETRYCOLLECTION", "MULTIPOINT", "MULTILINESTRING"};
    return s;
}

} // namespace

QString typeGroupForTypeName(const QString &upperTypeName)
{
    const QString t = upperTypeName.trimmed().toUpper();
    if (t == QLatin1String("BIT")) return QStringLiteral("bit");
    if (integerTypes().contains(t)) return QStringLiteral("integer");
    if (floatTypes().contains(t)) return QStringLiteral("float");
    if (dateTypes().contains(t)) return QStringLiteral("date");
    if (charTypes().contains(t)) return QStringLiteral("string");
    if (binaryTypes().contains(t)) return QStringLiteral("binary");
    if (t == QLatin1String("ENUM") || t == QLatin1String("SET")) return QStringLiteral("enum");
    if (textTypes().contains(t)) return QStringLiteral("textdata");
    if (geometryTypes().contains(t)) return QStringLiteral("geometry");
    // Default to "blobdata" so unknown/future types are preserved unmangled.
    return QStringLiteral("blobdata");
}

bool isNumericType(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return t == QLatin1String("BIT") || integerTypes().contains(t) || floatTypes().contains(t);
}

bool isStringType(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return charTypes().contains(t) || textTypes().contains(t) || t == QLatin1String("ENUM") || t == QLatin1String("SET");
}

bool isDateType(const QString &typeName)
{
    return dateTypes().contains(typeName.trimmed().toUpper());
}

bool isGeometryType(const QString &typeName)
{
    return geometryTypes().contains(typeName.trimmed().toUpper());
}

bool isBlobOrTextType(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return textTypes().contains(t) || blobTypes().contains(t);
}

bool typeAllowsBinaryAttribute(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return t == QLatin1String("CHAR") || t == QLatin1String("VARCHAR") || (textTypes().contains(t) && t != QLatin1String("JSON"));
}

bool typeAllowsCharset(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    if (t == QLatin1String("JSON") || t == QLatin1String("UUID") || t == QLatin1String("INET4") || t == QLatin1String("INET6")) return false;
    return isStringType(t);
}

bool typeAllowsUnsigned(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return (integerTypes().contains(t) || floatTypes().contains(t)) && t != QLatin1String("SERIAL")
           && t != QLatin1String("BOOL") && t != QLatin1String("BOOLEAN");
}

bool typeAllowsAutoIncrement(const QString &typeName)
{
    const QString t = typeName.trimmed().toUpper();
    return integerTypes().contains(t) || t == QLatin1String("FLOAT") || t == QLatin1String("DOUBLE") || t == QLatin1String("REAL");
}

QStringList suggestedTypes()
{
    // Order mirrors the macOS structure editor: numeric, string, binary, date/time, spatial, other.
    return {
        "TINYINT", "SMALLINT", "MEDIUMINT", "INT", "BIGINT",
        "FLOAT", "DOUBLE", "DECIMAL", "BIT", "SERIAL", "BOOL",
        "--------",
        "CHAR", "VARCHAR", "TINYTEXT", "TEXT", "MEDIUMTEXT", "LONGTEXT", "JSON", "UUID", "INET4", "INET6",
        "--------",
        "BINARY", "VARBINARY", "TINYBLOB", "BLOB", "MEDIUMBLOB", "LONGBLOB",
        "--------",
        "ENUM", "SET",
        "--------",
        "DATE", "DATETIME", "TIMESTAMP", "TIME", "YEAR",
        "--------",
        "GEOMETRY", "POINT", "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON", "GEOMETRYCOLLECTION",
    };
}

ParsedColumnType parseColumnType(const QString &showColumnsType)
{
    ParsedColumnType result;
    QString s = showColumnsType.trimmed();

    // Base type name: everything up to the first '(' or whitespace.
    int i = 0;
    while (i < s.size() && !s.at(i).isSpace() && s.at(i) != QLatin1Char('(')) ++i;
    result.name = s.left(i).toUpper();
    s = s.mid(i).trimmed();

    // Optional parenthesised length / enum members. Respect quotes for enum('a,b').
    if (s.startsWith(QLatin1Char('('))) {
        int depth = 0;
        QChar quote;
        int end = -1;
        for (int k = 0; k < s.size(); ++k) {
            const QChar c = s.at(k);
            if (!quote.isNull()) {
                if (c == quote) {
                    if (k + 1 < s.size() && s.at(k + 1) == quote) { ++k; continue; }
                    quote = QChar();
                }
                continue;
            }
            if (c == QLatin1Char('\'') || c == QLatin1Char('"')) { quote = c; continue; }
            if (c == QLatin1Char('(')) ++depth;
            else if (c == QLatin1Char(')')) { if (--depth == 0) { end = k; break; } }
        }
        if (end > 0) {
            result.length = s.mid(1, end - 1);
            s = s.mid(end + 1).trimmed();
        } else {
            result.length = s.mid(1);
            s.clear();
        }
    }

    // Remaining attributes.
    const QStringList attrs = s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList extra;
    for (const QString &a : attrs) {
        const QString u = a.toUpper();
        if (u == QLatin1String("UNSIGNED")) result.isUnsigned = true;
        else if (u == QLatin1String("ZEROFILL")) result.isZerofill = true;
        else if (u == QLatin1String("BINARY")) result.isBinary = true;
        else extra << a;
    }
    result.extra = extra.join(QLatin1Char(' '));
    return result;
}

} // namespace SASQLTypes
