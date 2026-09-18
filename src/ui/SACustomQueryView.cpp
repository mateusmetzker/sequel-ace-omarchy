//
//  SACustomQueryView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SACustomQueryView.h"
#include "SAOmarchyTheme.h"
#include "SAContentEditing.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAFieldEditorDialog.h"
#include "SAIcons.h"
#include "SAPreferences.h"
#include "SAQueryHistory.h"
#include "SAResultModel.h"
#include "SAResultTableView.h"
#include "SASQLClassifier.h"
#include "SASQLEditor.h"
#include <QHeaderView>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <climits>

namespace {
const QString FavoritesKey = QStringLiteral("QueryFavorites");

QJsonArray loadFavorites()
{
    return QJsonDocument::fromJson(SAPreferences::instance().value(FavoritesKey).toByteArray()).array();
}
void storeFavorites(const QJsonArray &array)
{
    SAPreferences::instance().set(FavoritesKey, QJsonDocument(array).toJson(QJsonDocument::Compact));
}
QString formatInterval(double seconds)
{
    if (seconds < 0.001) return QStringLiteral("%1 µs").arg(qRound(seconds * 1e6));
    if (seconds < 1.0) return QStringLiteral("%1 ms").arg(QString::number(seconds * 1000.0, 'f', 1));
    if (seconds < 60.0) return QStringLiteral("%1 s").arg(QString::number(seconds, 'f', 2));
    return QStringLiteral("%1 min").arg(QString::number(seconds / 60.0, 'f', 1));
}
}

SACustomQueryView::SACustomQueryView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QWidget;
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(6, 4, 6, 4);
    m_runCurrentButton = new QToolButton;
    m_runCurrentButton->setIcon(SAIcons::icon(SAIcons::Glyph::Run));
    m_runCurrentButton->setText(tr("Run Current"));
    m_runCurrentButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_runCurrentButton->setToolTip(tr("Run the statement at the cursor or the selection (Ctrl+Return, Ctrl+R)"));
    connect(m_runCurrentButton, &QToolButton::clicked, this, &SACustomQueryView::runCurrent);
    m_runAllButton = new QToolButton;
    m_runAllButton->setIcon(SAIcons::icon(SAIcons::Glyph::Run));
    m_runAllButton->setText(tr("Run All"));
    m_runAllButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_runAllButton->setToolTip(tr("Run all statements (Ctrl+Shift+Return)"));
    connect(m_runAllButton, &QToolButton::clicked, this, &SACustomQueryView::runAll);
    m_stopButton = new QToolButton;
    m_stopButton->setIcon(SAIcons::icon(SAIcons::Glyph::Stop));
    m_stopButton->setToolTip(tr("Stop the running query"));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QToolButton::clicked, this, &SACustomQueryView::stop);
    for (QToolButton *b : {m_runCurrentButton, m_runAllButton, m_stopButton}) { b->setAutoRaise(true); tb->addWidget(b); }
    tb->addSpacing(12);
    m_historyBox = new QComboBox;
    m_historyBox->setMinimumWidth(240);
    m_historyBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_historyBox->setToolTip(tr("Query history: choose an entry to insert it into the editor"));
    connect(m_historyBox, &QComboBox::activated, this, [this](int index) {
        const QString query = m_historyBox->itemData(index).toString();
        if (query.isEmpty()) return;
        QTextCursor cursor = m_editor->textCursor();
        if (!m_editor->toPlainText().isEmpty() && !cursor.atBlockStart()) cursor.insertText(QStringLiteral("\n"));
        cursor.insertText(query + (query.trimmed().endsWith(QLatin1Char(';')) ? QString() : QStringLiteral(";")) + QStringLiteral("\n"));
        m_editor->setTextCursor(cursor);
        m_editor->setFocus();
        m_historyBox->setCurrentIndex(0);
    });
    tb->addWidget(m_historyBox, 1);
    m_favoritesButton = new QToolButton;
    m_favoritesButton->setText(tr("Favorites"));
    m_favoritesButton->setPopupMode(QToolButton::InstantPopup);
    m_favoritesButton->setAutoRaise(true);
    m_favoritesButton->setMenu(new QMenu(m_favoritesButton));
    tb->addWidget(m_favoritesButton);
    auto *gear = new QToolButton;
    gear->setIcon(SAIcons::icon(SAIcons::Glyph::Gear));
    gear->setPopupMode(QToolButton::InstantPopup);
    gear->setAutoRaise(true);
    auto *gearMenu = new QMenu(gear);
    gearMenu->addAction(tr("Comment / Uncomment Selection"), QKeySequence(Qt::CTRL | Qt::Key_Slash), this, [this]() { m_editor->toggleCommentOnSelection(); });
    gearMenu->addAction(tr("Uppercase Keywords"), this, [this]() { m_editor->uppercaseKeywords(); });
    gearMenu->addAction(tr("Select Current Query"), this, [this]() { const SAStatementRange r = m_editor->currentStatementRange(); if (!r.isEmpty()) m_editor->selectStatement(r); });
    // Owned by the view, not by the menu: Qt::WidgetWithChildrenShortcut only
    // fires for an action whose parent chain contains the focused editor, and
    // the window-wide default would be ambiguous between connection tabs.
    m_explainAction = new QAction(tr("Explain Current Query"), this);
    m_explainAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E));
    m_explainAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_explainAction->setToolTip(tr("Run EXPLAIN on the statement at the cursor (Ctrl+Alt+E)"));
    connect(m_explainAction, &QAction::triggered, this, &SACustomQueryView::explainCurrent);
    addAction(m_explainAction);
    gearMenu->addAction(m_explainAction);
    connect(gearMenu, &QMenu::aboutToShow, this, &SACustomQueryView::updateRunButtons);
    gearMenu->addSeparator();
    gearMenu->addAction(tr("Open SQL File…"), this, &SACustomQueryView::openQueryFile);
    gearMenu->addAction(tr("Save Query As…"), this, &SACustomQueryView::saveQueryFile);
    gearMenu->addAction(tr("Export Result…"), this, [this]() { m_document->exportCurrentResult(); });
    gearMenu->addSeparator();
    gearMenu->addAction(tr("Clear Query History"), this, []() { SAQueryHistory::instance().clear(); });
    gearMenu->addAction(tr("Clear Editor"), this, [this]() { m_editor->clear(); });
    gear->setMenu(gearMenu);
    tb->addWidget(gear);
    layout->addWidget(toolbar);

    // Editor / results
    m_splitter = new QSplitter(Qt::Vertical);
    m_editor = new SASQLEditor;
    m_editor->setPlaceholderText(tr("Type SQL statements here. Ctrl+Return runs the statement at the cursor, Ctrl+Shift+Return runs everything. Ctrl+Space completes names."));
    m_editor->setColumnsLoader([this](const QString &table, std::function<void(const QStringList &)> done) {
        m_document->session()->query(SASchema::showFullColumns(table), [done](const SAResult &r) {
            QStringList names;
            for (int i = 0; i < r.rowCount(); ++i) names << r.stringAt(i, QStringLiteral("Field"));
            done(names);
        }, SADatabaseSession::Silent);
    });
    m_splitter->addWidget(m_editor);

    auto *results = new QWidget;
    auto *rl = new QVBoxLayout(results);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    m_model = new SAResultModel(this);
    m_model->setNullString(SAPreferences::instance().stringFor(SAPreferences::NullValue));
    m_model->setBinaryAsHex(SAPreferences::instance().boolFor(SAPreferences::DisplayBinaryDataAsHex));
    m_table = new SAResultTableView;
    m_table->setResultModel(m_model);
    m_table->setEditingEnabled(false);
    m_table->setShowColumnTypes(SAPreferences::instance().boolFor(SAPreferences::DisplayTableViewColumnTypes));
    connect(m_table, &SAResultTableView::editInSheetRequested, this, &SACustomQueryView::openFieldViewer);
    connect(m_table, &SAResultTableView::copyAsInsertRequested, this, &SACustomQueryView::copyAsSQLInsert);
    connect(m_table, &SAResultTableView::reloadRequested, this, &SACustomQueryView::runAll);
    connect(m_table, &SAResultTableView::exportRequested, this, [this]() { m_document->exportCurrentResult(); });
    // The whole result set is already in memory here (unlike the paginated
    // Content view, which re-queries the server instead), so sort it locally.
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        const Qt::SortOrder order = (m_model->sortColumn() == column && m_model->sortOrder() == Qt::AscendingOrder)
            ? Qt::DescendingOrder : Qt::AscendingOrder;
        m_model->sort(column, order);
    });
    rl->addWidget(m_table, 1);
    m_errorText = new QPlainTextEdit;
    m_errorText->setReadOnly(true);
    m_errorText->setMaximumHeight(90);
    m_errorText->setVisible(false);
    m_errorText->setStyleSheet(QStringLiteral("QPlainTextEdit { color: %1; }").arg(SAOmarchyTheme::current().red.name()));
    connect(&SAPreferences::instance(), &SAPreferences::changed, m_errorText, [this](const QString &) {
        m_errorText->setStyleSheet(QStringLiteral("QPlainTextEdit { color: %1; }").arg(SAOmarchyTheme::current().red.name()));
    });
    rl->addWidget(m_errorText);
    m_statusLabel = new QLabel(tr("No query executed yet."));
    m_statusLabel->setContentsMargins(6, 3, 6, 3);
    m_statusLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    rl->addWidget(m_statusLabel);
    m_splitter->addWidget(results);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    layout->addWidget(m_splitter, 1);

    // Shortcuts local to the view.
    auto *runCurrentAction = new QAction(this);
    runCurrentAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter), QKeySequence(Qt::CTRL | Qt::Key_R)});
    runCurrentAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(runCurrentAction, &QAction::triggered, this, &SACustomQueryView::runCurrent);
    addAction(runCurrentAction);
    auto *runAllAction = new QAction(this);
    runAllAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Enter)});
    runAllAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(runAllAction, &QAction::triggered, this, &SACustomQueryView::runAll);
    addAction(runAllAction);
    auto *stopAction = new QAction(this);
    stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    stopAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(stopAction, &QAction::triggered, this, &SACustomQueryView::stop);
    addAction(stopAction);

    connect(&SAQueryHistory::instance(), &SAQueryHistory::changed, this, &SACustomQueryView::updateHistoryBox);
    connect(&SAPreferences::instance(), &SAPreferences::changed, this, [this](const QString &key) {
        if (key == SAPreferences::DisplayBinaryDataAsHex) m_model->setBinaryAsHex(SAPreferences::instance().boolFor(key));
        else if (key == SAPreferences::NullValue) m_model->setNullString(SAPreferences::instance().stringFor(key));
        else if (key == SAPreferences::DisplayTableViewColumnTypes) m_table->setShowColumnTypes(SAPreferences::instance().boolFor(key));
        else if (key == FavoritesKey) updateFavoritesMenu();
    });
    updateHistoryBox();
    updateFavoritesMenu();
}

void SACustomQueryView::setQueryText(const QString &text) { m_editor->setPlainText(text); }
QString SACustomQueryView::statusText() const { return m_statusLabel->text(); }
QString SACustomQueryView::errorText() const { return m_errorText->isVisible() || !m_errorText->toPlainText().isEmpty() ? m_errorText->toPlainText() : QString(); }
QString SACustomQueryView::queryText() const { return m_editor->toPlainText(); }
void SACustomQueryView::focusEditor() { m_editor->setFocus(); }
void SACustomQueryView::updateCompletion(const QStringList &tables) { m_editor->setCompletionTables(tables); }

void SACustomQueryView::updateHistoryBox()
{
    m_historyBox->clear();
    m_historyBox->addItem(tr("Query History"), QString());
    for (const QString &item : SAQueryHistory::instance().items()) {
        QString label = item.simplified();
        if (label.size() > 80) label = label.left(80) + QStringLiteral("…");
        m_historyBox->addItem(label, item);
        m_historyBox->setItemData(m_historyBox->count() - 1, item, Qt::ToolTipRole);
    }
}

void SACustomQueryView::updateFavoritesMenu()
{
    QMenu *menu = m_favoritesButton->menu();
    menu->clear();
    menu->addAction(tr("Save Current Query as Favorite…"), this, &SACustomQueryView::saveAsFavorite);
    menu->addAction(tr("Manage Favorites…"), this, &SACustomQueryView::manageFavorites);
    const QJsonArray favorites = loadFavorites();
    if (!favorites.isEmpty()) menu->addSeparator();
    for (const QJsonValue &v : favorites) {
        const QJsonObject o = v.toObject();
        const QString query = o.value(QStringLiteral("query")).toString();
        QAction *a = menu->addAction(o.value(QStringLiteral("name")).toString());
        a->setToolTip(query);
        connect(a, &QAction::triggered, this, [this, query]() {
            QTextCursor cursor = m_editor->textCursor();
            if (!m_editor->toPlainText().isEmpty() && !cursor.atBlockStart()) cursor.insertText(QStringLiteral("\n"));
            cursor.insertText(query);
            m_editor->setTextCursor(cursor);
            m_editor->setFocus();
        });
    }
}

void SACustomQueryView::saveAsFavorite()
{
    QString query = m_editor->selectedText();
    if (query.isEmpty()) query = m_editor->currentStatement();
    if (query.isEmpty()) query = m_editor->toPlainText();
    if (query.trimmed().isEmpty()) return;
    bool ok = false;
    const QString name = SADialogs::askText(this, tr("Save Query Favorite"), tr("Name:"), query.simplified().left(40), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QJsonArray favorites = loadFavorites();
    QJsonObject o;
    o.insert(QStringLiteral("name"), name.trimmed());
    o.insert(QStringLiteral("query"), query);
    favorites.append(o);
    storeFavorites(favorites);
    updateFavoritesMenu();
}

void SACustomQueryView::manageFavorites()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Query Favorites"));
    auto *list = new QListWidget;
    QJsonArray favorites = loadFavorites();
    for (const QJsonValue &v : favorites) {
        auto *item = new QListWidgetItem(v.toObject().value(QStringLiteral("name")).toString());
        item->setToolTip(v.toObject().value(QStringLiteral("query")).toString());
        list->addItem(item);
    }
    auto *remove = new QPushButton(tr("Delete"));
    connect(remove, &QPushButton::clicked, &dialog, [&]() {
        const int row = list->currentRow();
        if (row < 0) return;
        favorites.removeAt(row);
        delete list->takeItem(row);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(list);
    auto *row = new QHBoxLayout;
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(buttons);
    layout->addLayout(row);
    dialog.resize(420, 360);
    dialog.exec();
    storeFavorites(favorites);
    updateFavoritesMenu();
}

// ---- running ----------------------------------------------------------------------------

void SACustomQueryView::updateRunButtons()
{
    m_runCurrentButton->setEnabled(!m_running);
    m_runAllButton->setEnabled(!m_running);
    m_stopButton->setEnabled(m_running);
    // Cheap validation only, like the macOS menu item: classifying the buffer
    // on every keystroke would reparse it character by character for nothing,
    // and explainCurrent() reports why it refused anyway.
    if (m_explainAction) m_explainAction->setEnabled(!m_running && !m_editor->document()->isEmpty());
}

void SACustomQueryView::runAll()
{
    if (m_running) return;
    const QString text = m_editor->toPlainText();
    const QVector<SAStatementRange> ranges = m_editor->statementRanges();
    QStringList statements;
    QVector<SAStatementRange> used;
    for (const SAStatementRange &r : ranges) {
        const QString s = text.mid(r.start, r.length).trimmed();
        if (!s.isEmpty()) { statements << s; used << SASQLSplitter::trimmedRange(text, r); }
    }
    runStatements(statements, used);
}

void SACustomQueryView::runCurrent()
{
    if (m_running) return;
    const QString selection = m_editor->selectedText();
    if (!selection.trimmed().isEmpty()) {
        const QStringList statements = SASQLSplitter().split(selection);
        runStatements(statements, {});
        return;
    }
    const SAStatementRange range = m_editor->currentStatementRange();
    if (range.isEmpty()) {
        m_statusLabel->setText(tr("No query at the cursor position."));
        return;
    }
    runStatements({m_editor->toPlainText().mid(range.start, range.length)}, {range});
}

void SACustomQueryView::stop()
{
    if (!m_running) return;
    m_run.cancelled = true;
    m_document->session()->cancelCurrentQuery();
}

void SACustomQueryView::explainCurrent()
{
    if (m_running) return;

    QString query;
    const QString selection = m_editor->selectedText();
    if (selection.trimmed().isEmpty()) {
        const SAStatementRange range = m_editor->currentStatementRange();
        if (range.isEmpty()) {
            m_statusLabel->setText(tr("No query at the cursor position."));
            return;
        }
        query = SASQLSplitter::normaliseForExecution(m_editor->toPlainText().mid(range.start, range.length));
    } else {
        // EXPLAIN takes one statement. The split is delimiter-aware and already
        // drops comment-only fragments, so "SELECT 1; -- foo" counts as one.
        const QStringList statements = SASQLSplitter().split(selection);
        if (statements.size() != 1) { reportUnsupportedExplain(); return; }
        query = SASQLSplitter::normaliseForExecution(statements.first());
    }

    if (!SASQLClassifier::isQueryExplainable(query)) { reportUnsupportedExplain(); return; }

    // The original text is what goes to the server; the stripped form was only
    // used to classify it. Sending the stripped SQL would change the meaning of
    // an executable comment and corrupt literals holding a CR.
    runStatements({QStringLiteral("EXPLAIN ") + query}, {});
}

void SACustomQueryView::reportUnsupportedExplain()
{
    const QString message = tr("EXPLAIN is only supported for a single SELECT or WITH statement.");
    m_statusLabel->setText(message);
    // The error box starts hidden, so showing it is what keeps this message
    // from disappearing the way the macOS one does in a collapsed pane.
    m_errorText->setPlainText(message);
    m_errorText->setVisible(true);
}

void SACustomQueryView::runStatements(const QStringList &statements, const QVector<SAStatementRange> &ranges)
{
    if (statements.isEmpty()) {
        m_statusLabel->setText(tr("Nothing to run."));
        return;
    }
    if (SAPreferences::instance().boolFor(SAPreferences::ShowWarningBeforeExecuteQuery)) {
        // Allow list, like -[SPCustomQuery queriesContainDestructiveSQL:]: only
        // SHOW/SELECT and the non-executing EXPLAIN aliases skip the prompt. A
        // keyword deny list missed writes hidden behind a comment, EXPLAIN
        // aliases, and everything that is neither read nor DML.
        if (SASQLClassifier::batchNeedsDestructiveWarning(statements)) {
            const QString preview = statements.join(QStringLiteral("\n"));
            if (!SADialogs::confirm(this, tr("Execute SQL?"), tr("Do you really want to proceed with %n statement(s)?\n\n%1", nullptr, statements.size()).arg(preview.size() > 800 ? preview.left(800) + QStringLiteral("…") : preview), tr("Proceed"))) return;
        }
    }
    m_run = RunState();
    m_run.statements = statements;
    m_run.ranges = ranges;
    m_running = true;
    updateRunButtons();
    m_errorText->clear();
    m_errorText->setVisible(false);
    m_statusLabel->setText(statements.size() > 1 ? tr("Running query 1 of %1…").arg(statements.size()) : tr("Running query…"));
    runNext();
}

void SACustomQueryView::runNext()
{
    if (m_run.index >= m_run.statements.size() || m_run.cancelled) {
        finishRun();
        return;
    }
    const int index = m_run.index;
    const QString sql = m_run.statements.at(index);
    if (m_run.statements.size() > 1) m_statusLabel->setText(tr("Running query %1 of %2…").arg(index + 1).arg(m_run.statements.size()));
    m_document->session()->query(sql, [this, index, sql](const SAResult &r) {
        m_run.executed++;
        m_run.time += r.executionTime;
        if (r.ok) {
            if (r.hasResultSet) {
                m_run.lastResultSet = r;
                m_run.haveResultSet = true;
                m_run.affected += quint64(r.rowCount());
            } else if (r.affectedRows != ~0ULL) {
                m_run.affected += r.affectedRows;
            }
            static const QRegularExpression schema(QStringLiteral("^\\s*(create|alter|drop|rename)\\b"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression database(QStringLiteral("^\\s*(use|drop\\s+database|drop\\s+schema)\\b"), QRegularExpression::CaseInsensitiveOption);
            if (schema.match(sql).hasMatch()) m_run.schemaChanged = true;
            if (database.match(sql).hasMatch()) m_run.databaseChanged = true;
        } else {
            QString error = r.wasCancelled ? tr("Query cancelled.") : r.errorMessage;
            if (m_run.firstErrorIndex < 0) m_run.firstErrorIndex = index;
            if (m_run.statements.size() > 1) {
                m_run.errors << tr("[ERROR in query %1] %2").arg(index + 1).arg(error);
                if (!r.wasCancelled && !m_run.suppressPrompts && index < m_run.statements.size() - 1) {
                    const int choice = SADialogs::errorContinuation(this, tr("MySQL Error"), error);
                    if (choice == 2) m_run.suppressPrompts = true;
                    else if (choice == 0) {
                        m_run.errors << tr("Execution stopped!");
                        m_run.index = m_run.statements.size();
                        finishRun();
                        return;
                    }
                }
            } else {
                m_run.errors << error;
            }
            if (r.wasCancelled) m_run.cancelled = true;
        }
        m_run.index++;
        runNext();
    }, SADatabaseSession::UserQuery);
}

QString SACustomQueryView::statusStringFor(const RunState &state) const
{
    const QString errorState = state.errors.isEmpty() ? tr("No errors") : tr("Errors");
    if (state.cancelled) {
        if (state.executed > 1) return tr("%1; cancelled in query %2, after %3").arg(errorState).arg(state.executed).arg(formatInterval(state.time));
        return tr("%1; cancelled after %2").arg(errorState, formatInterval(state.time));
    }
    if (state.executed > 1) {
        return tr("%1; %n row(s) affected in total, by %2 queries taking %3", nullptr, int(qMin<quint64>(state.affected, INT_MAX))).arg(errorState).arg(state.executed).arg(formatInterval(state.time));
    }
    QString status = tr("%1; %n row(s) affected", nullptr, int(qMin<quint64>(state.affected, INT_MAX))).arg(errorState);
    status += state.haveResultSet ? tr(", first row available after %1").arg(formatInterval(state.time)) : tr(", taking %1").arg(formatInterval(state.time));
    return status;
}

void SACustomQueryView::finishRun()
{
    m_running = false;
    updateRunButtons();
    if (m_run.haveResultSet) {
        m_model->setResult(m_run.lastResultSet);
        m_table->autosizeColumns();
        const QVector<SAField> &fields = m_run.lastResultSet.fields;
        m_lastResultTable = fields.isEmpty() ? QString() : fields.first().orgTable;
        for (const SAField &f : fields) if (f.orgTable != m_lastResultTable) { m_lastResultTable.clear(); break; }
        m_usedQuery = m_run.statements.join(QStringLiteral(";\n"));
    } else {
        m_model->setResult(SAResult());
        m_lastResultTable.clear();
        m_usedQuery.clear();
    }
    m_statusLabel->setText(statusStringFor(m_run));
    if (!m_run.errors.isEmpty()) {
        m_errorText->setPlainText(m_run.errors.join(QLatin1Char('\n')));
        m_errorText->setVisible(true);
        if (m_run.firstErrorIndex >= 0 && m_run.firstErrorIndex < m_run.ranges.size()) m_editor->selectStatement(m_run.ranges.at(m_run.firstErrorIndex));
    }
    if (!m_run.statements.isEmpty()) SAQueryHistory::instance().add(m_run.statements.join(QStringLiteral(";\n")));
    if (m_run.databaseChanged) {
        m_document->session()->refreshCurrentDatabase([this](bool, const QString &) { m_document->refreshDatabases(); });
    } else if (m_run.schemaChanged) {
        m_document->refreshTables();
    }
}

// ---- results helpers ----------------------------------------------------------------------

void SACustomQueryView::openFieldViewer(const QModelIndex &index)
{
    if (!index.isValid()) return;
    const SAField &field = m_model->result().fields.at(index.column());
    SAFieldEditorDialog dialog(this, field.name, field.typeGroup, field.typeName, m_model->cellAt(index.row(), index.column()), false, true);
    dialog.exec();
}

void SACustomQueryView::copyWithColumnNames() { m_table->copySelection(true); }

void SACustomQueryView::copyAsSQLInsert(bool skipAutoIncrement)
{
    const QList<int> rows = m_table->selectedRows();
    if (rows.isEmpty()) return;
    QVector<SARow> selected;
    for (int r : rows) selected << m_model->rowCells(r);
    SAContentEditing::Escaper esc;
    esc.string = [](const QString &s) { return SADatabaseSession::escapeString(s, true); };
    esc.data = [](const QByteArray &d) { return SAMySQLConnection().escapeAndQuoteData(d); };
    const QString table = m_lastResultTable.isEmpty() ? tr("<table>") : m_lastResultTable;
    const QString sql = SAContentEditing::insertStatementForRows(table, m_model->result().fields, selected, skipAutoIncrement, esc);
    if (!sql.isEmpty()) QApplication::clipboard()->setText(sql);
}

void SACustomQueryView::saveQueryFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Query"), QDir::homePath() + QStringLiteral("/query.sql"), tr("SQL files (*.sql)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { m_document->reportError(tr("Save failed"), file.errorString()); return; }
    file.write(m_editor->toPlainText().toUtf8());
}

void SACustomQueryView::openQueryFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open SQL File"), QDir::homePath(), tr("SQL files (*.sql);;All files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { m_document->reportError(tr("Open failed"), file.errorString()); return; }
    m_editor->setPlainText(QString::fromUtf8(file.readAll()));
}
