//
//  SASQLEditor.h
//  Sequel Ace (Linux port)
//
//  SQL text editor: line numbers, syntax highlighting, current-query
//  highlighting, auto-pairing, auto-indent, comment toggling and a completion
//  popup fed with keywords, functions, tables and columns.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAEditorTheme.h"
#include "SASQLSplitter.h"

#include <QPlainTextEdit>
#include <QStringList>
#include <QVector>

class SASQLHighlighter;
class QListWidget;
class QTimer;

class SASQLEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SASQLEditor(QWidget *parent = nullptr);

    void applyTheme(const SAEditorTheme &theme);
    void setEditorFont(const QFont &font);
    void setHighlightCurrentQuery(bool on);
    void setLineNumbersVisible(bool visible);
    void setSyntaxHighlightingEnabled(bool on);
    void reloadPreferences();

    // Statement handling (positions in QString units).
    QVector<SAStatementRange> statementRanges();
    SAStatementRange currentStatementRange();
    QString currentStatement();
    QString selectedText() const;
    void selectStatement(const SAStatementRange &range);

    void toggleCommentOnSelection();
    void uppercaseKeywords();

    // Completion data.
    void setCompletionTables(const QStringList &tables);
    void setCompletionColumns(const QStringList &columns);
    void setColumnsLoader(std::function<void(const QString &table, std::function<void(const QStringList &)>)> loader) { m_columnsLoader = std::move(loader); }

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

Q_SIGNALS:
    void currentStatementChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void updateCurrentQueryHighlight();
    void invalidateRanges() { m_rangesValid = false; }
    void showCompletion(bool automatic);
    void hideCompletion();
    void insertCompletion(const QString &text);
    QString wordBeforeCursor(int *start = nullptr) const;
    bool handleAutoPair(QKeyEvent *event);
    void handleNewline();
    void indentSelection(bool unindent);

    QWidget *m_lineNumberArea;
    SASQLHighlighter *m_highlighter;
    SAEditorTheme m_theme;
    bool m_highlightCurrentQuery = true;
    bool m_lineNumbersVisible = true;
    QVector<SAStatementRange> m_ranges;
    bool m_rangesValid = false;
    SAStatementRange m_currentRange;

    QListWidget *m_completionPopup = nullptr;
    QTimer *m_completionTimer;
    QStringList m_keywords;
    QStringList m_functions;
    QStringList m_tables;
    QStringList m_columns;
    std::function<void(const QString &, std::function<void(const QStringList &)>)> m_columnsLoader;
    bool m_autoPair = true;
    bool m_autoIndent = true;
    bool m_softIndent = false;
    int m_softIndentWidth = 2;
    bool m_autoComplete = true;
    double m_autoCompleteDelay = 1.5;
    bool m_completeWithBackticks = true;
};
