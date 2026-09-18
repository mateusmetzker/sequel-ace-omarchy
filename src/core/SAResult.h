//
//  SAResult.h
//  Sequel Ace (Linux port)
//
//  Fully materialised result of one SQL statement: field definitions (mirroring
//  the dictionary keys SPMySQLResult exposes - name, org_name, table, org_table,
//  db, type, typegrouping, flags, charsetnr, length, decimals), rows of cells,
//  and the statement's outcome (error, affected rows, insert id, timing).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>

struct SAField {
    QString name;        // alias as returned in the result
    QString orgName;     // original column name
    QString table;       // table alias
    QString orgTable;    // original table name
    QString db;          // database the column comes from
    QString typeName;    // e.g. "INT", "VARCHAR", "TEXT"
    QString typeGroup;   // "integer", "float", "string", "textdata", "blobdata", "binary", "bit", "date", "enum", "geometry"
    QString charset;     // charset name resolved from charsetnr, may be empty
    QString collation;   // collation resolved from charsetnr, may be empty
    unsigned int type = 0;
    unsigned int flags = 0;
    unsigned int charsetnr = 0;
    unsigned long long length = 0;
    unsigned int decimals = 0;
    unsigned int maxBytesPerChar = 1;

    bool isBinary() const;
    bool isPrimaryKey() const;
    bool isUniqueKey() const;
    bool isMultipleKey() const;
    bool isNotNull() const;
    bool isAutoIncrement() const;
    bool isUnsigned() const;
    bool isZerofill() const;
    bool isBlobOrText() const;   // typeGroup textdata/blobdata
    bool isNumeric() const;      // typeGroup integer/float/bit
    // Display width in characters for the column (length / bytes per char for strings).
    unsigned long long displayLength() const;
};

struct SACell {
    QByteArray data;
    bool isNull = true;

    static SACell null() { return SACell(); }
    static SACell of(const QByteArray &bytes) { SACell c; c.data = bytes; c.isNull = false; return c; }
    static SACell ofString(const QString &s) { return of(s.toUtf8()); }
    QString toString() const { return isNull ? QString() : QString::fromUtf8(data); }
    bool operator==(const SACell &o) const { return isNull == o.isNull && (isNull || data == o.data); }
    bool operator!=(const SACell &o) const { return !(*this == o); }
};

using SARow = QVector<SACell>;

class SAResult {
public:
    QString query;
    bool ok = false;               // statement executed without error
    bool hasResultSet = false;     // statement produced columns (SELECT/SHOW/...)
    bool wasCancelled = false;
    QVector<SAField> fields;
    QVector<SARow> rows;
    quint64 affectedRows = 0;      // ~0ULL when not applicable
    quint64 insertId = 0;
    unsigned int warningCount = 0;
    unsigned int errorNumber = 0;
    QString errorMessage;
    QString sqlState;
    double executionTime = 0.0;    // seconds until the first row was available

    int rowCount() const { return rows.size(); }
    int fieldCount() const { return fields.size(); }
    int fieldIndex(const QString &name) const;
    QStringList fieldNames() const;

    const SACell &cell(int row, int col) const;
    QString stringAt(int row, int col) const;                  // "" for NULL
    QString stringAt(int row, const QString &fieldName) const; // "" for NULL or missing
    bool isNull(int row, int col) const;

    // Convenience for single-value lookups (first row / first column).
    QString firstValue() const;
    // Row as name -> string (NULL becomes a null QString).
    QMap<QString, QString> rowAsMap(int row) const;

    static SAResult errorResult(const QString &query, unsigned int number, const QString &message, const QString &sqlState = QString());
};

Q_DECLARE_METATYPE(SAResult)
