//
//  SASQLSplitter.h
//  Sequel Ace (Linux port)
//
//  Splits SQL scripts into statements. Ports the parts of SPSQLParser the
//  custom query editor relies on: quoted strings (with backslash escapes and
//  doubled quotes), the three comment styles, DELIMITER commands, and the
//  "query at caret" resolution used by "Run Current Query" and current-query
//  highlighting. All positions are QString indices (UTF-16 code units).
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct SAStatementRange {
    int start = 0;
    int length = 0;
    int end() const { return start + length; }
    bool isEmpty() const { return length <= 0; }
};

class SASQLSplitter {
public:
    explicit SASQLSplitter(bool supportDelimiters = true, bool noBackslashEscapes = false);

    // Ranges of statements, excluding the delimiters themselves and any
    // DELIMITER commands. Ranges may include surrounding whitespace; use
    // trimmedRange() when a tight range is needed.
    QVector<SAStatementRange> splitIntoRanges(const QString &text) const;

    // Trimmed, non-empty statement strings.
    QStringList split(const QString &text) const;

    // Port of -[SPCustomQuery queryRangeAtPosition:lookBehind:]. Returns the
    // trimmed range of the statement at `position`. When *lookBehind is true, a
    // caret placed just after a statement (before the next one starts) is
    // associated with the previous statement; on return it tells whether that
    // association happened. Returns an empty range when nothing is found.
    static SAStatementRange rangeAtPosition(const QString &text, const QVector<SAStatementRange> &ranges,
                                            int position, bool *lookBehind);

    static SAStatementRange trimmedRange(const QString &text, SAStatementRange range);

    // Index of the end of a quoted string starting at `index` (position of the
    // closing quote), or text.size() when unterminated.
    static int endOfQuotedString(const QString &text, int index, bool noBackslashEscapes);

    // True when the range holds nothing but whitespace and comments.
    static bool isOnlyComments(const QString &text, SAStatementRange range);

    // Port of -[SPSQLParser normaliseQueryForExecution:]. Trims whitespace at
    // both ends and folds CRLF/CR line endings to LF outside of quoted
    // strings, where a CR is data.
    static QString normaliseForExecution(const QString &text, bool noBackslashEscapes = false);

private:
    bool m_supportDelimiters;
    bool m_noBackslashEscapes;
};
