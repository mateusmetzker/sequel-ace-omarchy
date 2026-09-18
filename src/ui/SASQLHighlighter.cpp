//
//  SASQLHighlighter.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLHighlighter.h"
#include "SASQLTokens.h"

#include <QTextCharFormat>

SASQLHighlighter::SASQLHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document), m_theme(SAEditorTheme::defaultLight())
{
}

void SASQLHighlighter::setTheme(const SAEditorTheme &theme)
{
    m_theme = theme;
    rehighlight();
}

void SASQLHighlighter::setEnabled(bool enabled)
{
    m_enabled = enabled;
    rehighlight();
}

void SASQLHighlighter::highlightBlock(const QString &text)
{
    if (!m_enabled) {
        setCurrentBlockState(Normal);
        return;
    }
    const int previous = previousBlockState() < 0 ? Normal : previousBlockState();
    QString scanned = text;
    int offset = 0;
    bool startInComment = previous == InComment;
    if (previous == InSingleQuote) { scanned.prepend(QLatin1Char('\'')); offset = 1; }
    else if (previous == InDoubleQuote) { scanned.prepend(QLatin1Char('"')); offset = 1; }
    else if (previous == InBacktick) { scanned.prepend(QLatin1Char('`')); offset = 1; }

    bool endsInComment = false;
    const QVector<SASQLToken> tokens = SASQLTokens::tokenize(scanned, startInComment, &endsInComment);

    int state = endsInComment ? InComment : Normal;
    for (const SASQLToken &token : tokens) {
        QTextCharFormat format;
        switch (token.type) {
        case SASQLTokenType::SingleQuotedText:
        case SASQLTokenType::DoubleQuotedText:
            format.setForeground(m_theme.string);
            break;
        case SASQLTokenType::BacktickQuotedText:
            format.setForeground(m_theme.backtick);
            break;
        case SASQLTokenType::Comment:
            format.setForeground(m_theme.comment);
            break;
        case SASQLTokenType::ReservedWord:
            format.setForeground(m_theme.keyword);
            format.setFontWeight(QFont::DemiBold);
            break;
        case SASQLTokenType::Numeric:
            format.setForeground(m_theme.number);
            break;
        case SASQLTokenType::Variable:
            format.setForeground(m_theme.variable);
            break;
        default:
            continue;
        }
        const int start = token.start - offset;
        const int length = token.length + qMin(0, token.start - offset) ;
        if (start + token.length <= 0) continue;
        setFormat(qMax(0, start), length, format);

        // An unterminated quoted string at the end of the line continues on the next block.
        if (&token == &tokens.last()) {
            const QString tokenText = scanned.mid(token.start, token.length);
            const QChar quote = tokenText.isEmpty() ? QChar() : tokenText.at(0);
            const bool unterminated = tokenText.size() < 2 || tokenText.at(tokenText.size() - 1) != quote
                                      || (tokenText.size() >= 2 && tokenText.at(tokenText.size() - 2) == QLatin1Char('\\') && quote != QLatin1Char('`'));
            if (token.type == SASQLTokenType::SingleQuotedText && unterminated) state = InSingleQuote;
            else if (token.type == SASQLTokenType::DoubleQuotedText && unterminated) state = InDoubleQuote;
            else if (token.type == SASQLTokenType::BacktickQuotedText && unterminated) state = InBacktick;
        }
    }
    setCurrentBlockState(state);
}
