//
//  SAExportDialog.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAExportDialog.h"
#include "SAContentEditing.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAResultExport.h"
#include "SACustomQueryView.h"
#include "SAResultModel.h"
#include "SATableContentView.h"
#include "SAIcons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QStandardItemModel>

namespace {
constexpr quint64 BatchSize = 2000;
}

SAExportDialog::SAExportDialog(SADatabaseDocument *document, const QStringList &preselected, QWidget *parent)
    : SAExportDialog(document, preselected, SelectedTables, parent)
{
}

SAExportDialog::SAExportDialog(SADatabaseDocument *document, const QStringList &preselected,
                               Source initial, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    buildUI(preselected, initial);
}

void SAExportDialog::buildUI(const QStringList &preselected, Source initial)
{
    SADatabaseDocument *document = m_document;
    setWindowTitle(tr("Export"));
    auto *layout = new QHBoxLayout(this);

    // Tables
    auto *left = new QVBoxLayout;
    m_tablesLabel = new QLabel(tr("Tables to export:"));
    left->addWidget(m_tablesLabel);
    m_tables = new QListWidget;
    for (const SASchema::ObjectEntry &e : document->tableEntries()) {
        if (e.type != SASchema::ObjectType::Table && e.type != SASchema::ObjectType::View) continue;
        auto *item = new QListWidgetItem(e.name, m_tables);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(preselected.contains(e.name) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, int(e.type));
    }
    left->addWidget(m_tables, 1);
    auto *selectRow = new QHBoxLayout;
    m_selectAll = new QPushButton(SAIcons::icon(SAIcons::Glyph::SelectAll), tr("Select All"));
    connect(m_selectAll, &QPushButton::clicked, this, [this]() { for (int i = 0; i < m_tables->count(); ++i) m_tables->item(i)->setCheckState(Qt::Checked); });
    m_selectNone = new QPushButton(SAIcons::icon(SAIcons::Glyph::SelectNone), tr("Select None"));
    connect(m_selectNone, &QPushButton::clicked, this, [this]() { for (int i = 0; i < m_tables->count(); ++i) m_tables->item(i)->setCheckState(Qt::Unchecked); });
    selectRow->addWidget(m_selectAll);
    selectRow->addWidget(m_selectNone);
    selectRow->addStretch();
    left->addLayout(selectRow);
    layout->addLayout(left, 1);

    // Options
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    m_source = new QComboBox;
    m_source->addItem(tr("Filtered table content"), FilteredContent);
    m_source->addItem(tr("Current query result"), QueryResult);
    m_source->addItem(tr("Selected tables"), SelectedTables);
    // A source with no rows behind it is offered but not selectable, like the
    // disabled entries of macOS' export input popup.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_source->model())) {
        const SATableContentView *content = document->contentView();
        const SACustomQueryView *query = document->queryView();
        const bool haveContent = content && !content->tableName().isEmpty() && content->model()->rowCount() > 0;
        const bool haveResult = query && query->model()->rowCount() > 0;
        model->item(FilteredContent)->setEnabled(haveContent);
        model->item(QueryResult)->setEnabled(haveResult);
        if ((initial == FilteredContent && !haveContent) || (initial == QueryResult && !haveResult))
            initial = SelectedTables;
    }
    m_source->setCurrentIndex(initial);
    form->addRow(tr("Source:"), m_source);

    m_format = new QComboBox;
    m_format->addItem(tr("CSV (one file per table)"), CSV);
    m_format->addItem(tr("SQL dump"), SQL);
    form->addRow(tr("Format:"), m_format);
    m_options = new QStackedWidget;
    auto *csvPage = new QWidget;
    auto *csvForm = new QFormLayout(csvPage);
    csvForm->setContentsMargins(0, 0, 0, 0);
    m_csvHeader = new QCheckBox(tr("Include field names as the first row"));
    m_csvHeader->setChecked(true);
    m_csvSeparator = new QLineEdit(QStringLiteral(","));
    m_csvEnclosure = new QLineEdit(QStringLiteral("\""));
    m_csvNull = new QLineEdit(QStringLiteral("NULL"));
    csvForm->addRow(QString(), m_csvHeader);
    csvForm->addRow(tr("Fields terminated by:"), m_csvSeparator);
    csvForm->addRow(tr("Fields enclosed by:"), m_csvEnclosure);
    csvForm->addRow(tr("Write NULL as:"), m_csvNull);
    m_options->addWidget(csvPage);
    auto *sqlPage = new QWidget;
    auto *sqlForm = new QVBoxLayout(sqlPage);
    sqlForm->setContentsMargins(0, 0, 0, 0);
    m_sqlStructure = new QCheckBox(tr("Structure (CREATE statements)"));
    m_sqlStructure->setChecked(true);
    m_sqlContent = new QCheckBox(tr("Content (INSERT statements)"));
    m_sqlContent->setChecked(true);
    m_sqlDrop = new QCheckBox(tr("Add DROP TABLE IF EXISTS before each CREATE"));
    sqlForm->addWidget(m_sqlStructure);
    sqlForm->addWidget(m_sqlContent);
    sqlForm->addWidget(m_sqlDrop);
    m_options->addWidget(sqlPage);
    connect(m_format, &QComboBox::currentIndexChanged, m_options, &QStackedWidget::setCurrentIndex);
    form->addRow(QString(), m_options);

    m_targetTable = new QLineEdit;
    m_targetTable->setPlaceholderText(tr("Table name for the INSERT statements"));
    m_targetTableLabel = new QLabel(tr("Target table:"));
    form->addRow(m_targetTableLabel, m_targetTable);

    m_currentPageOnly = new QCheckBox(tr("Only the rows currently loaded"));
    m_currentPageOnly->setToolTip(tr("Export the page shown in the Content view instead of every row matching the filter."));
    form->addRow(QString(), m_currentPageOnly);
    auto *outRow = new QHBoxLayout;
    m_output = new QLineEdit;
    m_output->setPlaceholderText(tr("Choose an output file or folder"));
    auto *browse = new QPushButton(tr("Choose…"));
    connect(browse, &QPushButton::clicked, this, &SAExportDialog::chooseOutput);
    outRow->addWidget(m_output, 1);
    outRow->addWidget(browse);
    form->addRow(tr("Output:"), outRow);
    right->addLayout(form);
    right->addStretch();
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    right->addWidget(m_progress);
    right->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_cancelButton = new QPushButton(tr("Cancel"));
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { if (m_running) m_cancelled = true; else reject(); });
    m_exportButton = new QPushButton(tr("Export"));
    m_exportButton->setDefault(true);
    connect(m_exportButton, &QPushButton::clicked, this, &SAExportDialog::startExport);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_exportButton);
    right->addLayout(buttons);
    layout->addLayout(right, 2);
    resize(760, 460);

    connect(m_source, &QComboBox::currentIndexChanged, this, &SAExportDialog::updateOptionsForSource);
    connect(m_format, &QComboBox::currentIndexChanged, this, &SAExportDialog::updateOptionsForSource);
    updateOptionsForSource();
}

SAExportDialog::Source SAExportDialog::currentSource() const
{
    return Source(m_source->currentData().toInt());
}

bool SAExportDialog::writesOneFilePerTable() const
{
    return currentSource() == SelectedTables && m_format->currentData().toInt() == CSV;
}

QString SAExportDialog::statusMessage() const
{
    return m_status->text();
}

void SAExportDialog::setOutputPath(const QString &path)
{
    m_output->setText(path);
}

void SAExportDialog::updateOptionsForSource()
{
    const Source source = currentSource();
    const bool tables = source == SelectedTables;
    const bool sql = m_format->currentData().toInt() == SQL;

    m_tables->setEnabled(tables);
    m_tablesLabel->setEnabled(tables);
    m_selectAll->setEnabled(tables);
    m_selectNone->setEnabled(tables);

    m_format->setItemText(CSV, tables ? tr("CSV (one file per table)") : tr("CSV"));

    // There is no CREATE TABLE for an arbitrary result set, and a view's CREATE
    // statement would not let the dumped rows be imported back.
    bool structureAvailable = sql;
    if (source == QueryResult) {
        structureAvailable = false;
    } else if (source == FilteredContent) {
        const SATableContentView *content = m_document->contentView();
        if (content && content->objectType() == SASchema::ObjectType::View) structureAvailable = false;
    }
    m_sqlStructure->setEnabled(structureAvailable);
    if (!structureAvailable) m_sqlStructure->setChecked(false);
    m_sqlDrop->setEnabled(structureAvailable && m_sqlStructure->isChecked());

    // Without the table list there is nothing to pick and choose: the rows that
    // are there are the content of the export.
    m_sqlContent->setEnabled(tables);
    if (!tables) m_sqlContent->setChecked(true);

    const bool needsTargetTable = source == QueryResult && sql;
    m_targetTableLabel->setVisible(needsTargetTable);
    m_targetTable->setVisible(needsTargetTable);
    if (needsTargetTable && m_targetTable->text().trimmed().isEmpty()) {
        if (const SACustomQueryView *query = m_document->queryView())
            m_targetTable->setText(query->resultTableName());
    }

    m_currentPageOnly->setVisible(source == FilteredContent);
}

QStringList SAExportDialog::selectedTables() const
{
    QStringList names;
    for (int i = 0; i < m_tables->count(); ++i)
        if (m_tables->item(i)->checkState() == Qt::Checked) names << m_tables->item(i)->text();
    return names;
}

void SAExportDialog::chooseOutput()
{
    if (!writesOneFilePerTable() && m_format->currentData().toInt() == CSV) {
        const QString suggested = QDir::homePath() + QStringLiteral("/%1.csv").arg(m_document->currentDatabase());
        const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), suggested, tr("CSV files (*.csv)"));
        if (!path.isEmpty()) m_output->setText(path);
        return;
    }
    if (m_format->currentData().toInt() == SQL) {
        const QString suggested = QDir::homePath() + QStringLiteral("/%1_%2.sql").arg(m_document->currentDatabase(), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd")));
        const QString path = QFileDialog::getSaveFileName(this, tr("Export SQL"), suggested, tr("SQL files (*.sql)"));
        if (!path.isEmpty()) m_output->setText(path);
    } else {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder for the CSV files"), QDir::homePath());
        if (!dir.isEmpty()) m_output->setText(dir);
    }
}

SAResultExport::CsvOptions SAExportDialog::csvOptions() const
{
    SAResultExport::CsvOptions options;
    options.separator = m_csvSeparator->text();
    options.enclosure = m_csvEnclosure->text();
    options.nullString = m_csvNull->text();
    options.header = m_csvHeader->isChecked();
    return options;
}

QString SAExportDialog::sqlDumpHeader() const
{
    QString header = QStringLiteral("-- Sequel Ace SQL dump\n-- Host: %1  Database: %2\n-- Generated: %3\n")
                         .arg(m_document->connectionInfo().hostDescription(), m_document->currentDatabase(),
                              QDateTime::currentDateTime().toString(Qt::ISODate));
    // Record the statement the rows came from, the way the macOS XML export
    // does with its <resultset statement="..."> attribute.
    QString query;
    if (m_activeSource == QueryResult) {
        if (const SACustomQueryView *view = m_document->queryView()) query = view->usedQuery();
    } else if (m_activeSource == FilteredContent) {
        if (const SATableContentView *view = m_document->contentView()) query = view->lastUsedQuery();
    }
    if (!query.isEmpty()) header += QStringLiteral("-- Query: %1\n").arg(query.simplified());
    header += QStringLiteral("\nSET NAMES utf8mb4;\nSET FOREIGN_KEY_CHECKS = 0;\n\n");
    return header;
}

QString SAExportDialog::orderByForContent() const
{
    const SATableContentView *content = m_document->contentView();
    if (!content) return QString();
    // Paging with OFFSET needs a deterministic order. The column the user
    // sorted by comes first, then the primary key.
    const int sortColumn = content->sortColumnIndex();
    const QVector<SASchema::Column> &columns = content->columns();
    if (sortColumn >= 0 && sortColumn < columns.size()) {
        return SADatabaseSession::quoteIdentifier(columns.at(sortColumn).name)
            + (content->sortDescending() ? QStringLiteral(" DESC") : QString());
    }
    QStringList keys;
    for (const QString &key : content->primaryKeys()) keys << SADatabaseSession::quoteIdentifier(key);
    return keys.join(QStringLiteral(", "));
}

QVector<SAField> SAExportDialog::fieldsForInsert(const QVector<SAField> &fields) const
{
    // Two columns of a join can share an original name, which would emit the
    // same column twice. Fall back to the aliases for every column at once, so
    // the list never mixes the two conventions.
    QSet<QString> seen;
    bool duplicated = false;
    for (const SAField &f : fields) {
        const QString name = f.orgName.isEmpty() ? f.name : f.orgName;
        if (seen.contains(name)) { duplicated = true; break; }
        seen.insert(name);
    }
    if (!duplicated) return fields;

    QVector<SAField> renamed = fields;
    for (SAField &f : renamed) f.orgName = f.name;
    return renamed;
}

void SAExportDialog::startExport()
{
    if (m_running) return;
    m_activeSource = currentSource();

    if (m_activeSource == SelectedTables) { startTableExport(); return; }

    const bool sql = m_format->currentData().toInt() == SQL;
    QString table;
    SAResult inline_result;
    bool useInlineRows = false;

    if (m_activeSource == QueryResult) {
        SACustomQueryView *query = m_document->queryView();
        if (!query || query->model()->rowCount() == 0) {
            SADialogs::warning(this, tr("Nothing to export"), tr("The query view holds no result set."));
            return;
        }
        if (query->isRunning()) {
            SADialogs::warning(this, tr("Query still running"), tr("Wait for the query to finish before exporting its result."));
            return;
        }
        table = m_targetTable->text().trimmed();
        if (table.isEmpty()) table = query->resultTableName();
        if (sql && table.isEmpty()) {
            SADialogs::warning(this, tr("Target table needed"),
                               tr("The result set does not come from a single table, so the INSERT statements need a table name."));
            return;
        }
        // The model sorts and edits SAResult in place, so this is exactly the
        // grid the user is looking at.
        inline_result = query->model()->result();
        useInlineRows = true;
    } else {
        SATableContentView *content = m_document->contentView();
        if (!content || content->tableName().isEmpty()) {
            SADialogs::warning(this, tr("Nothing to export"), tr("No table is loaded in the Content view."));
            return;
        }
        if (content->isLoading()) {
            SADialogs::warning(this, tr("Content still loading"), tr("Wait for the table to finish loading before exporting it."));
            return;
        }
        table = content->tableName();
        useInlineRows = m_currentPageOnly->isChecked();
        if (useInlineRows) inline_result = content->model()->result();
    }

    if (m_output->text().trimmed().isEmpty()) { chooseOutput(); if (m_output->text().trimmed().isEmpty()) return; }

    if (useInlineRows) { startResultExport(inline_result, table); return; }

    // Re-run the SELECT in batches with the filter that is actually applied, so
    // the file holds every matching row and not just the loaded page.
    const SATableContentView *content = m_document->contentView();
    m_queue = QStringList{table};
    m_where = content ? content->activeFilter() : QString();
    m_orderBy = orderByForContent();
    startTableExport();
}

void SAExportDialog::startResultExport(const SAResult &result, const QString &table)
{
    m_file.setFileName(m_output->text().trimmed());
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        SADialogs::warning(this, tr("Export failed"), m_file.errorString());
        return;
    }
    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);
    m_stream.setGenerateByteOrderMark(false);

    m_running = true;
    m_cancelled = false;
    m_rowsWritten = 0;
    m_exportButton->setEnabled(false);
    m_progress->setRange(0, 100);

    const bool sql = m_format->currentData().toInt() == SQL;
    if (sql) m_stream << sqlDumpHeader();
    else m_stream << SAResultExport::csvHeaderLine(result.fields, csvOptions());

    writeRows(result.fields, result.rows, table);
    m_rowsWritten = quint64(result.rows.size());

    if (sql) m_stream << QStringLiteral("\nSET FOREIGN_KEY_CHECKS = 1;\n");
    finish(tr("Exported %n row(s).", nullptr, int(qMin<quint64>(m_rowsWritten, INT_MAX))), false);
}

void SAExportDialog::writeRows(const QVector<SAField> &fields, const QVector<SARow> &rows, const QString &table)
{
    if (m_format->currentData().toInt() == SQL) {
        if (rows.isEmpty()) return;   // never emit "INSERT ... VALUES ;"
        SAContentEditing::Escaper esc;
        esc.string = [](const QString &s) { return SADatabaseSession::escapeString(s, true); };
        esc.data = [](const QByteArray &d) { return SAMySQLConnection().escapeAndQuoteData(d); };
        m_stream << SAContentEditing::insertStatementForRows(table, fieldsForInsert(fields), rows, false, esc)
                 << QStringLiteral("\n\n");
    } else {
        m_stream << SAResultExport::csvRows(fields, rows, csvOptions());
    }
}

void SAExportDialog::startTableExport()
{
    if (m_activeSource == SelectedTables) {
        m_queue = selectedTables();
        if (m_queue.isEmpty()) { SADialogs::warning(this, tr("Nothing selected"), tr("Please choose at least one table.")); return; }
        m_where.clear();
        m_orderBy.clear();
    }
    if (m_output->text().trimmed().isEmpty()) { chooseOutput(); if (m_output->text().trimmed().isEmpty()) return; }
    m_perTableFiles = writesOneFilePerTable();
    if (!m_perTableFiles) {
        m_file.setFileName(m_output->text().trimmed());
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { SADialogs::warning(this, tr("Export failed"), m_file.errorString()); return; }
        m_stream.setDevice(&m_file);
        m_stream.setEncoding(QStringConverter::Utf8);
        m_stream.setGenerateByteOrderMark(false);
        if (m_format->currentData().toInt() == SQL) m_stream << sqlDumpHeader();
    }
    m_running = true;
    m_cancelled = false;
    m_tableIndex = 0;
    m_rowsWritten = 0;
    m_exportButton->setEnabled(false);
    m_tables->setEnabled(false);
    // A filtered row count is unknown, so there is no honest percentage.
    const bool indeterminate = m_activeSource == FilteredContent && !m_where.trimmed().isEmpty();
    m_progress->setRange(0, indeterminate ? 0 : 100);
    exportNextTable();
}

void SAExportDialog::exportNextTable()
{
    if (m_cancelled) { finish(tr("Export cancelled."), true); return; }
    if (m_tableIndex >= m_queue.size()) {
        if (m_format->currentData().toInt() == SQL) m_stream << QStringLiteral("\nSET FOREIGN_KEY_CHECKS = 1;\n");
        if (m_file.isOpen()) { m_stream.flush(); m_file.close(); }
        if (m_activeSource == SelectedTables)
            finish(tr("Exported %n table(s), %1 rows.", nullptr, m_queue.size()).arg(m_rowsWritten), false);
        else
            finish(tr("Exported %n row(s).", nullptr, int(qMin<quint64>(m_rowsWritten, INT_MAX))), false);
        return;
    }
    m_currentTable = m_queue.at(m_tableIndex);
    m_offset = 0;
    if (m_progress->maximum() > 0) m_progress->setValue(int(100.0 * m_tableIndex / m_queue.size()));
    m_status->setText(tr("Exporting %1…").arg(m_currentTable));

    const bool sql = m_format->currentData().toInt() == SQL;
    if (m_perTableFiles) {
        const QString path = QDir(m_output->text().trimmed()).filePath(m_currentTable + QStringLiteral(".csv"));
        if (m_file.isOpen()) m_file.close();
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { finish(tr("Could not write %1: %2").arg(path, m_file.errorString()), true); return; }
        m_stream.setDevice(&m_file);
        m_stream.setEncoding(QStringConverter::Utf8);
        m_stream.setGenerateByteOrderMark(false);
    }

    QStringList statements{SASchema::showFullColumns(m_currentTable)};
    if (sql && m_sqlStructure->isChecked()) statements << SASchema::showCreate(SASchema::ObjectType::Table, m_currentTable);
    m_document->session()->queryBatch(statements, [this, sql](const QVector<SAResult> &results) {
        if (results.isEmpty() || !results[0].ok) { finish(tr("Could not read the structure of %1.").arg(m_currentTable), true); return; }
        m_currentColumns = SASchema::parseColumns(results[0], m_document->session()->serverInfo().isMariaDB);
        if (sql) {
            m_stream << QStringLiteral("-- Table %1\n").arg(m_currentTable);
            if (m_sqlStructure->isChecked() && results.size() > 1 && results[1].ok && results[1].rowCount()) {
                int col = 1;
                for (int i = 0; i < results[1].fieldCount(); ++i) if (results[1].fields.at(i).name.startsWith(QLatin1String("Create "))) col = i;
                if (m_sqlDrop->isChecked()) m_stream << QStringLiteral("DROP TABLE IF EXISTS %1;\n").arg(SADatabaseSession::quoteIdentifier(m_currentTable));
                m_stream << results[1].stringAt(0, col) << QStringLiteral(";\n\n");
            }
            if (!m_sqlContent->isChecked()) { ++m_tableIndex; exportNextTable(); return; }
        } else if (m_perTableFiles || m_tableIndex == 0) {
            m_stream << SAResultExport::csvHeaderLine(m_currentColumns, csvOptions());
        }
        fetchBatch();
    }, SADatabaseSession::Silent);
}

void SAExportDialog::fetchBatch()
{
    if (m_cancelled) { finish(tr("Export cancelled."), true); return; }
    QStringList fields;
    for (const SASchema::Column &c : m_currentColumns) fields << SADatabaseSession::quoteIdentifier(c.name);
    const QString sql = SAResultExport::batchSelect(SADatabaseSession::quoteIdentifier(m_currentTable), fields,
                                                    m_where, m_orderBy, m_offset, BatchSize);
    m_document->session()->query(sql, [this](const SAResult &r) {
        if (!r.ok) { finish(tr("Error reading %1: %2").arg(m_currentTable, r.errorMessage), true); return; }
        writeRows(r.fields, r.rows, m_currentTable);
        m_rowsWritten += quint64(r.rowCount());
        m_offset += BatchSize;
        m_status->setText(tr("Exporting %1… %2 rows").arg(m_currentTable).arg(m_rowsWritten));
        if (quint64(r.rowCount()) < BatchSize) {
            ++m_tableIndex;
            exportNextTable();
        } else {
            fetchBatch();
        }
    }, SADatabaseSession::Silent);
}

void SAExportDialog::finish(const QString &message, bool failed)
{
    m_running = false;
    if (m_file.isOpen()) { m_stream.flush(); m_file.close(); }
    const int previous = m_progress->value();
    m_progress->setRange(0, 100);
    m_progress->setValue(failed ? previous : 100);
    m_status->setText(message);
    m_exportButton->setEnabled(true);
    // Restores the enabled state that belongs to the selected source, instead
    // of re-enabling the table list behind a source that does not use it.
    updateOptionsForSource();
    m_cancelButton->setText(tr("Close"));
}
