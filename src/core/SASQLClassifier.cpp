//
//  SASQLClassifier.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLClassifier.h"

#include <optional>

namespace SASQLClassifier {
namespace {

// MySQL opens a `--` comment only when whitespace or a control character
// follows. QChar::isSpace() is wrong here: it accepts U+00A0, which the server
// treats as an ordinary character.
bool isCommentWhitespace(QChar c)
{
    return c.unicode() <= 0x20;
}

bool isIdentifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// Whether the server executes the body of an executable comment. Without
// connection context every body is preserved for inspection, and the caller is
// told the state is indeterminate.
bool shouldPreserveExecutableComment(std::optional<int> requiredVersion, bool hasVersionGate,
                                     bool isMariaDBOnly, const ServerContext &server)
{
    if (!server.known) return true;
    if (isMariaDBOnly && !server.isMariaDB) return false;
    if (hasVersionGate && !requiredVersion.has_value()) return false;
    if (requiredVersion.has_value() && *requiredVersion > server.version) return false;

    // MariaDB deliberately ignores MySQL 5.7+ version-gated comments in this
    // range; /*M! ... */ stays available for MariaDB-specific SQL.
    if (server.isMariaDB && !isMariaDBOnly && requiredVersion.has_value()
        && *requiredVersion >= 50700 && *requiredVersion <= 99999)
        return false;

    return true;
}

StripResult stripCommentsAtDepth(const QString &sql, const StripOptions &options, int depth)
{
    StripResult out;
    out.sql.reserve(sql.size());

    const int n = sql.size();
    int i = 0;
    QChar quote;

    while (i < n) {
        const QChar c = sql.at(i);

        if (!quote.isNull()) {
            out.sql += c;
            if (c == QLatin1Char('\\') && quote != QLatin1Char('`') && i + 1 < n) {
                ++i;
                out.sql += sql.at(i);
            } else if (c == quote) {
                if (i + 1 < n && sql.at(i + 1) == quote) {
                    ++i;
                    out.sql += sql.at(i);
                } else {
                    quote = QChar();
                }
            }
            ++i;
            continue;
        }

        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            quote = c;
            out.sql += c;
            ++i;
            continue;
        }

        if (c == QLatin1Char('#')) {
            out.sql += QLatin1Char(' ');
            ++i;
            while (i < n && sql.at(i) != QLatin1Char('\n')) ++i;
            continue;
        }

        if (c == QLatin1Char('-') && i + 1 < n && sql.at(i + 1) == QLatin1Char('-')
            && (i + 2 == n || isCommentWhitespace(sql.at(i + 2)))) {
            out.sql += QLatin1Char(' ');
            i += 2;
            while (i < n && sql.at(i) != QLatin1Char('\n')) ++i;
            continue;
        }

        if (c == QLatin1Char('/') && i + 1 < n && sql.at(i + 1) == QLatin1Char('*')) {
            std::optional<int> executableContentStart;
            bool isMariaDBOnly = false;
            if (i + 2 < n && sql.at(i + 2) == QLatin1Char('!')) {
                executableContentStart = i + 3;
            } else if (i + 3 < n && (sql.at(i + 2) == QLatin1Char('M') || sql.at(i + 2) == QLatin1Char('m'))
                       && sql.at(i + 3) == QLatin1Char('!')) {
                executableContentStart = i + 4;
                isMariaDBOnly = true;
            }

            int closing = i + 2;
            while (closing + 1 < n && !(sql.at(closing) == QLatin1Char('*') && sql.at(closing + 1) == QLatin1Char('/')))
                ++closing;
            const bool hasClosingMarker = closing + 1 < n;
            const int contentEnd = hasClosingMarker ? closing : n;

            out.sql += QLatin1Char(' ');
            if (executableContentStart.has_value()) {
                out.sawExecutableComment = true;

                int contentStart = *executableContentStart;
                const int versionStart = contentStart;
                while (contentStart < contentEnd && sql.at(contentStart).isDigit()
                       && sql.at(contentStart).unicode() < 0x80)
                    ++contentStart;
                const bool hasVersionGate = contentStart > versionStart;
                std::optional<int> requiredVersion;
                if (hasVersionGate) {
                    bool ok = false;
                    // A gate too large for int leaves this unset, which makes the
                    // comment inactive on a known server and indeterminate
                    // otherwise -- never a silent 0, which would read as "always
                    // active".
                    const int parsed = QStringView(sql).mid(versionStart, contentStart - versionStart).toInt(&ok);
                    if (ok) requiredVersion = parsed;
                }

                if (!options.server.known && (hasVersionGate || (isMariaDBOnly && !options.server.isMariaDB)))
                    out.indeterminateExecutableComment = true;

                const bool preserve = options.executableComments == ExecutableComments::PreserveGated
                    && shouldPreserveExecutableComment(requiredVersion, hasVersionGate, isMariaDBOnly, options.server);

                if (preserve && contentStart < contentEnd) {
                    if (depth >= MaxExecutableCommentDepth) {
                        // Refuse to descend further rather than risk the stack.
                        // Both classifiers reject on either of these flags.
                        out.depthLimitExceeded = true;
                        out.indeterminateExecutableComment = true;
                    } else {
                        const StripResult nested = stripCommentsAtDepth(
                            sql.mid(contentStart, contentEnd - contentStart), options, depth + 1);
                        out.sql += nested.sql;
                        out.sawExecutableComment = out.sawExecutableComment || nested.sawExecutableComment;
                        out.indeterminateExecutableComment =
                            out.indeterminateExecutableComment || nested.indeterminateExecutableComment;
                        out.depthLimitExceeded = out.depthLimitExceeded || nested.depthLimitExceeded;
                    }
                }
                out.sql += QLatin1Char(' ');
            }

            i = hasClosingMarker ? closing + 2 : n;
            continue;
        }

        out.sql += c;
        ++i;
    }

    return out;
}

// Leading `(` groups are unwrapped so `(SELECT ...)` is treated like
// `SELECT ...` while `(UPDATE ...)` and `(EXPLAIN ...)` still are not.
QString unwrappedUpper(const QString &strippedSql)
{
    QString trimmed = strippedSql.trimmed();
    while (trimmed.startsWith(QLatin1Char('('))) trimmed = trimmed.mid(1).trimmed();
    return trimmed.toUpper();
}

bool hasLeadingKeyword(const QString &keyword, const QString &upper, bool allowBare = true)
{
    if (!upper.startsWith(keyword)) return false;
    if (upper.size() == keyword.size()) return allowBare;
    return !isIdentifierChar(upper.at(keyword.size()));
}

// `=` is wrapped in spaces first, so FORMAT=JSON always tokenizes as
// [FORMAT, =, JSON].
QStringList sqlTokens(const QString &upper)
{
    QString spaced = upper;
    spaced.replace(QLatin1Char('='), QLatin1String(" = "));
    QStringList tokens;
    QString current;
    for (const QChar c : spaced) {
        if (c.isSpace()) {
            if (!current.isEmpty()) { tokens << current; current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.isEmpty()) tokens << current;
    return tokens;
}

void skipExplainModifiers(const QStringList &tokens, int &index)
{
    while (index < tokens.size()) {
        const QString &token = tokens.at(index);
        if (token == QLatin1String("EXTENDED") || token == QLatin1String("PARTITIONS")) {
            ++index;
        } else if (token == QLatin1String("FORMAT")) {
            ++index;
            if (index < tokens.size() && tokens.at(index) == QLatin1String("=")) ++index;
            if (index < tokens.size()) ++index;
        } else {
            return;
        }
    }
}

bool isExplainAliasSafe(const QString &upper, const QString &alias)
{
    const QStringList tokens = sqlTokens(upper);
    if (tokens.isEmpty() || tokens.first() != alias) return false;

    int index = 1;
    skipExplainModifiers(tokens, index);

    // Plain EXPLAIN does not execute the statement, so anything goes.
    if (index >= tokens.size() || tokens.at(index) != QLatin1String("ANALYZE")) return true;

    ++index;
    skipExplainModifiers(tokens, index);
    if (index >= tokens.size()) return false;

    // WITH can introduce UPDATE/DELETE in MySQL CTE syntax, and finding the
    // outer verb past the CTE list needs a real parser. Warning on a read-only
    // CTE SELECT is an acceptable false positive; running a mutation under
    // EXPLAIN ANALYZE without warning is not.
    if (tokens.at(index) == QLatin1String("WITH")) return false;

    const QString &verb = tokens.at(index);
    return !(verb == QLatin1String("UPDATE") || verb == QLatin1String("DELETE")
             || verb == QLatin1String("INSERT") || verb == QLatin1String("REPLACE"));
}

} // namespace

StripResult stripComments(const QString &sql, const StripOptions &options)
{
    return stripCommentsAtDepth(sql, options, 0);
}

QString strippedSQL(const QString &sql, const ServerContext &server)
{
    StripOptions options;
    options.server = server;
    return stripComments(sql, options).sql;
}

bool isQueryExplainable(const QString &sql, const ServerContext &server)
{
    if (sql.isEmpty()) return false;

    StripOptions options;
    options.server = server;
    const StripResult stripped = stripComments(sql, options);
    // A gated body may run or vanish and expose a different leading statement.
    // Do not guess which form can be handed to EXPLAIN.
    if (stripped.indeterminateExecutableComment) return false;

    const QString upper = unwrappedUpper(stripped.sql);
    if (upper.isEmpty()) return false;

    return hasLeadingKeyword(QStringLiteral("SELECT"), upper, false)
        || hasLeadingKeyword(QStringLiteral("WITH"), upper, false);
}

bool isQuerySafeWithoutDestructiveWarning(const QString &sql, const ServerContext &server)
{
    if (sql.isEmpty()) return false;

    StripOptions options;
    options.server = server;
    const StripResult stripped = stripComments(sql, options);
    // Both the executed and the ignored form would have to be safe. When the
    // active form is unknown, require the confirmation.
    if (stripped.indeterminateExecutableComment) return false;

    const QString upper = unwrappedUpper(stripped.sql);
    if (upper.isEmpty()) return false;

    if (hasLeadingKeyword(QStringLiteral("SHOW"), upper)) return true;
    if (hasLeadingKeyword(QStringLiteral("SELECT"), upper)) return true;

    for (const QString &alias : {QStringLiteral("EXPLAIN"), QStringLiteral("DESCRIBE"), QStringLiteral("DESC")}) {
        if (hasLeadingKeyword(alias, upper)) return isExplainAliasSafe(upper, alias);
    }

    return false;
}

bool batchNeedsDestructiveWarning(const QStringList &statements, const ServerContext &server)
{
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) continue;
        if (!isQuerySafeWithoutDestructiveWarning(trimmed, server)) return true;
    }
    return false;
}

} // namespace SASQLClassifier
