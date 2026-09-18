//
//  SAMCPReadOnlyGuard.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAMCPReadOnlyGuard.h"

#include <QRegularExpression>
#include <QSet>

namespace {

// Replaces the contents of '...'/"..."/`...` literals with 'x' while
// leaving everything else (keywords, punctuation, whitespace, the quote
// delimiters themselves) untouched. Lets keyword/structure scans run
// safely without ever matching text that is really inside a literal.
QString maskLiterals(const QString &s)
{
    QString out;
    out.reserve(s.size());
    int i = 0;
    const int n = s.size();
    while (i < n) {
        const QChar c = s[i];
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            const QChar quote = c;
            out += c;
            ++i;
            while (i < n) {
                const QChar d = s[i];
                if (d == QLatin1Char('\\') && i + 1 < n) {
                    out += QLatin1Char('x');
                    out += QLatin1Char('x');
                    i += 2;
                    continue;
                }
                if (d == quote) {
                    if (i + 1 < n && s[i + 1] == quote) {
                        out += QLatin1Char('x');
                        i += 2;
                        continue;
                    }
                    out += quote;
                    ++i;
                    break;
                }
                out += QLatin1Char('x');
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('`')) {
            out += c;
            ++i;
            while (i < n) {
                const QChar d = s[i];
                if (d == QLatin1Char('`')) {
                    if (i + 1 < n && s[i + 1] == QLatin1Char('`')) {
                        out += QLatin1Char('x');
                        i += 2;
                        continue;
                    }
                    out += QLatin1Char('`');
                    ++i;
                    break;
                }
                out += QLatin1Char('x');
                ++i;
            }
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// True when there is a data-modifying keyword at paren-depth 0 in `masked`
// (already literal-masked). Used to catch "WITH x AS (SELECT ...) DELETE
// FROM t" — the CTE body sits inside parentheses, the DML statement after
// it does not.
bool containsTopLevelWriteKeyword(const QString &masked)
{
    static const QSet<QString> writeKeywords = {
        QStringLiteral("INSERT"), QStringLiteral("UPDATE"), QStringLiteral("DELETE"),
        QStringLiteral("REPLACE"), QStringLiteral("ALTER"), QStringLiteral("DROP"),
        QStringLiteral("CREATE"), QStringLiteral("TRUNCATE"), QStringLiteral("GRANT"),
        QStringLiteral("REVOKE"), QStringLiteral("SET"), QStringLiteral("CALL"),
        QStringLiteral("LOCK"), QStringLiteral("RENAME"), QStringLiteral("START"),
        QStringLiteral("BEGIN"), QStringLiteral("COMMIT"), QStringLiteral("ROLLBACK"),
        QStringLiteral("INTO"),
    };
    int depth = 0;
    int i = 0;
    const int n = masked.size();
    while (i < n) {
        const QChar c = masked[i];
        if (c == QLatin1Char('(')) { ++depth; ++i; continue; }
        if (c == QLatin1Char(')')) { if (depth > 0) --depth; ++i; continue; }
        if (depth == 0 && (c.isLetter() || c == QLatin1Char('_'))) {
            const int start = i;
            while (i < n && (masked[i].isLetterOrNumber() || masked[i] == QLatin1Char('_')))
                ++i;
            if (writeKeywords.contains(masked.mid(start, i - start).toUpper()))
                return true;
            continue;
        }
        ++i;
    }
    return false;
}

QString firstWordOf(const QString &s)
{
    int i = 0;
    const int n = s.size();
    while (i < n && s[i].isSpace())
        ++i;
    const int start = i;
    while (i < n && (s[i].isLetterOrNumber() || s[i] == QLatin1Char('_')))
        ++i;
    return s.mid(start, i - start).toUpper();
}

QString escapeLiteral(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

namespace SAMCPReadOnlyGuard {

QString stripComments(const QString &sql, bool *sawExecutableComment)
{
    if (sawExecutableComment)
        *sawExecutableComment = false;

    QString out;
    out.reserve(sql.size());
    int i = 0;
    const int n = sql.size();
    while (i < n) {
        const QChar c = sql[i];

        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            const QChar quote = c;
            out += c;
            ++i;
            while (i < n) {
                const QChar d = sql[i];
                if (d == QLatin1Char('\\') && i + 1 < n) {
                    out += d;
                    out += sql[i + 1];
                    i += 2;
                    continue;
                }
                out += d;
                if (d == quote) {
                    if (i + 1 < n && sql[i + 1] == quote) {
                        out += sql[i + 1];
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (c == QLatin1Char('`')) {
            out += c;
            ++i;
            while (i < n) {
                const QChar d = sql[i];
                out += d;
                if (d == QLatin1Char('`')) {
                    if (i + 1 < n && sql[i + 1] == QLatin1Char('`')) {
                        out += sql[i + 1];
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (c == QLatin1Char('/') && i + 1 < n && sql[i + 1] == QLatin1Char('*')) {
            const int contentStart = i + 2;
            bool executable = false;
            if (contentStart < n && sql[contentStart] == QLatin1Char('!'))
                executable = true;
            else if (contentStart + 1 < n
                     && (sql[contentStart] == QLatin1Char('M') || sql[contentStart] == QLatin1Char('m'))
                     && sql[contentStart + 1] == QLatin1Char('!'))
                executable = true;

            int end = sql.indexOf(QStringLiteral("*/"), contentStart);
            end = (end < 0) ? n : end + 2;

            if (executable && sawExecutableComment)
                *sawExecutableComment = true;

            out += QLatin1Char(' ');
            i = end;
            continue;
        }

        // MySQL opens a `--` comment only when whitespace or a control
        // character follows. QChar::isSpace() would also accept U+00A0, which
        // the server treats as an ordinary character, so this would hide text
        // the server still parses. Same rule as SASQLClassifier.
        if (c == QLatin1Char('-') && i + 1 < n && sql[i + 1] == QLatin1Char('-')
            && (i + 2 >= n || sql[i + 2].unicode() <= 0x20)) {
            int end = sql.indexOf(QLatin1Char('\n'), i);
            end = (end < 0) ? n : end;
            out += QLatin1Char(' ');
            i = end;
            continue;
        }

        if (c == QLatin1Char('#')) {
            int end = sql.indexOf(QLatin1Char('\n'), i);
            end = (end < 0) ? n : end;
            out += QLatin1Char(' ');
            i = end;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

QString bindParameters(const QString &sql, const QStringList &params)
{
    QString out;
    out.reserve(sql.size());
    int paramIndex = 0;
    int i = 0;
    const int n = sql.size();
    while (i < n) {
        const QChar c = sql[i];

        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            const QChar quote = c;
            const int start = i;
            ++i;
            while (i < n) {
                const QChar d = sql[i];
                if (d == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }
                if (d == quote) {
                    if (i + 1 < n && sql[i + 1] == quote) { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            out += sql.mid(start, i - start);
            continue;
        }

        if (c == QLatin1Char('`')) {
            const int start = i;
            ++i;
            while (i < n) {
                const QChar d = sql[i];
                if (d == QLatin1Char('`')) {
                    if (i + 1 < n && sql[i + 1] == QLatin1Char('`')) { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            out += sql.mid(start, i - start);
            continue;
        }

        if (c == QLatin1Char('/') && i + 1 < n && sql[i + 1] == QLatin1Char('*')) {
            int end = sql.indexOf(QStringLiteral("*/"), i + 2);
            end = (end < 0) ? n : end + 2;
            out += sql.mid(i, end - i);
            i = end;
            continue;
        }

        // MySQL opens a `--` comment only when whitespace or a control
        // character follows. QChar::isSpace() would also accept U+00A0, which
        // the server treats as an ordinary character, so this would hide text
        // the server still parses. Same rule as SASQLClassifier.
        if (c == QLatin1Char('-') && i + 1 < n && sql[i + 1] == QLatin1Char('-')
            && (i + 2 >= n || sql[i + 2].unicode() <= 0x20)) {
            int end = sql.indexOf(QLatin1Char('\n'), i);
            end = (end < 0) ? n : end;
            out += sql.mid(i, end - i);
            i = end;
            continue;
        }

        if (c == QLatin1Char('#')) {
            int end = sql.indexOf(QLatin1Char('\n'), i);
            end = (end < 0) ? n : end;
            out += sql.mid(i, end - i);
            i = end;
            continue;
        }

        if (c == QLatin1Char('?')) {
            if (paramIndex < params.size())
                out += escapeLiteral(params[paramIndex++]);
            else
                out += c;
            ++i;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

bool isExplainAnalyze(const QString &sql)
{
    const QString stripped = stripComments(sql).trimmed();
    const QStringList tokens = stripped.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty() || tokens.first().compare(QStringLiteral("EXPLAIN"), Qt::CaseInsensitive) != 0)
        return false;

    static const QSet<QString> bodyStarters = {
        QStringLiteral("SELECT"), QStringLiteral("INSERT"), QStringLiteral("UPDATE"),
        QStringLiteral("DELETE"), QStringLiteral("REPLACE"), QStringLiteral("WITH"),
    };
    for (int idx = 1; idx < tokens.size(); ++idx) {
        QString upper = tokens[idx].toUpper();
        if (upper.startsWith(QStringLiteral("ANALYZE")))
            return true;
        const int eq = upper.indexOf(QLatin1Char('='));
        const QString bare = (eq >= 0) ? upper.left(eq) : upper;
        if (bodyStarters.contains(bare))
            return false;
    }
    return false;
}

Verdict checkReadOnly(const QString &sql)
{
    if (isExplainAnalyze(sql))
        return {false, QStringLiteral("EXPLAIN ANALYZE executes the query; not allowed in read-only mode.")};

    bool sawExecutableComment = false;
    const QString stripped = stripComments(sql, &sawExecutableComment).trimmed();

    if (sawExecutableComment)
        return {false, QStringLiteral("Executable version comments (/*! */ or /*M! */) are not allowed in read-only mode.")};

    if (stripped.isEmpty())
        return {false, QStringLiteral("Empty statement.")};

    const QString masked = maskLiterals(stripped);

    {
        QString body = masked.trimmed();
        if (body.endsWith(QLatin1Char(';')))
            body.chop(1);
        if (body.contains(QLatin1Char(';')))
            return {false, QStringLiteral("Multiple statements are not allowed in read-only mode.")};
    }

    const QString upperMasked = masked.toUpper();
    for (const QString &kw : {QStringLiteral("OUTFILE"), QStringLiteral("DUMPFILE"), QStringLiteral("LOAD_FILE")}) {
        if (upperMasked.contains(kw))
            return {false, QStringLiteral("%1 is not allowed in read-only mode.").arg(kw)};
    }

    const QString firstWord = firstWordOf(stripped);

    if (firstWord == QStringLiteral("EXPLAIN"))
        return {true, QString()}; // EXPLAIN ANALYZE was already rejected above.

    static const QSet<QString> readOnlyStarters = {
        QStringLiteral("SELECT"), QStringLiteral("SHOW"), QStringLiteral("DESCRIBE"),
        QStringLiteral("DESC"), QStringLiteral("WITH"),
    };
    if (!readOnlyStarters.contains(firstWord))
        return {false, QStringLiteral("Only SELECT/SHOW/DESCRIBE/EXPLAIN statements are allowed in read-only mode.")};

    if (firstWord == QStringLiteral("WITH") && containsTopLevelWriteKeyword(masked))
        return {false, QStringLiteral("Only SELECT/SHOW/DESCRIBE/EXPLAIN statements are allowed in read-only mode.")};

    return {true, QString()};
}

} // namespace SAMCPReadOnlyGuard
