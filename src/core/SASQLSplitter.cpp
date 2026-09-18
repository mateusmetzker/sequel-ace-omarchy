//
//  SASQLSplitter.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLSplitter.h"

namespace {

bool isLineBreak(QChar c) { return c == QLatin1Char('\n') || c == QLatin1Char('\r'); }

int endOfLine(const QString &text, int index)
{
    while (index < text.size() && !isLineBreak(text.at(index))) ++index;
    return index;
}

// Detects "DELIMITER <token>" at `index` (case-insensitive). Returns the index
// just after the token and stores it; returns -1 when there is no command.
int matchDelimiterCommand(const QString &text, int index, QString *delimiter)
{
    static const QString keyword = QStringLiteral("delimiter");
    if (index + keyword.size() > text.size()) return -1;
    if (text.mid(index, keyword.size()).compare(keyword, Qt::CaseInsensitive) != 0) return -1;
    int i = index + keyword.size();
    if (i >= text.size() || (text.at(i) != QLatin1Char(' ') && text.at(i) != QLatin1Char('\t'))) return -1;
    while (i < text.size() && (text.at(i) == QLatin1Char(' ') || text.at(i) == QLatin1Char('\t'))) ++i;
    int tokenStart = i;
    while (i < text.size() && !text.at(i).isSpace()) ++i;
    if (i == tokenStart) return -1;
    // The token must be followed by whitespace or the end of the text.
    if (i < text.size() && !text.at(i).isSpace()) return -1;
    *delimiter = text.mid(tokenStart, i - tokenStart);
    return i;
}

} // namespace

bool SASQLSplitter::isOnlyComments(const QString &text, SAStatementRange range)
{
    int i = range.start;
    const int end = range.end();
    while (i < end) {
        const QChar c = text.at(i);
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('#')) { i = endOfLine(text, i); continue; }
        if (c == QLatin1Char('-') && i + 1 < end && text.at(i + 1) == QLatin1Char('-') && (i + 2 >= end || text.at(i + 2).isSpace())) {
            i = endOfLine(text, i);
            continue;
        }
        if (c == QLatin1Char('/') && i + 1 < end && text.at(i + 1) == QLatin1Char('*')) {
            // Conditional comments (/*! ... */) are executable code.
            if (i + 2 < end && text.at(i + 2) == QLatin1Char('!')) return false;
            const int close = text.indexOf(QStringLiteral("*/"), i + 2);
            if (close < 0 || close + 2 > end) return true;
            i = close + 2;
            continue;
        }
        return false;
    }
    return true;
}

SASQLSplitter::SASQLSplitter(bool supportDelimiters, bool noBackslashEscapes)
    : m_supportDelimiters(supportDelimiters), m_noBackslashEscapes(noBackslashEscapes)
{
}

int SASQLSplitter::endOfQuotedString(const QString &text, int index, bool noBackslashEscapes)
{
    const QChar quote = text.at(index);
    const bool allowBackslash = !noBackslashEscapes && quote != QLatin1Char('`');
    int i = index + 1;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (allowBackslash && c == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (c == quote) {
            // A doubled quote is an escaped quote inside the string.
            if (i + 1 < text.size() && text.at(i + 1) == quote) {
                i += 2;
                continue;
            }
            return i;
        }
        ++i;
    }
    return text.size();
}

QString SASQLSplitter::normaliseForExecution(const QString &text, bool noBackslashEscapes)
{
    QString out;
    out.reserve(text.size());
    const int n = text.size();
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            const int end = endOfQuotedString(text, i, noBackslashEscapes);
            out += text.mid(i, qMin(end, n - 1) - i + 1);
            i = end + 1;
            continue;
        }
        if (c == QLatin1Char('\r')) {
            out += QLatin1Char('\n');
            if (i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) ++i;
            ++i;
            continue;
        }
        out += c;
        ++i;
    }
    return out.trimmed();
}

QVector<SAStatementRange> SASQLSplitter::splitIntoRanges(const QString &text) const
{
    QVector<SAStatementRange> ranges;
    const int n = text.size();
    QString delimiter = QStringLiteral(";");
    int statementStart = 0;
    int i = 0;
    bool atStatementStart = true;

    auto finishStatement = [&](int endExclusive) {
        if (endExclusive > statementStart) {
            const SAStatementRange trimmed = trimmedRange(text, {statementStart, endExclusive - statementStart});
            // Statements consisting only of comments would make the server answer
            // "Query was empty"; drop them like whitespace.
            if (!trimmed.isEmpty() && !isOnlyComments(text, trimmed)) ranges.append({statementStart, endExclusive - statementStart});
        }
    };

    while (i < n) {
        const QChar c = text.at(i);

        if (atStatementStart) {
            if (c.isSpace()) { ++i; continue; }
            if (m_supportDelimiters) {
                QString newDelimiter;
                const int after = matchDelimiterCommand(text, i, &newDelimiter);
                if (after >= 0) {
                    delimiter = newDelimiter;
                    i = after;
                    statementStart = i;
                    atStatementStart = true;
                    continue;
                }
            }
            atStatementStart = false;
        }

        if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`')) {
            i = endOfQuotedString(text, i, m_noBackslashEscapes) + 1;
            continue;
        }
        if (c == QLatin1Char('#')) {
            i = endOfLine(text, i);
            continue;
        }
        if (c == QLatin1Char('-') && i + 1 < n && text.at(i + 1) == QLatin1Char('-')
            && (i + 2 >= n || text.at(i + 2).isSpace())) {
            i = endOfLine(text, i);
            continue;
        }
        if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
            const int close = text.indexOf(QStringLiteral("*/"), i + 2);
            i = (close < 0) ? n : close + 2;
            continue;
        }
        if (text.mid(i, delimiter.size()) == delimiter) {
            finishStatement(i);
            i += delimiter.size();
            statementStart = i;
            atStatementStart = true;
            continue;
        }
        ++i;
    }

    if (statementStart < n) {
        // Ignore a trailing DELIMITER command without statements after it.
        bool trailingIsDelimiterCommand = false;
        if (m_supportDelimiters) {
            int k = statementStart;
            while (k < n && text.at(k).isSpace()) ++k;
            QString ignored;
            trailingIsDelimiterCommand = (k < n) && matchDelimiterCommand(text, k, &ignored) >= 0;
        }
        if (!trailingIsDelimiterCommand) finishStatement(n);
    }
    return ranges;
}

QStringList SASQLSplitter::split(const QString &text) const
{
    QStringList statements;
    for (const SAStatementRange &range : splitIntoRanges(text)) {
        const QString statement = text.mid(range.start, range.length).trimmed();
        if (!statement.isEmpty()) statements << statement;
    }
    return statements;
}

SAStatementRange SASQLSplitter::trimmedRange(const QString &text, SAStatementRange range)
{
    int start = qBound(0, range.start, text.size());
    int end = qBound(start, range.end(), text.size());
    while (start < end && text.at(start).isSpace()) ++start;
    while (end > start && text.at(end - 1).isSpace()) --end;
    return {start, end - start};
}

SAStatementRange SASQLSplitter::rangeAtPosition(const QString &text, const QVector<SAStatementRange> &ranges,
                                                int position, bool *lookBehind)
{
    bool wantLookBehind = lookBehind ? *lookBehind : false;
    if (position < 0 || position > text.size() || ranges.isEmpty()) {
        if (lookBehind) *lookBehind = false;
        return {};
    }

    SAStatementRange found;
    bool located = false;

    for (int i = 0; i < ranges.size(); ++i) {
        const SAStatementRange range = ranges.at(i);
        const int rangeEnd = range.end();
        if (rangeEnd < position) continue;

        found = range;
        located = true;

        if (wantLookBehind) {
            bool associateWithPrevious = false;
            // Caret at the very start of the statement range.
            if (position == range.start) associateWithPrevious = true;
            // Caret inside a multi-character delimiter between statements.
            if (!associateWithPrevious && i > 0 && ranges.at(i - 1).end() < position && position < range.start)
                associateWithPrevious = true;
            // Only whitespace before the caret and a line break before the next character.
            if (!associateWithPrevious && position >= range.start) {
                const QString toPrevious = text.mid(range.start, position - range.start);
                if (toPrevious.trimmed().isEmpty()) {
                    for (int j = position; j < rangeEnd; ++j) {
                        const QChar ch = text.at(j);
                        if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t')) continue;
                        if (isLineBreak(ch)) associateWithPrevious = true;
                        break;
                    }
                }
            }
            if (i > 0 && associateWithPrevious) {
                const SAStatementRange previous = ranges.at(i - 1);
                if (!text.mid(previous.start, previous.length).trimmed().isEmpty()) {
                    found = previous;
                    break;
                }
            }
            wantLookBehind = false;
        }
        break;
    }

    if (!located) {
        // Position after the last statement.
        if (wantLookBehind || position == text.size()) {
            found = ranges.last();
            located = true;
        }
    }
    if (lookBehind) *lookBehind = wantLookBehind && located;
    if (!located) return {};

    const SAStatementRange trimmed = trimmedRange(text, found);
    return trimmed.isEmpty() ? SAStatementRange{} : trimmed;
}
