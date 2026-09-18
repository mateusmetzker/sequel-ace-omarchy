//
//  SASQLEditor.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SASQLEditor.h"
#include "SAIcons.h"
#include "SAPreferences.h"
#include "SASQLHighlighter.h"
#include "SASQLTokens.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>

namespace {

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(SASQLEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }
private:
    SASQLEditor *m_editor;
};

QStringList loadTokenList(const QString &key)
{
    static QJsonObject tokens;
    if (tokens.isEmpty()) {
        QFile file(QStringLiteral(":/resources/CompletionTokens.json"));
        if (file.open(QIODevice::ReadOnly)) tokens = QJsonDocument::fromJson(file.readAll()).object();
    }
    QStringList list;
    for (const QJsonValue &v : tokens.value(key).toArray()) list << v.toString();
    return list;
}

bool needsBackticks(const QString &identifier)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$"));
    return !plain.match(identifier).hasMatch();
}

} // namespace

SASQLEditor::SASQLEditor(QWidget *parent)
    : QPlainTextEdit(parent), m_theme(SAEditorTheme::defaultLight())
{
    m_lineNumberArea = new LineNumberArea(this);
    m_highlighter = new SASQLHighlighter(document());
    m_completionTimer = new QTimer(this);
    m_completionTimer->setSingleShot(true);
    connect(m_completionTimer, &QTimer::timeout, this, [this]() { showCompletion(true); });

    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &SASQLEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &SASQLEditor::updateCurrentQueryHighlight);
    connect(this, &QPlainTextEdit::textChanged, this, [this]() { invalidateRanges(); updateCurrentQueryHighlight(); });

    m_keywords = loadTokenList(QStringLiteral("core_keywords"));
    m_functions = loadTokenList(QStringLiteral("core_builtin_functions"));

    reloadPreferences();
    connect(&SAPreferences::instance(), &SAPreferences::changed, this, [this](const QString &) { reloadPreferences(); });
    updateLineNumberAreaWidth();
}

void SASQLEditor::reloadPreferences()
{
    SAPreferences &prefs = SAPreferences::instance();
    m_autoPair = prefs.boolFor(SAPreferences::CustomQueryAutoPairCharacters);
    m_autoIndent = prefs.boolFor(SAPreferences::CustomQueryAutoIndent);
    m_softIndent = prefs.boolFor(SAPreferences::CustomQuerySoftIndent);
    m_softIndentWidth = qMax(1, prefs.intFor(SAPreferences::CustomQuerySoftIndentWidth));
    m_autoComplete = prefs.value(QStringLiteral("CustomQueryAutoComplete")).isValid() ? prefs.boolFor(QStringLiteral("CustomQueryAutoComplete")) : true;
    m_autoCompleteDelay = prefs.value(QStringLiteral("CustomQueryAutoCompleteDelay")).isValid() ? prefs.doubleFor(QStringLiteral("CustomQueryAutoCompleteDelay")) : 1.5;
    m_completeWithBackticks = prefs.value(QStringLiteral("SPCustomQueryEditorCompleteWithBackticks")).isValid() ? prefs.boolFor(QStringLiteral("SPCustomQueryEditorCompleteWithBackticks")) : true;

    QFont font;
    const QString fontSpec = prefs.stringFor(SAPreferences::EditorFont);
    if (fontSpec.isEmpty() || !font.fromString(fontSpec)) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(11);
    }
    setEditorFont(font);
    applyTheme(SAEditorTheme::themeNamed(prefs.stringFor(SAPreferences::EditorTheme)));
    setHighlightCurrentQuery(prefs.boolFor(SAPreferences::CustomQueryHighlightCurrentQuery));
    setSyntaxHighlightingEnabled(prefs.boolFor(SAPreferences::CustomQueryEnableSyntaxHighlighting));
}

void SASQLEditor::applyTheme(const SAEditorTheme &theme)
{
    m_theme = theme;
    QPalette p = palette();
    p.setColor(QPalette::Base, theme.background);
    p.setColor(QPalette::Text, theme.foreground);
    p.setColor(QPalette::Highlight, theme.selection);
    p.setColor(QPalette::HighlightedText, theme.foreground);
    setPalette(p);
    m_highlighter->setTheme(theme);
    updateCurrentQueryHighlight();
    m_lineNumberArea->update();
}

void SASQLEditor::setEditorFont(const QFont &font)
{
    setFont(font);
    document()->setDefaultFont(font);
    const int tabWidth = qMax(1, SAPreferences::instance().intFor(SAPreferences::CustomQueryEditorTabStopWidth));
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * tabWidth);
    updateLineNumberAreaWidth();
}

void SASQLEditor::setHighlightCurrentQuery(bool on)
{
    m_highlightCurrentQuery = on;
    updateCurrentQueryHighlight();
}

void SASQLEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbersVisible = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
}

void SASQLEditor::setSyntaxHighlightingEnabled(bool on)
{
    m_highlighter->setEnabled(on);
}

// ---- line numbers -----------------------------------------------------------------

int SASQLEditor::lineNumberAreaWidth() const
{
    if (!m_lineNumbersVisible) return 0;
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void SASQLEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void SASQLEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void SASQLEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void SASQLEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    QColor bg = m_theme.background.lightness() < 128 ? m_theme.background.lighter(125) : m_theme.background.darker(104);
    painter.fillRect(event->rect(), bg);
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentBlock = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QColor color = m_theme.foreground;
            color.setAlphaF(blockNumber == currentBlock ? 0.9 : 0.45);
            painter.setPen(color);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// ---- statements ---------------------------------------------------------------------

QVector<SAStatementRange> SASQLEditor::statementRanges()
{
    if (!m_rangesValid) {
        m_ranges = SASQLSplitter().splitIntoRanges(toPlainText());
        m_rangesValid = true;
    }
    return m_ranges;
}

SAStatementRange SASQLEditor::currentStatementRange()
{
    const QString text = toPlainText();
    bool lookBehind = true;
    return SASQLSplitter::rangeAtPosition(text, statementRanges(), textCursor().position(), &lookBehind);
}

QString SASQLEditor::currentStatement()
{
    const SAStatementRange range = currentStatementRange();
    return range.isEmpty() ? QString() : toPlainText().mid(range.start, range.length);
}

QString SASQLEditor::selectedText() const
{
    return textCursor().selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
}

void SASQLEditor::selectStatement(const SAStatementRange &range)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(range.start);
    cursor.setPosition(range.end(), QTextCursor::KeepAnchor);
    setTextCursor(cursor);
}

void SASQLEditor::updateCurrentQueryHighlight()
{
    QList<QTextEdit::ExtraSelection> selections;
    SAStatementRange range;
    if (m_highlightCurrentQuery && !isReadOnly()) {
        range = currentStatementRange();
        if (!range.isEmpty()) {
            QTextCharFormat format;
            format.setBackground(m_theme.lineHighlight);
            format.setProperty(QTextFormat::FullWidthSelection, true);
            QTextBlock block = document()->findBlock(range.start);
            const QTextBlock last = document()->findBlock(qMax(range.start, range.end() - 1));
            while (block.isValid()) {
                QTextEdit::ExtraSelection selection;
                selection.cursor = QTextCursor(block);
                selection.format = format;
                selections << selection;
                if (block == last) break;
                block = block.next();
            }
        }
    }
    setExtraSelections(selections);
    if (range.start != m_currentRange.start || range.length != m_currentRange.length) {
        m_currentRange = range;
        Q_EMIT currentStatementChanged();
    }
}

// ---- editing helpers ----------------------------------------------------------------

void SASQLEditor::toggleCommentOnSelection()
{
    QTextCursor cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (end > start && last.position() == end) last = last.previous();
    bool allCommented = true;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        if (!b.text().trimmed().isEmpty() && !b.text().trimmed().startsWith(QLatin1String("-- "))) allCommented = false;
        if (b == last) break;
    }
    cursor.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QTextCursor c(b);
        const QString text = b.text();
        if (allCommented) {
            const int idx = text.indexOf(QLatin1String("-- "));
            if (idx >= 0) {
                c.setPosition(b.position() + idx);
                c.setPosition(b.position() + idx + 3, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else if (!text.trimmed().isEmpty()) {
            c.setPosition(b.position());
            c.insertText(QStringLiteral("-- "));
        }
        if (b == last) break;
    }
    cursor.endEditBlock();
}

void SASQLEditor::uppercaseKeywords()
{
    QTextCursor cursor = textCursor();
    const bool hasSelection = cursor.hasSelection();
    const int start = hasSelection ? cursor.selectionStart() : 0;
    const int end = hasSelection ? cursor.selectionEnd() : toPlainText().size();
    const QString text = toPlainText().mid(start, end - start);
    const QString upper = SASQLTokens::uppercaseKeywords(text);
    if (upper == text) return;
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(upper);
    cursor.endEditBlock();
}

void SASQLEditor::indentSelection(bool unindent)
{
    QTextCursor cursor = textCursor();
    const QString indent = m_softIndent ? QString(m_softIndentWidth, QLatin1Char(' ')) : QStringLiteral("\t");
    if (!cursor.hasSelection() && !unindent) {
        cursor.insertText(indent);
        return;
    }
    QTextBlock first = document()->findBlock(cursor.selectionStart());
    QTextBlock last = document()->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last.position() == cursor.selectionEnd() && last != first) last = last.previous();
    cursor.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QTextCursor c(b);
        if (unindent) {
            const QString text = b.text();
            int remove = 0;
            if (text.startsWith(QLatin1Char('\t'))) remove = 1;
            else { while (remove < text.size() && remove < m_softIndentWidth && text.at(remove) == QLatin1Char(' ')) ++remove; }
            if (remove) {
                c.setPosition(b.position());
                c.setPosition(b.position() + remove, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else {
            c.setPosition(b.position());
            c.insertText(indent);
        }
        if (b == last) break;
    }
    cursor.endEditBlock();
}

void SASQLEditor::handleNewline()
{
    QTextCursor cursor = textCursor();
    QString indent;
    if (m_autoIndent) {
        const QString line = cursor.block().text();
        int i = 0;
        while (i < line.size() && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t'))) ++i;
        indent = line.left(i);
    }
    cursor.insertText(QLatin1Char('\n') + indent);
}

bool SASQLEditor::handleAutoPair(QKeyEvent *event)
{
    if (!m_autoPair || event->text().size() != 1) return false;
    const QChar ch = event->text().at(0);
    static const QHash<QChar, QChar> pairs{{'(', ')'}, {'[', ']'}, {'{', '}'}, {'\'', '\''}, {'"', '"'}, {'`', '`'}};
    QTextCursor cursor = textCursor();
    const QString text = toPlainText();
    const int pos = cursor.position();
    const QChar next = pos < text.size() ? text.at(pos) : QChar();
    const QChar prev = pos > 0 ? text.at(pos - 1) : QChar();

    if (cursor.hasSelection() && pairs.contains(ch)) {
        // Wrap the selection.
        const QString selected = cursor.selectedText();
        cursor.insertText(ch + selected + pairs.value(ch));
        return true;
    }
    // Typing the closing character right before an identical one just steps over it.
    if ((ch == ')' || ch == ']' || ch == '}' || ch == '\'' || ch == '"' || ch == '`') && next == ch) {
        cursor.movePosition(QTextCursor::Right);
        setTextCursor(cursor);
        return true;
    }
    if (pairs.contains(ch)) {
        // Do not pair quotes inside words (e.g. don't) or right after a backslash.
        if ((ch == '\'' || ch == '"' || ch == '`') && (prev.isLetterOrNumber() || prev == QLatin1Char('\\'))) return false;
        if (!next.isNull() && (next.isLetterOrNumber() || next == QLatin1Char('_'))) return false;
        cursor.insertText(QString(ch) + pairs.value(ch));
        cursor.movePosition(QTextCursor::Left);
        setTextCursor(cursor);
        return true;
    }
    return false;
}

void SASQLEditor::keyPressEvent(QKeyEvent *event)
{
    if (m_completionPopup && m_completionPopup->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Escape:
            hideCompletion();
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            if (m_completionPopup->currentItem()) insertCompletion(m_completionPopup->currentItem()->text());
            hideCompletion();
            event->accept();
            return;
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QApplication::sendEvent(m_completionPopup, event);
            event->accept();
            return;
        default:
            break;
        }
    }

    if (event->key() == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier)) {
        showCompletion(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && !isReadOnly()) {
        showCompletion(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Slash && (event->modifiers() & Qt::ControlModifier)) {
        toggleCommentOnSelection();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier)) {
        indentSelection(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backtab) {
        indentSelection(true);
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier))) {
        handleNewline();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace && m_autoPair && !textCursor().hasSelection()) {
        const QString text = toPlainText();
        const int pos = textCursor().position();
        if (pos > 0 && pos < text.size()) {
            const QChar prev = text.at(pos - 1), next = text.at(pos);
            static const QHash<QChar, QChar> pairs{{'(', ')'}, {'[', ']'}, {'{', '}'}, {'\'', '\''}, {'"', '"'}, {'`', '`'}};
            if (pairs.contains(prev) && pairs.value(prev) == next) {
                QTextCursor c = textCursor();
                c.deleteChar();
                c.deletePreviousChar();
                event->accept();
                return;
            }
        }
    }
    if (!isReadOnly() && handleAutoPair(event)) {
        event->accept();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    if (m_completionPopup && m_completionPopup->isVisible()) {
        // Keep filtering as the user types.
        showCompletion(true);
    } else if (m_autoComplete && !isReadOnly() && !event->text().isEmpty() && (event->text().at(0).isLetter() || event->text().at(0) == QLatin1Char('_'))) {
        m_completionTimer->start(int(m_autoCompleteDelay * 1000));
    } else {
        m_completionTimer->stop();
    }
}

void SASQLEditor::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    if (m_completionPopup && m_completionPopup->isVisible() && !m_completionPopup->hasFocus()) hideCompletion();
}

// ---- completion --------------------------------------------------------------------

void SASQLEditor::setCompletionTables(const QStringList &tables) { m_tables = tables; }
void SASQLEditor::setCompletionColumns(const QStringList &columns) { m_columns = columns; }

QString SASQLEditor::wordBeforeCursor(int *start) const
{
    const QString text = toPlainText();
    int pos = textCursor().position();
    int i = pos;
    while (i > 0) {
        const QChar c = text.at(i - 1);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$') || c == QLatin1Char('.') || c == QLatin1Char('`')) --i;
        else break;
    }
    if (start) *start = i;
    return text.mid(i, pos - i);
}

void SASQLEditor::showCompletion(bool automatic)
{
    int start = 0;
    QString prefix = wordBeforeCursor(&start);
    QString qualifier;
    const int dot = prefix.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0) {
        qualifier = prefix.left(dot);
        qualifier.remove(QLatin1Char('`'));
        prefix = prefix.mid(dot + 1);
    }
    prefix.remove(QLatin1Char('`'));
    if (automatic && prefix.size() < 2) { hideCompletion(); return; }

    if (!qualifier.isEmpty() && m_columnsLoader && m_tables.contains(qualifier, Qt::CaseInsensitive)) {
        // table.column completion: fetch the columns then show.
        m_columnsLoader(qualifier, [this, automatic](const QStringList &columns) {
            m_columns = columns;
            QStringList words = columns;
            int s = 0;
            QString p = wordBeforeCursor(&s);
            const int d = p.lastIndexOf(QLatin1Char('.'));
            p = d >= 0 ? p.mid(d + 1) : p;
            p.remove(QLatin1Char('`'));
            QStringList matches;
            for (const QString &w : words) if (w.startsWith(p, Qt::CaseInsensitive)) matches << w;
            if (matches.isEmpty()) { hideCompletion(); return; }
            if (!m_completionPopup) {
                m_completionPopup = new QListWidget(this);
                m_completionPopup->setWindowFlags(Qt::ToolTip);
                m_completionPopup->setFocusPolicy(Qt::NoFocus);
                connect(m_completionPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { insertCompletion(item->text()); hideCompletion(); });
            }
            m_completionPopup->clear();
            for (const QString &m : matches) m_completionPopup->addItem(new QListWidgetItem(SAIcons::icon(SAIcons::Glyph::Structure), m));
            m_completionPopup->setCurrentRow(0);
            const QRect r = cursorRect();
            m_completionPopup->move(viewport()->mapToGlobal(QPoint(r.left(), r.bottom() + 2)));
            m_completionPopup->resize(320, qMin(240, 22 * matches.size() + 6));
            m_completionPopup->show();
        });
        return;
    }

    struct Candidate { QString text; SAIcons::Glyph glyph; int rank; };
    QVector<Candidate> candidates;
    auto consider = [&](const QStringList &list, SAIcons::Glyph glyph, int rank) {
        for (const QString &w : list) {
            if (prefix.isEmpty() || w.startsWith(prefix, Qt::CaseInsensitive)) {
                if (w.compare(prefix, Qt::CaseInsensitive) == 0) continue;
                candidates.append({w, glyph, rank});
            }
        }
    };
    consider(m_columns, SAIcons::Glyph::Structure, 0);
    consider(m_tables, SAIcons::Glyph::Table, 1);
    consider(m_keywords, SAIcons::Glyph::Query, 2);
    consider(m_functions, SAIcons::Glyph::Function, 3);
    if (candidates.isEmpty() || (automatic && candidates.size() > 60)) { hideCompletion(); return; }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.text.compare(b.text, Qt::CaseInsensitive) < 0;
    });

    if (!m_completionPopup) {
        m_completionPopup = new QListWidget(this);
        m_completionPopup->setWindowFlags(Qt::ToolTip);
        m_completionPopup->setFocusPolicy(Qt::NoFocus);
        connect(m_completionPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { insertCompletion(item->text()); hideCompletion(); });
    }
    m_completionPopup->clear();
    int shown = 0;
    QSet<QString> seen;
    for (const Candidate &c : candidates) {
        if (seen.contains(c.text.toLower())) continue;
        seen.insert(c.text.toLower());
        m_completionPopup->addItem(new QListWidgetItem(SAIcons::icon(c.glyph), c.text));
        if (++shown >= 200) break;
    }
    m_completionPopup->setCurrentRow(0);
    const QRect r = cursorRect();
    m_completionPopup->move(viewport()->mapToGlobal(QPoint(r.left(), r.bottom() + 2)));
    m_completionPopup->resize(320, qMin(240, 22 * shown + 6));
    m_completionPopup->show();
}

void SASQLEditor::hideCompletion()
{
    if (m_completionPopup) m_completionPopup->hide();
    m_completionTimer->stop();
}

void SASQLEditor::insertCompletion(const QString &text)
{
    int start = 0;
    QString prefix = wordBeforeCursor(&start);
    const int dot = prefix.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0) start += dot + 1;
    QString insertText = text;
    const bool isIdentifier = m_tables.contains(text) || m_columns.contains(text);
    if (isIdentifier && m_completeWithBackticks && needsBackticks(text)) insertText = QLatin1Char('`') + text + QLatin1Char('`');
    else if (!isIdentifier && m_functions.contains(text)) insertText = text + QStringLiteral("()");
    QTextCursor cursor = textCursor();
    cursor.setPosition(start);
    cursor.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    cursor.insertText(insertText);
    if (insertText.endsWith(QLatin1String("()"))) cursor.movePosition(QTextCursor::Left);
    setTextCursor(cursor);
}
