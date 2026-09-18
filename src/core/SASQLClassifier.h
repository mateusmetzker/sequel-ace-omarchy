//
//  SASQLClassifier.h
//  Sequel Ace (Linux port)
//
//  Lexical classification of a single SQL statement: which statements can be
//  passed to EXPLAIN, and which ones need the destructive-query confirmation.
//  Port of SPCustomQuerySQLClassifier.swift (the enum; the SASQLDatabaseContext
//  class in the same file tracks USE/DROP DATABASE and is not ported).
//
//  Pure functions, no I/O, so they can be unit tested exhaustively.
//
//  These lexical rules are mirrored in SAMCPReadOnlyGuard, which strips
//  comments for a security boundary and therefore uses the opposite policy for
//  executable comments: it drops the body and refuses, where this classifier
//  preserves the body according to the version gate so the statement that the
//  server would really run can be inspected. ExecutableComments selects between
//  the two.
//
//  Three differences from the Swift original are inherent to the port and
//  intentional:
//    * Swift iterates Characters (grapheme clusters), QString iterates UTF-16
//      code units. Irrelevant for the ASCII delimiters this lexer looks for,
//      and surrogates have unicode() >= 0xD800 so they never pass the <= 0x20
//      whitespace test.
//    * CharacterSet.alphanumerics includes combining marks (category M*),
//      QChar::isLetterOrNumber() does not. Differs only for identifiers
//      starting with a combining mark.
//    * Character.isWhitespace is treated as QChar::isSpace() in the tokenizer.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>

namespace SASQLClassifier {

// Optional server context. `known == false` means "no context": every
// executable comment body is preserved for inspection and any version or
// flavour gate marks the result indeterminate, because whether the server runs
// the body or skips it cannot be decided. That is how the macOS app calls the
// classifiers, and it is the conservative side for both of them.
struct ServerContext {
    bool known = false;
    int version = 0;        // 80036, 101106 — the scale of SAServerInfo::versionNumber
    bool isMariaDB = false;

    static ServerContext mysql(int version) { return {true, version, false}; }
    static ServerContext mariaDB(int version) { return {true, version, true}; }
};

enum class ExecutableComments {
    PreserveGated,   // keep the body when the gate says the server runs it
    DropAndFlag,     // drop every body and report that one was seen
};

struct StripOptions {
    ExecutableComments executableComments = ExecutableComments::PreserveGated;
    ServerContext server;
};

struct StripResult {
    QString sql;
    bool sawExecutableComment = false;            // a /*! or /*M! comment was found
    bool indeterminateExecutableComment = false;  // run-versus-skip could not be decided
    bool depthLimitExceeded = false;              // nested executable comments hit the cap
};

// Replaces -- / # line comments and /* */ block comments with a single space,
// without touching the contents of '...'/"..."/`...` literals. A single space
// keeps adjacent tokens apart, so SELECT/*c*/1 becomes SELECT 1. An unterminated
// block comment consumes the rest of the string. The body of an executable
// comment is stripped recursively, up to MaxExecutableCommentDepth levels.
StripResult stripComments(const QString &sql, const StripOptions &options = {});

// stripComments(...).sql, with the PreserveGated policy.
QString strippedSQL(const QString &sql, const ServerContext &server = {});

// True when `sql` is a single SELECT or WITH statement, which is what MySQL's
// EXPLAIN accepts. Comments are stripped first, leading parentheses unwrapped,
// and the keyword must be followed by a non-identifier character, so
// SELECTOR_TABLE and WITHOUT VALIDATION do not match while SELECT(1) and
// SELECT/*c*/1 do. A bare SELECT or WITH is rejected, and so is anything
// carrying an executable comment whose gate cannot be decided.
bool isQueryExplainable(const QString &sql, const ServerContext &server = {});

// True when the destructive-query confirmation may be skipped for `sql`: an
// allow list of SHOW, SELECT, and the EXPLAIN aliases EXPLAIN/DESCRIBE/DESC.
// For the aliases, EXTENDED/PARTITIONS/FORMAT [=] x modifiers are skipped and
// ANALYZE followed by UPDATE/DELETE/INSERT/REPLACE — or by a WITH whose outer
// verb cannot be found without a real parser — is refused, because ANALYZE
// executes the statement. Note that WITH is not on the allow list even though
// isQueryExplainable accepts it.
bool isQuerySafeWithoutDestructiveWarning(const QString &sql, const ServerContext &server = {});

// True when any statement in the batch needs the confirmation. Port of
// -[SPCustomQuery queriesContainDestructiveSQL:]; short-circuits on the first
// unsafe statement. An empty batch needs no warning.
bool batchNeedsDestructiveWarning(const QStringList &statements, const ServerContext &server = {});

// Recursion cap for the bodies of nested executable comments. Beyond it
// stripComments stops descending and reports depthLimitExceeded together with
// indeterminateExecutableComment, which makes both classifiers refuse.
constexpr int MaxExecutableCommentDepth = 32;

} // namespace SASQLClassifier
