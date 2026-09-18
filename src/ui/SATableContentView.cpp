//
//  SATableContentView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATableContentView.h"
#include "SAContentEditing.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAFieldEditorDialog.h"
#include "SAFilterRuleWidget.h"
#include "SAIcons.h"
#include "SAPreferences.h"
#include "SAResultModel.h"
#include "SAResultTableView.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

SATableContentView::SATableContentView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    buildUI();
    connect(&SAPreferences::instance(), &SAPreferences::changed, this, [this](const QString &key) {
        if (key == SAPreferences::DisplayBinaryDataAsHex) m_model->setBinaryAsHex(SAPreferences::instance().boolFor(key));
        else if (key == SAPreferences::NullValue) m_model->setNullString(SAPreferences::instance().stringFor(key));
        else if (key == SAPreferences::DisplayTableViewColumnTypes) m_table->setShowColumnTypes(SAPreferences::instance().boolFor(key));
        else if (key == SAPreferences::DisplayTableViewVerticalGridlines) m_table->setVerticalGridlines(SAPreferences::instance().boolFor(key));
    });
}

void SATableContentView::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Filter bar
    auto *filterBar = new QWidget;
    auto *fb = new QHBoxLayout(filterBar);
    fb->setContentsMargins(6, 4, 6, 4);
    m_filterStack = new QStackedWidget;
    m_filterGroup = new SAFilterGroupWidget(m_columns, true);
    m_filterGroup->setNode(SAFilterNode::makeGroup(true, true));
    m_filterScroll = new QScrollArea;
    m_filterScroll->setWidget(m_filterGroup);
    m_filterScroll->setWidgetResizable(true);
    m_filterScroll->setFrameShape(QFrame::NoFrame);
    m_filterScroll->setMaximumHeight(220);
    m_filterStack->addWidget(m_filterScroll);
    m_filterWhere = new QLineEdit;
    m_filterWhere->setPlaceholderText(tr("WHERE clause, e.g. status = 'active' AND created_at > '2024-01-01'"));
    connect(m_filterWhere, &QLineEdit::returnPressed, this, &SATableContentView::applyFilter);
    m_filterStack->addWidget(m_filterWhere);
    fb->addWidget(m_filterStack, 1);
    m_filterCustom = new QCheckBox(tr("Custom WHERE"));
    connect(m_filterCustom, &QCheckBox::toggled, this, [this](bool on) { m_filterStack->setCurrentIndex(on ? 1 : 0); });
    fb->addWidget(m_filterCustom);
    m_filterButton = new QToolButton;
    m_filterButton->setText(tr("Filter"));
    m_filterButton->setIcon(SAIcons::icon(SAIcons::Glyph::Filter));
    m_filterButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filterButton->setToolTip(tr("Apply the filter (hold Shift for a case-sensitive BINARY comparison)"));
    connect(m_filterButton, &QToolButton::clicked, this, &SATableContentView::applyFilter);
    fb->addWidget(m_filterButton);
    auto *resetButton = new QToolButton;
    resetButton->setText(tr("Reset"));
    resetButton->setAutoRaise(true);
    connect(resetButton, &QToolButton::clicked, this, &SATableContentView::resetFilter);
    fb->addWidget(resetButton);
    layout->addWidget(filterBar);

    // Grid
    m_model = new SAResultModel(this);
    SAPreferences &prefs = SAPreferences::instance();
    m_model->setNullString(prefs.stringFor(SAPreferences::NullValue));
    m_model->setBinaryAsHex(prefs.boolFor(SAPreferences::DisplayBinaryDataAsHex));
    m_table = new SAResultTableView;
    m_table->setResultModel(m_model);
    m_table->setShowColumnTypes(prefs.boolFor(SAPreferences::DisplayTableViewColumnTypes));
    m_table->setVerticalGridlines(prefs.boolFor(SAPreferences::DisplayTableViewVerticalGridlines));
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, &SATableContentView::headerClicked);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &SATableContentView::cellEdited);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SATableContentView::currentRowChanged);
    connect(m_table, &SAResultTableView::deleteRowsRequested, this, &SATableContentView::deleteRows);
    connect(m_table, &SAResultTableView::setNullRequested, this, &SATableContentView::setSelectedCellsNull);
    connect(m_table, &SAResultTableView::editInSheetRequested, this, &SATableContentView::openFieldEditor);
    connect(m_table, &SAResultTableView::copyAsInsertRequested, this, &SATableContentView::copyAsSQLInsert);
    connect(m_table, &SAResultTableView::addRowRequested, this, &SATableContentView::addRow);
    connect(m_table, &SAResultTableView::duplicateRowRequested, this, &SATableContentView::duplicateRow);
    connect(m_table, &SAResultTableView::reloadRequested, this, &SATableContentView::reload);
    connect(m_table, &SAResultTableView::columnFilterRequested, this, &SATableContentView::filterByCell);
    connect(m_table, &SAResultTableView::exportRequested, this, [this]() { m_document->exportCurrentResult(); });
    layout->addWidget(m_table, 1);

    // Bottom bar
    auto *bottom = new QWidget;
    auto *bb = new QHBoxLayout(bottom);
    bb->setContentsMargins(6, 3, 6, 3);
    m_addButton = new QToolButton;
    m_addButton->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    m_addButton->setToolTip(tr("Add a new row"));
    connect(m_addButton, &QToolButton::clicked, this, &SATableContentView::addRow);
    m_duplicateButton = new QToolButton;
    m_duplicateButton->setIcon(SAIcons::icon(SAIcons::Glyph::Duplicate));
    m_duplicateButton->setToolTip(tr("Duplicate the selected row"));
    connect(m_duplicateButton, &QToolButton::clicked, this, &SATableContentView::duplicateRow);
    m_removeButton = new QToolButton;
    m_removeButton->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_removeButton->setToolTip(tr("Delete the selected rows"));
    connect(m_removeButton, &QToolButton::clicked, this, &SATableContentView::deleteRows);
    m_reloadButton = new QToolButton;
    m_reloadButton->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    m_reloadButton->setToolTip(tr("Reload the table content"));
    connect(m_reloadButton, &QToolButton::clicked, this, &SATableContentView::reload);
    for (QToolButton *b : {m_addButton, m_duplicateButton, m_removeButton, m_reloadButton}) { b->setAutoRaise(true); bb->addWidget(b); }
    bb->addSpacing(12);
    m_prevPage = new QToolButton;
    m_prevPage->setIcon(SAIcons::icon(SAIcons::Glyph::Left));
    m_prevPage->setAutoRaise(true);
    connect(m_prevPage, &QToolButton::clicked, this, [this]() { goToPage(m_page - 1); });
    m_pageBox = new QSpinBox;
    m_pageBox->setRange(1, 1);
    m_pageBox->setKeyboardTracking(false);
    connect(m_pageBox, &QSpinBox::valueChanged, this, [this](int page) { if (!m_loading && page != m_page) goToPage(page); });
    m_pageLabel = new QLabel;
    m_nextPage = new QToolButton;
    m_nextPage->setIcon(SAIcons::icon(SAIcons::Glyph::Right));
    m_nextPage->setAutoRaise(true);
    connect(m_nextPage, &QToolButton::clicked, this, [this]() { goToPage(m_page + 1); });
    bb->addWidget(m_prevPage);
    bb->addWidget(new QLabel(tr("Page")));
    bb->addWidget(m_pageBox);
    bb->addWidget(m_pageLabel);
    bb->addWidget(m_nextPage);
    bb->addStretch();
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    bb->addWidget(m_countLabel);
    layout->addWidget(bottom);
}

SAContentEditing::Escaper SATableContentView::escaper() const
{
    SAContentEditing::Escaper e;
    e.string = [](const QString &s) { return SADatabaseSession::escapeString(s, true); };
    e.data = [](const QByteArray &d) { return SAMySQLConnection().escapeAndQuoteData(d); };
    return e;
}

// ---- loading -------------------------------------------------------------------------

void SATableContentView::clear()
{
    m_table_name.clear();
    m_type = SASchema::ObjectType::None;
    m_columns.clear();
    m_primaryKeys.clear();
    m_model->setResult(SAResult());
    m_filterGroup->setColumns({});
    m_filterGroup->setNode(SAFilterNode::makeGroup(true, true));
    m_activeFilter.clear();
    m_filterWhere->clear();
    m_sortColumn = -1;
    m_page = 1;
    m_totalRows = -1;
    m_editingRow = -1;
    m_countLabel->clear();
    m_pageLabel->clear();
    setEnabled(false);
}

void SATableContentView::loadTable(const QString &name, SASchema::ObjectType type)
{
    const bool sameTable = (name == m_table_name && type == m_type);
    m_table_name = name;
    m_type = type;
    m_editingRow = -1;
    if (!sameTable) {
        m_activeFilter.clear();
        m_filterGroup->setNode(SAFilterNode::makeGroup(true, true));
        m_filterWhere->clear();
        m_sortColumn = -1;
        m_sortDescending = false;
        m_page = 1;
    }
    m_totalRows = -1;
    if (type != SASchema::ObjectType::Table && type != SASchema::ObjectType::View) {
        m_model->setResult(SAResult());
        m_columns.clear();
        m_filterGroup->setColumns({});
        m_countLabel->setText(tr("Procedures and functions have no content to browse."));
        setEnabled(true);
        return;
    }
    setEnabled(true);
    const bool editable = type == SASchema::ObjectType::Table;
    m_model->setEditable(editable);
    m_table->setEditingEnabled(editable);
    m_addButton->setEnabled(editable);
    m_duplicateButton->setEnabled(editable);
    m_removeButton->setEnabled(editable);
    loadColumns([this]() { loadRowCount([this]() { loadValues(); }); });
}

void SATableContentView::reload()
{
    if (m_table_name.isEmpty()) return;
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    m_totalRows = -1;
    loadColumns([this]() { loadRowCount([this]() { loadValues(); }); });
}

void SATableContentView::loadColumns(std::function<void()> then)
{
    const QString table = m_table_name;
    m_document->session()->query(SASchema::showFullColumns(table), [this, table, then](const SAResult &r) {
        if (table != m_table_name) return;
        if (!r.ok) {
            m_document->reportError(tr("Error"), tr("The table structure could not be loaded.\n\nMySQL said: %1").arg(r.errorMessage));
            return;
        }
        m_columns = SASchema::parseColumns(r, m_document->session()->serverInfo().isMariaDB);
        m_primaryKeys = SASchema::primaryKeyColumns(m_columns);
        m_filterGroup->setColumns(m_columns);
        then();
    }, SADatabaseSession::Silent);
}

void SATableContentView::loadRowCount(std::function<void()> then)
{
    const QString table = m_table_name;
    if (!m_activeFilter.isEmpty()) {
        // Filtered counts are derived from the result itself.
        m_totalRows = -1;
        then();
        return;
    }
    m_document->session()->query(SASchema::tableStatusLike(table, m_document->escaper()), [this, table, then](const SAResult &r) {
        if (table != m_table_name) return;
        m_totalRows = -1;
        m_totalIsExact = false;
        if (r.ok && r.rowCount()) {
            const QMap<QString, QString> status = r.rowAsMap(0);
            const QString engine = status.value(QStringLiteral("Engine"));
            const qint64 dataLength = status.value(QStringLiteral("Data_length")).toLongLong();
            m_totalRows = status.value(QStringLiteral("Rows")).isEmpty() ? -1 : status.value(QStringLiteral("Rows")).toLongLong();
            m_totalIsExact = engine.compare(QLatin1String("MyISAM"), Qt::CaseInsensitive) == 0;
            const int level = SAPreferences::instance().intFor(SAPreferences::TableRowCountQueryLevel);
            const qint64 boundary = SAPreferences::instance().value(SAPreferences::TableRowCountCheapLookupSizeBoundary).toLongLong();
            if (!m_totalIsExact && (level == 2 || (level == 1 && dataLength < boundary))) {
                m_document->session()->query(SASchema::countRows(table), [this, table, then](const SAResult &c) {
                    if (table != m_table_name) return;
                    if (c.ok) { m_totalRows = c.firstValue().toLongLong(); m_totalIsExact = true; }
                    then();
                }, SADatabaseSession::Silent);
                return;
            }
        }
        then();
    }, SADatabaseSession::Silent);
}

void SATableContentView::loadValues()
{
    if (m_table_name.isEmpty() || m_columns.isEmpty()) return;
    SAPreferences &prefs = SAPreferences::instance();
    const bool limit = prefs.boolFor(SAPreferences::LimitResults);
    const int limitValue = qMax(1, prefs.intFor(SAPreferences::LimitResultsValue));

    QStringList fields;
    for (const SASchema::Column &c : m_columns) fields << SADatabaseSession::quoteIdentifier(c.name);
    QString sql = QStringLiteral("SELECT %1 FROM %2").arg(fields.join(QStringLiteral(", ")), SADatabaseSession::quoteIdentifier(m_table_name));
    if (!m_activeFilter.isEmpty()) sql += QStringLiteral(" WHERE %1").arg(m_activeFilter);
    if (m_sortColumn >= 0 && m_sortColumn < m_columns.size()) {
        sql += QStringLiteral(" ORDER BY %1").arg(SADatabaseSession::quoteIdentifier(m_columns.at(m_sortColumn).name));
        if (m_sortDescending) sql += QStringLiteral(" DESC");
    }
    if (limit) {
        if (m_page < 1) m_page = 1;
        if (m_totalRows >= 0 && m_page > 1 && qint64(m_page - 1) * limitValue >= m_totalRows) m_page = qMax(1, int((m_totalRows + limitValue - 1) / limitValue));
        sql += QStringLiteral(" LIMIT %1,%2").arg(qint64(m_page - 1) * limitValue).arg(limitValue);
    }
    m_lastUsedQuery = sql;
    m_loading = true;
    const QString table = m_table_name;
    m_document->session()->query(sql, [this, table, limit, limitValue](const SAResult &r) {
        if (table != m_table_name) return;
        m_loading = false;
        if (!r.ok) {
            if (r.wasCancelled) { m_countLabel->setText(tr("Loading cancelled.")); return; }
            m_document->reportError(tr("Error"), m_activeFilter.isEmpty()
                ? tr("The table data couldn't be loaded.\n\nMySQL said: %1").arg(r.errorMessage)
                : tr("The table data couldn't be loaded presumably due to the used filter clause.\n\nMySQL said: %1").arg(r.errorMessage));
            return;
        }
        if (limit && m_page > 1 && r.rowCount() == 0) {
            // Late page beyond the data: fall back to the first page.
            m_page = 1;
            loadValues();
            return;
        }
        m_model->setResult(r);
        m_editingRow = -1;
        m_model->setSortIndicator(m_sortColumn, m_sortDescending ? Qt::DescendingOrder : Qt::AscendingOrder);
        m_limited = limit && (m_page > 1 || r.rowCount() == limitValue);
        if (!m_activeFilter.isEmpty()) {
            if (!m_limited) { m_totalRows = r.rowCount(); m_totalIsExact = true; }
            else if (m_totalRows < 0) m_totalRows = -1;
        } else if (!m_limited && m_page == 1) {
            m_totalRows = r.rowCount();
            m_totalIsExact = true;
        }
        m_table->autosizeColumns();
        updateCountText();
        updatePagination();
    });
}

void SATableContentView::updateCountText()
{
    const int rows = m_model->rowCount();
    const QLocale locale;
    QString text;
    if (!m_limited && m_activeFilter.isEmpty()) {
        text = tr("%n row(s) in table", nullptr, rows);
    } else if (!m_limited) {
        text = tr("%n row(s) match the filter", nullptr, rows);
    } else {
        const int limitValue = qMax(1, SAPreferences::instance().intFor(SAPreferences::LimitResultsValue));
        const qint64 first = qint64(m_page - 1) * limitValue + 1;
        const qint64 last = first + rows - 1;
        if (m_totalRows >= 0) {
            text = tr("Rows %1 - %2 of %3%4%5").arg(locale.toString(first), locale.toString(last), m_totalIsExact ? QString() : QStringLiteral("~"),
                                                    locale.toString(m_totalRows), m_activeFilter.isEmpty() ? tr(" in table") : tr(" matching the filter"));
        } else {
            text = tr("Rows %1 - %2%3").arg(locale.toString(first), locale.toString(last), m_activeFilter.isEmpty() ? QString() : tr(" matching the filter"));
        }
    }
    if (!m_activeFilter.isEmpty()) text += tr(" · WHERE %1").arg(m_activeFilter.size() > 60 ? m_activeFilter.left(60) + QStringLiteral("…") : m_activeFilter);
    m_countLabel->setText(text);
}

void SATableContentView::updatePagination()
{
    const bool limit = SAPreferences::instance().boolFor(SAPreferences::LimitResults);
    const int limitValue = qMax(1, SAPreferences::instance().intFor(SAPreferences::LimitResultsValue));
    int pages = 1;
    if (limit && m_totalRows >= 0) pages = qMax(1, int((m_totalRows + limitValue - 1) / limitValue));
    else if (limit && m_limited) pages = m_page + 1;
    const QSignalBlocker blocker(m_pageBox);
    m_pageBox->setRange(1, qMax(pages, m_page));
    m_pageBox->setValue(m_page);
    m_pageLabel->setText(m_totalRows >= 0 || !m_limited ? tr("of %1").arg(pages) : tr("of ?"));
    m_prevPage->setEnabled(limit && m_page > 1);
    m_nextPage->setEnabled(limit && m_limited && (m_totalRows < 0 || m_page < pages));
    m_pageBox->setEnabled(limit && (pages > 1 || m_limited));
}

void SATableContentView::goToPage(int page)
{
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    if (page < 1) page = 1;
    m_page = page;
    loadValues();
}

void SATableContentView::headerClicked(int section)
{
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    if (section < 0 || section >= m_columns.size()) return;
    if (m_sortColumn == section) m_sortDescending = !m_sortDescending;
    else { m_sortColumn = section; m_sortDescending = false; }
    m_page = 1;
    loadValues();
}

// ---- filter ---------------------------------------------------------------------------

QString SATableContentView::filterClause(QString *problem) const
{
    Q_UNUSED(problem)
    if (m_filterCustom->isChecked()) return m_filterWhere->text().trimmed();
    const bool caseSensitive = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    return SAFilterTree::buildWhere(m_filterGroup->node(), caseSensitive,
                                    [](const QString &c) { return SADatabaseSession::quoteIdentifier(c); },
                                    [](const QString &s) { return SADatabaseSession::escapeString(s, false); });
}

void SATableContentView::applyFilter()
{
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    QString problem;
    const QString clause = filterClause(&problem);
    if (!problem.isEmpty()) { m_document->reportError(tr("Invalid Filter"), problem); return; }
    m_activeFilter = clause;
    m_page = 1;
    m_totalRows = -1;
    loadRowCount([this]() { loadValues(); });
}

void SATableContentView::resetFilter()
{
    m_filterGroup->setNode(SAFilterNode::makeGroup(true, true));
    m_filterWhere->clear();
    if (m_activeFilter.isEmpty()) return;
    m_activeFilter.clear();
    m_page = 1;
    m_totalRows = -1;
    loadRowCount([this]() { loadValues(); });
}

void SATableContentView::filterByCell(const QModelIndex &index)
{
    if (!index.isValid() || index.column() >= m_columns.size()) return;
    m_filterCustom->setChecked(false);
    const SASchema::Column &column = m_columns.at(index.column());
    const QString filterType = SAContentFilters::filterTypeForTypeGroup(column.typeGroup);
    const QVector<SAContentFilter> &filters = SAContentFilters::filtersForType(filterType);
    const bool isNull = m_model->data(index, SAResultModel::IsNullRole).toBool();
    int op = 0;
    for (int i = 0; i < filters.size(); ++i) {
        if (filters.at(i).menuLabel == (isNull ? QStringLiteral("IS NULL") : QStringLiteral("="))) { op = i; break; }
    }
    SAFilterExpr expr;
    expr.column = column.name;
    expr.filterType = filterType;
    expr.operatorIndex = op;
    if (!isNull) expr.values << QString::fromUtf8(m_model->data(index, SAResultModel::RawBytesRole).toByteArray());
    SAFilterNodePtr root = SAFilterNode::makeGroup(true, true);
    root->children << SAFilterNode::makeLeaf(expr);
    m_filterGroup->setNode(root);
    applyFilter();
}

void SATableContentView::focusFilter()
{
    if (m_filterCustom->isChecked()) m_filterWhere->setFocus();
    else m_filterGroup->setFocus();
}

// ---- editing --------------------------------------------------------------------------

SARow SATableContentView::defaultRow() const
{
    SARow row;
    const QString nullString = SAPreferences::instance().stringFor(SAPreferences::NullValue);
    Q_UNUSED(nullString);
    for (const SASchema::Column &c : m_columns) {
        if (c.isAutoIncrement() || c.isGenerated()) { row << SACell::null(); continue; }
        if (c.hasDefault && !c.defaultIsNull) {
            QString value = c.defaultValue;
            if (c.typeGroup == QLatin1String("bit") && value.size() >= 3 && value.startsWith(QLatin1String("b'"), Qt::CaseInsensitive) && value.endsWith(QLatin1Char('\'')))
                value = value.mid(2, value.size() - 3);
            row << SACell::ofString(value);
            continue;
        }
        if (c.nullable) { row << SACell::null(); continue; }
        row << SACell::ofString(QString());
    }
    return row;
}

void SATableContentView::beginEditingRow(int row)
{
    if (m_editingRow == row) return;
    m_editingRow = row;
    m_oldRow = m_model->rowCells(row);
    m_internalChange = true;
    m_model->setRowEdited(row, true);
    m_internalChange = false;
}

void SATableContentView::cellEdited(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    Q_UNUSED(bottomRight)
    if (m_loading || m_savingRow || m_internalChange || !topLeft.isValid()) return;
    if (m_editingRow < 0) {
        // First edit in this row: snapshot the original values before the change was applied.
        // The model already holds the new value, so rebuild the old row from the snapshot kept by setData.
        m_editingRow = topLeft.row();
        m_editingNewRow = false;
        m_oldRow = m_model->rowCells(topLeft.row());
        m_oldRow[topLeft.column()] = m_cellBeforeEdit;
    } else if (m_editingRow != topLeft.row()) {
        // Edits landed in another row; commit the pending one first.
        saveRow();
    }
}

void SATableContentView::currentRowChanged(const QModelIndex &current, const QModelIndex &previous)
{
    if (current.isValid()) {
        // Remember the value of the cell about to be edited so cellEdited can snapshot it.
        m_cellBeforeEdit = m_model->cellAt(current.row(), current.column());
    }
    if (m_editingRow >= 0 && previous.isValid() && previous.row() == m_editingRow && current.row() != m_editingRow && !m_savingRow) {
        saveRow();
    }
}

bool SATableContentView::commitPendingEdits()
{
    if (m_editingRow < 0) return true;
    return saveRow();
}

bool SATableContentView::saveRow()
{
    if (m_editingRow < 0 || m_savingRow) return true;
    if (m_type != SASchema::ObjectType::Table) { cancelRowEditing(); return true; }
    const int row = m_editingRow;
    const SARow newCells = m_model->rowCells(row);
    if (!m_editingNewRow && newCells == m_oldRow) {
        m_model->setRowEdited(row, false);
        m_editingRow = -1;
        return true;
    }
    SAPreferences &prefs = SAPreferences::instance();
    const QString nullString = prefs.stringFor(SAPreferences::NullValue);
    SAContentEditing::EditedRow edited;
    edited.cells = newCells;
    edited.binaryColumns = m_model->binaryEditedColumns(row);
    QString problem;
    const QString sql = m_editingNewRow
        ? SAContentEditing::insertStatement(m_table_name, m_columns, edited, nullString, escaper())
        : SAContentEditing::updateStatement(m_table_name, m_columns, m_oldRow, edited, m_primaryKeys, nullString, escaper(), &problem);
    if (sql.isEmpty()) {
        if (!problem.isEmpty()) m_document->reportError(tr("Unable to write row"), problem);
        cancelRowEditing();
        return true;
    }
    if (prefs.boolFor(SAPreferences::ShowWarningBeforeExecuteQuery)) {
        if (!SADialogs::confirm(this, tr("Edit row?"), tr("Do you really want to proceed with this query?\n\n%1").arg(sql.size() > 800 ? sql.left(800) + QStringLiteral("…") : sql), tr("Proceed"))) {
            cancelRowEditing();
            return true;
        }
    }
    m_savingRow = true;
    const bool wasNewRow = m_editingNewRow;
    m_document->session()->query(sql, [this, row, wasNewRow](const SAResult &r) {
        m_savingRow = false;
        if (!r.ok) {
            const bool keepEditing = SADialogs::confirm(this, tr("Unable to write row"), tr("MySQL said:\n\n%1").arg(r.errorMessage), tr("Edit row"), tr("Discard changes"));
            if (keepEditing) {
                m_table->setCurrentIndex(m_model->index(row, 0));
                m_table->setFocus();
            } else {
                cancelRowEditing();
            }
            return;
        }
        SAPreferences &prefs = SAPreferences::instance();
        if (r.affectedRows == 0 && !wasNewRow) {
            if (prefs.boolFor(SAPreferences::ShowNoAffectedRowsError)) {
                m_document->reportError(tr("Warning"), tr("The row was not written to the MySQL database. You probably haven't changed anything.\nReload the table to be sure that the row exists and use a primary key for your table.\n(This error can be turned off in the preferences.)"));
            }
            cancelRowEditing();
            return;
        }
        m_editingRow = -1;
        m_editingNewRow = false;
        m_internalChange = true;
        m_model->setRowEdited(row, false);
        if (wasNewRow && !prefs.boolFor(SAPreferences::ReloadAfterAddingRow)) {
            for (int i = 0; i < m_columns.size(); ++i)
                if (m_columns.at(i).isAutoIncrement() && r.insertId) m_model->setCell(row, i, SACell::ofString(QString::number(r.insertId)));
            m_model->setRowEdited(row, false);
        }
        m_internalChange = false;
        if (wasNewRow) {
            if (prefs.boolFor(SAPreferences::ReloadAfterAddingRow)) { m_totalRows = -1; loadRowCount([this]() { loadValues(); }); }
        } else if (prefs.boolFor(SAPreferences::ReloadAfterEditingRow)) {
            loadValues();
        }
        m_document->tableContentChanged();
    });
    return true;
}

void SATableContentView::cancelRowEditing()
{
    if (m_editingRow < 0) return;
    const int row = m_editingRow;
    const bool wasNewRow = m_editingNewRow;
    m_editingRow = -1;
    m_editingNewRow = false;
    m_internalChange = true;
    if (wasNewRow) {
        m_model->removeRowsAt({row});
    } else if (row < m_model->rowCount()) {
        if (!m_oldRow.isEmpty()) m_model->replaceRow(row, m_oldRow);
        m_model->setRowEdited(row, false);
    }
    m_internalChange = false;
    updateCountText();
}

void SATableContentView::addRow()
{
    if (m_type != SASchema::ObjectType::Table || m_columns.isEmpty()) return;
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    const int row = m_model->appendBlankRow(defaultRow());
    m_editingRow = row;
    m_editingNewRow = true;
    m_oldRow = m_model->rowCells(row);
    int firstEditable = 0;
    for (int i = 0; i < m_columns.size(); ++i) if (!m_columns.at(i).isAutoIncrement()) { firstEditable = i; break; }
    const QModelIndex index = m_model->index(row, firstEditable);
    m_table->scrollTo(index);
    m_table->setCurrentIndex(index);
    m_table->edit(index);
}

void SATableContentView::duplicateRow()
{
    if (m_type != SASchema::ObjectType::Table) return;
    const QList<int> rows = m_table->selectedRows();
    if (rows.size() != 1) return;
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    SARow cells = m_model->rowCells(rows.first());
    for (int i = 0; i < m_columns.size() && i < cells.size(); ++i)
        if (m_columns.at(i).isAutoIncrement()) cells[i] = SACell::null();
    const int row = m_model->appendBlankRow(cells);
    m_editingRow = row;
    m_editingNewRow = true;
    m_oldRow = SARow();
    const QModelIndex index = m_model->index(row, 0);
    m_table->scrollTo(index);
    m_table->setCurrentIndex(index);
    // Duplicates are written immediately, like the macOS app.
    saveRow();
}

void SATableContentView::deleteRows()
{
    if (m_type != SASchema::ObjectType::Table) return;
    QList<int> rows = m_table->selectedRows();
    if (rows.isEmpty()) return;
    if (m_editingRow >= 0 && rows.contains(m_editingRow) && m_editingNewRow) { cancelRowEditing(); return; }
    if (m_editingRow >= 0 && !commitPendingEdits()) return;
    SAPreferences &prefs = SAPreferences::instance();
    const bool allRows = m_activeFilter.isEmpty() && !m_limited && rows.size() == m_model->rowCount();
    if (prefs.boolFor(SAPreferences::ShowWarningBeforeDeleteQuery)) {
        const QString text = allRows ? tr("Are you sure you want to delete all %n row(s) from this table? This action cannot be undone.", nullptr, rows.size())
                                     : tr("Are you sure you want to delete the selected %n row(s) from this table? This action cannot be undone.", nullptr, rows.size());
        if (!SADialogs::confirm(this, tr("Delete rows?"), text, tr("Delete"), QString(), true)) return;
    }
    QStringList statements;
    if (allRows) {
        statements << QStringLiteral("DELETE FROM %1").arg(SADatabaseSession::quoteIdentifier(m_table_name));
        if (prefs.boolFor(SAPreferences::ResetAutoIncrementAfterDeletionOfAllRows)) {
            for (const SASchema::Column &c : m_columns)
                if (c.isAutoIncrement()) statements << SASchema::setAutoIncrement(m_table_name, 1);
        }
    } else {
        QVector<SARow> selected;
        for (int r : rows) selected << m_model->rowCells(r);
        QString problem;
        statements = SAContentEditing::deleteStatements(m_table_name, m_columns, selected, m_primaryKeys, escaper(), &problem);
        if (statements.isEmpty()) { m_document->reportError(tr("Error"), problem.isEmpty() ? tr("The rows could not be identified for deletion.") : problem); return; }
    }
    m_document->session()->queryBatch(statements, [this, rows](const QVector<SAResult> &results) {
        quint64 affected = 0;
        QStringList errors;
        for (const SAResult &r : results) {
            if (!r.ok) errors << r.errorMessage;
            else if (r.affectedRows != ~0ULL) affected += r.affectedRows;
        }
        if (!errors.isEmpty()) m_document->reportError(tr("Error"), tr("Some rows could not be deleted.\n\nMySQL said: %1").arg(errors.join(QLatin1Char('\n'))));
        else if (affected != quint64(rows.size()) && !SAPreferences::instance().boolFor(SAPreferences::ReloadAfterRemovingRow)) {
            SADialogs::information(this, tr("Rows deleted"), tr("%1 rows were deleted, but %2 were selected. Reload the table to be sure that the contents have not changed in the meantime.").arg(affected).arg(rows.size()));
        }
        if (SAPreferences::instance().boolFor(SAPreferences::ReloadAfterRemovingRow) || !errors.isEmpty()) {
            m_totalRows = -1;
            loadRowCount([this]() { loadValues(); });
        } else {
            m_model->removeRowsAt(rows);
            if (m_totalRows >= 0) m_totalRows = qMax<qint64>(0, m_totalRows - qint64(affected));
            updateCountText();
            updatePagination();
        }
        m_document->tableContentChanged();
    });
}

void SATableContentView::setSelectedCellsNull()
{
    if (!m_model->isEditable()) return;
    const QModelIndexList indexes = m_table->selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) return;
    const int row = indexes.first().row();
    for (const QModelIndex &i : indexes) if (i.row() != row) { m_document->reportError(tr("Set NULL"), tr("Please select cells of a single row.")); return; }
    if (m_editingRow >= 0 && m_editingRow != row && !commitPendingEdits()) return;
    if (m_editingRow < 0) beginEditingRow(row);
    m_internalChange = true;   // bookkeeping already done by beginEditingRow
    for (const QModelIndex &i : indexes) m_model->setCell(i.row(), i.column(), SACell::null());
    m_internalChange = false;
    if (!m_editingNewRow) saveRow();
}

void SATableContentView::openFieldEditor(const QModelIndex &index)
{
    if (!index.isValid() || index.column() >= m_columns.size()) return;
    const SASchema::Column &column = m_columns.at(index.column());
    const SACell cell = m_model->cellAt(index.row(), index.column());
    const bool editable = m_model->isEditable() && !column.isGenerated();
    SAFieldEditorDialog dialog(this, QStringLiteral("%1.%2").arg(m_table_name, column.name), column.typeGroup, column.fullType, cell, editable, column.nullable);
    if (dialog.exec() != QDialog::Accepted || !editable) return;
    const SACell value = dialog.value();
    if (value == cell) return;
    if (m_editingRow >= 0 && m_editingRow != index.row() && !commitPendingEdits()) return;
    if (m_editingRow < 0) beginEditingRow(index.row());
    m_internalChange = true;
    m_model->setCell(index.row(), index.column(), value, dialog.valueIsBinary());
    m_internalChange = false;
    if (!m_editingNewRow) saveRow();
}

// ---- copying ----------------------------------------------------------------------------

void SATableContentView::copyWithColumnNames()
{
    m_table->copySelection(true);
}

void SATableContentView::copyAsSQLInsert(bool skipAutoIncrement)
{
    const QList<int> rows = m_table->selectedRows();
    if (rows.isEmpty()) return;
    QVector<SARow> selected;
    for (int r : rows) selected << m_model->rowCells(r);
    const QString sql = SAContentEditing::insertStatementForRows(m_table_name, m_model->result().fields, selected, skipAutoIncrement, escaper());
    if (!sql.isEmpty()) QApplication::clipboard()->setText(sql);
}
