//
//  SAMCPReadOnlyGuard.h
//  Sequel Ace (Linux port)
//
//  Security-critical, quote/comment-aware validation for the MCP server's
//  read-only mode (SPMCPReadOnlyGuard in the macOS app). Pure functions,
//  no I/O, so they can be unit tested exhaustively.
//
//  Usage order matters: bind `?` placeholders with bindParameters() FIRST,
//  then classify the *bound* SQL with checkReadOnly(). A placeholder sitting
//  inside what looks like a comment could otherwise "close" the comment once
//  its literal value is substituted, disguising a write statement as a
//  harmless comment at classification time.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>

namespace SAMCPReadOnlyGuard {

struct Verdict {
    bool allowed = false;
    QString reason;   // human-readable rejection reason, empty when allowed
};

// Removes -- / # line comments and /* */ block comments, without touching
// the contents of '...'/"..."/`...` literals. Comments are replaced by a
// single space so token boundaries survive. If `sawExecutableComment` is
// non-null, it is set to true when a MySQL/MariaDB executable version
// comment (/*! ... */ or /*M! ... */) was found — those are not ordinary
// comments, the server executes their contents.
QString stripComments(const QString &sql, bool *sawExecutableComment = nullptr);

// Substitutes each `?` outside of literals/comments with an escaped, quoted
// SQL literal built from the corresponding entry of `params` (in order).
// Extra `?` beyond params.size() are left untouched.
QString bindParameters(const QString &sql, const QStringList &params);

// True when `sql` is an EXPLAIN ANALYZE statement (in any accepted spelling,
// e.g. "EXPLAIN ANALYZE ...", "EXPLAIN FORMAT=JSON ANALYZE ..."). ANALYZE
// executes the statement for real, so this is refused unconditionally by
// checkReadOnly(), independent of the read-only preference.
bool isExplainAnalyze(const QString &sql);

// Full read-only classification: single statement, no executable comments,
// no OUTFILE/DUMPFILE/LOAD_FILE, starts with SELECT/SHOW/DESCRIBE/EXPLAIN
// (a WITH ... CTE is additionally checked for a top-level write statement
// after its CTE definitions, e.g. "WITH x AS (SELECT 1) DELETE FROM t").
// Call on the SQL *after* bindParameters().
Verdict checkReadOnly(const QString &sql);

} // namespace SAMCPReadOnlyGuard
