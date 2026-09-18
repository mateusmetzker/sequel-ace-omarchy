//
//  SAExportDialog.h
//  Sequel Ace (Linux port)
//
//  Export selected tables as CSV files or as an SQL dump (structure and/or
//  data). Data is fetched in batches through the session so the UI stays
//  responsive; the dialog can be cancelled at any point.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include "SAResult.h"
#include "SAResultExport.h"
#include "SASchemaQueries.h"

#include <QDialog>
#include <QFile>
#include <QTextStream>

class SADatabaseDocument;
class QListWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QProgressBar;
class QLabel;
class QStackedWidget;
class QPushButton;

class SAExportDialog : public QDialog {
    Q_OBJECT
public:
    // Where the rows come from. Same order as SPExportSource on macOS.
    enum Source { FilteredContent = 0, QueryResult = 1, SelectedTables = 2 };

    SAExportDialog(SADatabaseDocument *document, const QStringList &preselected,
                   Source initial, QWidget *parent = nullptr);
    SAExportDialog(SADatabaseDocument *document, const QStringList &preselected, QWidget *parent = nullptr);

    // Test seams: drive the dialog without the file chooser.
    void setOutputPath(const QString &path);
    void startExport();
    bool isRunning() const { return m_running; }
    QString statusMessage() const;

private:
    enum Format { CSV = 0, SQL = 1 };

    void buildUI(const QStringList &preselected, Source initial);
    void updateOptionsForSource();
    void chooseOutput();
    void startTableExport();
    void startResultExport(const SAResult &result, const QString &table);
    void exportNextTable();
    void fetchBatch();
    void writeRows(const QVector<SAField> &fields, const QVector<SARow> &rows, const QString &table);
    void finish(const QString &message, bool failed);
    SAResultExport::CsvOptions csvOptions() const;
    QStringList selectedTables() const;
    Source currentSource() const;
    bool writesOneFilePerTable() const;
    QString sqlDumpHeader() const;
    // Deterministic ORDER BY for the batched content export, or empty when the
    // table offers nothing to order by.
    QString orderByForContent() const;
    // Fields with duplicate original column names replaced by their aliases, so
    // a join never produces an INSERT with the same column twice.
    QVector<SAField> fieldsForInsert(const QVector<SAField> &fields) const;

    SADatabaseDocument *m_document;
    QListWidget *m_tables;
    QComboBox *m_source;
    QComboBox *m_format;
    QStackedWidget *m_options;
    QLineEdit *m_output;
    QCheckBox *m_csvHeader;
    QLineEdit *m_csvSeparator;
    QLineEdit *m_csvEnclosure;
    QLineEdit *m_csvNull;
    QCheckBox *m_sqlStructure;
    QCheckBox *m_sqlContent;
    QCheckBox *m_sqlDrop;
    QLabel *m_targetTableLabel;
    QLineEdit *m_targetTable;
    QCheckBox *m_currentPageOnly;
    QLabel *m_tablesLabel;
    QPushButton *m_selectAll;
    QPushButton *m_selectNone;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_exportButton;
    QPushButton *m_cancelButton;

    // run state
    Source m_activeSource = SelectedTables;
    bool m_running = false;
    bool m_cancelled = false;
    QStringList m_queue;
    int m_tableIndex = 0;
    QString m_currentTable;
    QVector<SASchema::Column> m_currentColumns;
    QString m_where;      // WHERE clause without the keyword, empty for none
    QString m_orderBy;    // ORDER BY list without the keyword; empty means the
                          // server's order, which OFFSET paging cannot rely on
    quint64 m_offset = 0;
    quint64 m_rowsWritten = 0;
    QFile m_file;
    QTextStream m_stream;
    bool m_perTableFiles = false;
};
