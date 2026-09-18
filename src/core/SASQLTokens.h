//
//  SASQLTokens.h
//  Sequel Ace (Linux port)
//
//  C++ facade over the flex-generated SQL editor lexer (SPEditorTokens.l).
//  Produces tokens with QString (UTF-16) offsets for syntax highlighting.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QString>
#include <QVector>

enum class SASQLTokenType {
    DoubleQuotedText = 1,
    SingleQuotedText = 2,
    Comment = 3,
    BacktickQuotedText = 4,
    ReservedWord = 5,
    Whitespace = 6,
    Numeric = 7,
    Variable = 8,
    Word = 9,
    Other = 10,
};

struct SASQLToken {
    SASQLTokenType type;
    int start;    // UTF-16 offset within the scanned text
    int length;   // UTF-16 length
};

namespace SASQLTokens {

// Tokenizes `text`. When `startInComment` is set the scanner begins inside a
// /* block comment. `endsInComment` reports whether the text ended inside one.
QVector<SASQLToken> tokenize(const QString &text, bool startInComment = false, bool *endsInComment = nullptr);

// Upper-cases reserved words in `text` outside of strings/comments.
QString uppercaseKeywords(const QString &text);

} // namespace SASQLTokens
