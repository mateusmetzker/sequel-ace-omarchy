//
//  SASQLHighlighter.h
//  Sequel Ace (Linux port)
//
//  QSyntaxHighlighter driven by the shared flex lexer; colours come from an
//  SAEditorTheme.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAEditorTheme.h"

#include <QSyntaxHighlighter>

class SASQLHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SASQLHighlighter(QTextDocument *document);
    void setTheme(const SAEditorTheme &theme);
    void setEnabled(bool enabled);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Normal = 0, InComment = 1, InSingleQuote = 2, InDoubleQuote = 3, InBacktick = 4 };
    SAEditorTheme m_theme;
    bool m_enabled = true;
};
