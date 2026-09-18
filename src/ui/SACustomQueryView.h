//
//  SACustomQueryView.h
//  Sequel Ace (Linux port)
//
//  The Query view: SQL editor, run controls, history and favorites, result
//  grid and status/error area. Executes statements sequentially with the same
//  error-continuation prompt as SPCustomQuery.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"
#include "SASQLSplitter.h"

#include <QWidget>

class SADatabaseDocument;
class SASQLEditor;
class SAResultModel;
class SAResultTableView;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QToolButton;
class QAction;
class QSplitter;

class SACustomQueryView : public QWidget {
    Q_OBJECT
public:
    explicit SACustomQueryView(SADatabaseDocument *document, QWidget *parent = nullptr);

    void setQueryText(const QString &text);
    QString queryText() const;
    void focusEditor();
    void updateCompletion(const QStringList &tables);
    SASQLEditor *editor() const { return m_editor; }
    SAResultModel *model() const { return m_model; }
    QString statusText() const;
    QString errorText() const;
    bool isRunning() const { return m_running; }

    // For the export dialog: the table every field of the last result came
    // from, empty when the fields disagree or have none (a join, an
    // expression), and the statements that produced it.
    QString resultTableName() const { return m_lastResultTable; }
    QString usedQuery() const { return m_usedQuery; }

public Q_SLOTS:
    void runAll();
    void runCurrent();
    void explainCurrent();
    void stop();
    void copyWithColumnNames();
    void copyAsSQLInsert(bool skipAutoIncrement = false);
    void saveQueryFile();
    void openQueryFile();

private:
    struct RunState {
        QStringList statements;
        QVector<SAStatementRange> ranges;
        int index = 0;
        int executed = 0;
        quint64 affected = 0;
        double time = 0.0;
        QStringList errors;
        int firstErrorIndex = -1;
        bool suppressPrompts = false;
        bool cancelled = false;
        bool schemaChanged = false;
        bool databaseChanged = false;
        SAResult lastResultSet;
        bool haveResultSet = false;
    };

    void runStatements(const QStringList &statements, const QVector<SAStatementRange> &ranges);
    void runNext();
    void finishRun();
    void updateHistoryBox();
    void updateFavoritesMenu();
    void saveAsFavorite();
    void manageFavorites();
    void openFieldViewer(const QModelIndex &index);
    void updateRunButtons();
    QString statusStringFor(const RunState &state) const;
    void reportUnsupportedExplain();

    SADatabaseDocument *m_document;
    SASQLEditor *m_editor;
    QToolButton *m_runCurrentButton;
    QToolButton *m_runAllButton;
    QToolButton *m_stopButton;
    QAction *m_explainAction = nullptr;
    QComboBox *m_historyBox;
    QToolButton *m_favoritesButton;
    SAResultModel *m_model;
    SAResultTableView *m_table;
    QLabel *m_statusLabel;
    QPlainTextEdit *m_errorText;
    QSplitter *m_splitter;
    RunState m_run;
    bool m_running = false;
    QString m_lastResultTable;
    QString m_usedQuery;
};
