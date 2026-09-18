//
//  SASQLTypes.h
//  Sequel Ace (Linux port)
//
//  MySQL/MariaDB type name and "type group" mapping. Ports the logic of
//  SPMySQLResult's Field Definitions category (wire type -> name/group) and
//  SPTableData's CREATE TABLE parser (type name -> group), so both the result
//  grid and the structure editor classify columns the same way the macOS app
//  does. Type groups drive editing behaviour: "integer", "float", "string",
//  "textdata", "blobdata", "binary", "bit", "date", "enum", "geometry".
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>

namespace SASQLTypes {

// Charset number libmysqlclient reports for binary strings/blobs.
constexpr unsigned int BinaryCharsetNumber = 63;

// Wire-level mapping (enum_field_types value, charset, flags, length in bytes).
QString typeNameForWireType(unsigned int type, unsigned int charsetnr, unsigned int flags,
                            unsigned long long length, unsigned int maxBytesPerChar);
QString typeGroupForWireType(unsigned int type, unsigned int charsetnr, unsigned int flags);

// Name-level mapping used by the structure editor; expects an upper-case type name
// without length/attributes, e.g. "VARCHAR", "INT", "DECIMAL".
QString typeGroupForTypeName(const QString &upperTypeName);

bool isNumericType(const QString &typeName);
bool isStringType(const QString &typeName);      // CHAR/VARCHAR/*TEXT/ENUM/SET/JSON/INET*
bool isDateType(const QString &typeName);
bool isGeometryType(const QString &typeName);
bool isBlobOrTextType(const QString &typeName);
bool typeAllowsBinaryAttribute(const QString &typeName);
bool typeAllowsCharset(const QString &typeName);
bool typeAllowsUnsigned(const QString &typeName);
bool typeAllowsAutoIncrement(const QString &typeName);

// Suggested type names for the structure editor combo box, in display order.
QStringList suggestedTypes();

// Splits a SHOW COLUMNS "Type" value such as "int(11) unsigned zerofill" or
// "enum('a','b')" into its parts.
struct ParsedColumnType {
    QString name;        // upper-case base type, e.g. "INT", "ENUM"
    QString length;      // contents of the parentheses, e.g. "11", "10,2", "'a','b'"
    bool isUnsigned = false;
    bool isZerofill = false;
    bool isBinary = false;
    QString extra;       // anything else that followed the type
};
ParsedColumnType parseColumnType(const QString &showColumnsType);

} // namespace SASQLTypes
