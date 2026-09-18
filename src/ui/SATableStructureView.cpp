//
//  SATableStructureView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATableStructureView.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAIcons.h"
#include "SAPreferences.h"
#include "SASQLTypes.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFontMetrics>
#include <QLabel>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

// ---- delegate --------------------------------------------------------------------

SAComboDelegate::SAComboDelegate(std::function<QStringList(const QModelIndex &)> items, bool editable, QObject *parent)
    : QStyledItemDelegate(parent), m_items(std::move(items)), m_editable(editable)
{
}

QWidget *SAComboDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const
{
    auto *box = new QComboBox(parent);
    box->setEditable(m_editable);
    box->addItems(m_items(index));
    box->setInsertPolicy(QComboBox::NoInsert);
    return box;
}

void SAComboDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *box = qobject_cast<QComboBox *>(editor);
    const QString value = index.data(Qt::EditRole).toString();
    const int i = box->findText(value, Qt::MatchFixedString);
    if (i >= 0) box->setCurrentIndex(i);
    else if (m_editable) box->setCurrentText(value);
}

void SAComboDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    auto *box = qobject_cast<QComboBox *>(editor);
    model->setData(index, box->currentText().trimmed(), Qt::EditRole);
}

// ---- view -------------------------------------------------------------------------

SATableStructureView::SATableStructureView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    buildUI();
}

void SATableStructureView::buildUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_message = new QLabel;
    m_message->setContentsMargins(8, 6, 8, 6);
    m_message->setVisible(false);
    layout->addWidget(m_message);

    auto *splitter = new QSplitter(Qt::Vertical);

    auto *fieldsPane = new QWidget;
    auto *fl = new QVBoxLayout(fieldsPane);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(0);
    m_fields = new QTableWidget(0, ColCount);
    m_fields->setHorizontalHeaderLabels({tr("Field"), tr("Type"), tr("Length"), tr("Unsigned"), tr("Zerofill"), tr("Binary"), tr("Allow Null"),
                                         tr("Key"), tr("Default"), tr("Extra"), tr("Encoding"), tr("Collation"), tr("Comment")});
    m_fields->verticalHeader()->setVisible(false);
    m_fields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fields->setAlternatingRowColors(true);
    m_fields->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_fields->horizontalHeader()->setStretchLastSection(true);
    m_fields->setItemDelegateForColumn(ColType, new SAComboDelegate([](const QModelIndex &) {
        QStringList types = SASQLTypes::suggestedTypes();
        types.removeAll(QStringLiteral("--------"));
        return types;
    }, true, this));
    m_fields->setItemDelegateForColumn(ColExtra, new SAComboDelegate([](const QModelIndex &) {
        return QStringList{QString(), QStringLiteral("auto_increment"), QStringLiteral("on update CURRENT_TIMESTAMP"), QStringLiteral("SERIAL DEFAULT VALUE"),
                           QStringLiteral("VIRTUAL GENERATED"), QStringLiteral("STORED GENERATED")};
    }, true, this));
    m_fields->setItemDelegateForColumn(ColEncoding, new SAComboDelegate([this](const QModelIndex &) {
        return QStringList{QString()} + m_document->charsetInfo().charsets;
    }, false, this));
    m_fields->setItemDelegateForColumn(ColCollation, new SAComboDelegate([this](const QModelIndex &index) {
        const QString charset = m_fields->item(index.row(), ColEncoding) ? m_fields->item(index.row(), ColEncoding)->text() : QString();
        return QStringList{QString()} + m_document->charsetInfo().collations.value(charset);
    }, false, this));
    connect(m_fields, &QTableWidget::itemChanged, this, &SATableStructureView::itemChanged);
    connect(m_fields, &QTableWidget::currentCellChanged, this, &SATableStructureView::currentCellChanged);
    fl->addWidget(m_fields, 1);
    auto *fieldButtons = new QHBoxLayout;
    fieldButtons->setContentsMargins(6, 3, 6, 3);
    m_addField = new QToolButton;
    m_addField->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    m_addField->setToolTip(tr("Add field"));
    connect(m_addField, &QToolButton::clicked, this, &SATableStructureView::addField);
    m_removeField = new QToolButton;
    m_removeField->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_removeField->setToolTip(tr("Remove field"));
    connect(m_removeField, &QToolButton::clicked, this, &SATableStructureView::removeField);
    auto *refresh = new QToolButton;
    refresh->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    refresh->setToolTip(tr("Reload"));
    connect(refresh, &QToolButton::clicked, this, &SATableStructureView::reload);
    for (QToolButton *b : {m_addField, m_removeField, refresh}) { b->setAutoRaise(true); fieldButtons->addWidget(b); }
    fieldButtons->addStretch();
    auto *hint = new QLabel(tr("Double-click a cell to edit; changes are applied when you leave the row."));
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    fieldButtons->addWidget(hint);
    fl->addLayout(fieldButtons);
    splitter->addWidget(fieldsPane);

    auto *indexPane = new QWidget;
    auto *il = new QVBoxLayout(indexPane);
    il->setContentsMargins(0, 0, 0, 0);
    il->setSpacing(0);
    m_indexes = new QTableWidget(0, 4);
    m_indexes->setHorizontalHeaderLabels({tr("Key name"), tr("Type"), tr("Columns"), tr("Comment")});
    m_indexes->verticalHeader()->setVisible(false);
    m_indexes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_indexes->setSelectionMode(QAbstractItemView::SingleSelection);
    m_indexes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_indexes->setAlternatingRowColors(true);
    m_indexes->horizontalHeader()->setStretchLastSection(true);
    il->addWidget(m_indexes, 1);
    auto *indexButtons = new QHBoxLayout;
    indexButtons->setContentsMargins(6, 3, 6, 3);
    m_addIndex = new QToolButton;
    m_addIndex->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    m_addIndex->setToolTip(tr("Add index"));
    connect(m_addIndex, &QToolButton::clicked, this, &SATableStructureView::addIndex);
    m_removeIndex = new QToolButton;
    m_removeIndex->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_removeIndex->setToolTip(tr("Remove index"));
    connect(m_removeIndex, &QToolButton::clicked, this, &SATableStructureView::removeIndex);
    for (QToolButton *b : {m_addIndex, m_removeIndex}) { b->setAutoRaise(true); indexButtons->addWidget(b); }
    indexButtons->addStretch();
    il->addLayout(indexButtons);
    splitter->addWidget(indexPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
}

void SATableStructureView::clear()
{
    m_table.clear();
    m_type = SASchema::ObjectType::None;
    m_columns.clear();
    m_indexList.clear();
    m_populating = true;
    m_fields->setRowCount(0);
    m_indexes->setRowCount(0);
    m_populating = false;
    m_editingRow = -1;
    m_message->setVisible(false);
}

void SATableStructureView::loadTable(const QString &name, SASchema::ObjectType type)
{
    if (m_editingRow >= 0 && m_table == name) commitPendingEdit();
    m_table = name;
    m_type = type;
    m_editingRow = -1;
    const bool editable = type == SASchema::ObjectType::Table;
    for (QToolButton *b : {m_addField, m_removeField, m_addIndex, m_removeIndex}) b->setEnabled(editable);
    m_fields->setEditTriggers(editable ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed) : QAbstractItemView::NoEditTriggers);
    if (type == SASchema::ObjectType::Procedure || type == SASchema::ObjectType::Function) {
        m_populating = true;
        m_fields->setRowCount(0);
        m_indexes->setRowCount(0);
        m_populating = false;
        m_message->setText(tr("Procedures and functions have no columns. Use the Table Info view to see their definition."));
        m_message->setVisible(true);
        return;
    }
    m_message->setVisible(type == SASchema::ObjectType::View);
    if (type == SASchema::ObjectType::View) m_message->setText(tr("Views are read-only here. Edit the view definition with ALTER VIEW in the Query view."));
    const QString table = name;
    m_document->session()->queryBatch({SASchema::showFullColumns(name), SASchema::showIndex(name)}, [this, table](const QVector<SAResult> &results) {
        if (table != m_table || results.size() < 2) return;
        if (!results[0].ok) {
            m_document->reportError(tr("Error"), tr("An error occurred while retrieving the table structure.\n\nMySQL said: %1").arg(results[0].errorMessage));
            return;
        }
        m_columns = SASchema::parseColumns(results[0], m_document->session()->serverInfo().isMariaDB);
        m_indexList = results[1].ok ? SASchema::parseIndexes(results[1]) : QVector<SASchema::Index>();
        populateColumns();
        populateIndexes();
    }, SADatabaseSession::Silent);
}

void SATableStructureView::reload()
{
    if (!m_table.isEmpty()) loadTable(m_table, m_type);
}

void SATableStructureView::setRowFromColumn(int row, const SASchema::Column &c)
{
    auto text = [&](int col, const QString &value, bool editable = true) {
        auto *item = new QTableWidgetItem(value);
        if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_fields->setItem(row, col, item);
    };
    auto check = [&](int col, bool on, bool enabled) {
        auto *item = new QTableWidgetItem;
        item->setFlags((Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable) & (enabled ? ~Qt::NoItemFlags : ~Qt::ItemIsUserCheckable));
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        m_fields->setItem(row, col, item);
    };
    text(ColName, c.name);
    text(ColType, c.type);
    text(ColLength, c.length);
    check(ColUnsigned, c.isUnsigned, SASQLTypes::typeAllowsUnsigned(c.type));
    check(ColZerofill, c.isZerofill, SASQLTypes::typeAllowsUnsigned(c.type));
    check(ColBinary, c.isBinary, SASQLTypes::typeAllowsBinaryAttribute(c.type));
    check(ColNull, c.nullable, true);
    text(ColKey, c.key, false);
    const QString nullString = SAPreferences::instance().stringFor(SAPreferences::NullValue);
    text(ColDefault, c.isGenerated() ? c.defaultValue : (c.defaultIsNull && c.hasDefault ? nullString : c.defaultValue));
    if (c.defaultIsNull && c.hasDefault && !c.isGenerated()) m_fields->item(row, ColDefault)->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    text(ColExtra, c.extra);
    text(ColEncoding, c.charset);
    text(ColCollation, c.collation);
    text(ColComment, c.comment);
    if (c.isPrimary()) {
        QFont bold = m_fields->item(row, ColName)->font();
        bold.setBold(true);
        m_fields->item(row, ColName)->setFont(bold);
        m_fields->item(row, ColName)->setIcon(SAIcons::icon(SAIcons::Glyph::Key));
    }
}

void SATableStructureView::populateColumns()
{
    m_populating = true;
    ++m_generation;
    m_fields->setRowCount(0);
    m_fields->setRowCount(m_columns.size());
    for (int i = 0; i < m_columns.size(); ++i) setRowFromColumn(i, m_columns.at(i));
    m_fields->resizeColumnsToContents();
    // The checkbox columns size to their (tiny) indicator by default; widen
    // them to fit their header label instead of clipping it.
    const QFontMetrics headerMetrics(m_fields->horizontalHeader()->font());
    for (int col : {ColUnsigned, ColZerofill, ColBinary, ColNull}) {
        const int labelWidth = headerMetrics.horizontalAdvance(m_fields->horizontalHeaderItem(col)->text());
        m_fields->setColumnWidth(col, qMax(m_fields->columnWidth(col), labelWidth + 28));
    }
    m_fields->setColumnWidth(ColName, qMax(140, m_fields->columnWidth(ColName)));
    m_populating = false;
    m_editingRow = -1;
    m_editingNewRow = false;
}

void SATableStructureView::populateIndexes()
{
    m_indexes->setRowCount(0);
    m_indexes->setRowCount(m_indexList.size());
    for (int i = 0; i < m_indexList.size(); ++i) {
        const SASchema::Index &index = m_indexList.at(i);
        QString type;
        if (index.isPrimary()) type = QStringLiteral("PRIMARY");
        else if (index.type == QLatin1String("FULLTEXT") || index.type == QLatin1String("SPATIAL")) type = index.type;
        else type = index.unique ? QStringLiteral("UNIQUE") : QStringLiteral("INDEX");
        QStringList cols;
        for (int c = 0; c < index.columns.size(); ++c)
            cols << (index.subParts.value(c).isEmpty() ? index.columns.at(c) : QStringLiteral("%1(%2)").arg(index.columns.at(c), index.subParts.at(c)));
        m_indexes->setItem(i, 0, new QTableWidgetItem(index.isPrimary() ? SAIcons::icon(SAIcons::Glyph::Key) : QIcon(), index.name));
        m_indexes->setItem(i, 1, new QTableWidgetItem(type));
        m_indexes->setItem(i, 2, new QTableWidgetItem(cols.join(QStringLiteral(", "))));
        m_indexes->setItem(i, 3, new QTableWidgetItem(index.comment));
    }
    m_indexes->resizeColumnsToContents();
}

// ---- editing --------------------------------------------------------------------------

SASchema::Column SATableStructureView::columnFromRow(int row) const
{
    SASchema::Column c;
    auto text = [&](int col) { return m_fields->item(row, col) ? m_fields->item(row, col)->text().trimmed() : QString(); };
    auto checked = [&](int col) { return m_fields->item(row, col) && m_fields->item(row, col)->checkState() == Qt::Checked; };
    c.name = text(ColName);
    c.type = text(ColType).toUpper();
    c.length = text(ColLength);
    c.isUnsigned = checked(ColUnsigned);
    c.isZerofill = checked(ColZerofill);
    c.isBinary = checked(ColBinary);
    c.nullable = checked(ColNull);
    c.key = text(ColKey);
    const QString nullString = SAPreferences::instance().stringFor(SAPreferences::NullValue);
    const QString def = text(ColDefault);
    c.extra = text(ColExtra);
    if (c.isGenerated()) {
        c.defaultValue = def;
        c.hasDefault = !def.isEmpty();
    } else if (def == nullString) {
        c.defaultIsNull = true;
        c.hasDefault = true;
    } else {
        c.defaultValue = def;
        c.hasDefault = !def.isEmpty();
    }
    c.charset = text(ColEncoding);
    c.collation = text(ColCollation);
    c.comment = text(ColComment);
    c.typeGroup = SASQLTypes::typeGroupForTypeName(c.type);
    c.fullType = c.type + (c.length.isEmpty() ? QString() : QStringLiteral("(%1)").arg(c.length));
    return c;
}

void SATableStructureView::itemChanged(QTableWidgetItem *item)
{
    if (m_populating || m_saving || !item || m_type != SASchema::ObjectType::Table) return;
    int row = item->row();
    const int col = item->column();
    if (m_editingRow >= 0 && m_editingRow != row) {
        // Committing another row may rebuild the table (or drop an unsaved new
        // row above this one), which destroys every item including this one.
        // Keep the user's change and look the item up again afterwards.
        const QString newText = item->text();
        const Qt::CheckState newState = item->checkState();
        const bool checkable = item->flags() & Qt::ItemIsUserCheckable;
        const int editingRow = m_editingRow;
        const bool editingNewRow = m_editingNewRow;
        const int generation = m_generation;
        if (!commitPendingEdit()) return;
        if (m_generation != generation) {
            if (editingNewRow && editingRow < row) --row;
            item = m_fields->item(row, col);
            if (!item) return;
            m_populating = true;
            if (checkable) item->setCheckState(newState);
            else item->setText(newText);
            m_populating = false;
        }
    }
    if (m_editingRow < 0) {
        m_editingRow = row;
        m_editingNewRow = false;
        m_editingOriginalName = row < m_columns.size() ? m_columns.at(row).name : QString();
    }
    // Type changes toggle which attributes apply.
    if (item->column() == ColType) {
        const QString type = item->text().trimmed().toUpper();
        m_populating = true;
        for (int col : {ColUnsigned, ColZerofill}) {
            QTableWidgetItem *box = m_fields->item(row, col);
            if (!box) continue;
            const bool allowed = SASQLTypes::typeAllowsUnsigned(type);
            box->setFlags(allowed ? (box->flags() | Qt::ItemIsUserCheckable) : (box->flags() & ~Qt::ItemIsUserCheckable));
            if (!allowed) box->setCheckState(Qt::Unchecked);
        }
        if (QTableWidgetItem *box = m_fields->item(row, ColBinary)) {
            const bool allowed = SASQLTypes::typeAllowsBinaryAttribute(type);
            box->setFlags(allowed ? (box->flags() | Qt::ItemIsUserCheckable) : (box->flags() & ~Qt::ItemIsUserCheckable));
            if (!allowed) box->setCheckState(Qt::Unchecked);
        }
        m_populating = false;
    }
    if (item->column() == ColEncoding && m_fields->item(row, ColCollation)) {
        // Reset the collation to the charset default when the charset changes.
        m_populating = true;
        m_fields->item(row, ColCollation)->setText(m_document->charsetInfo().defaultCollation.value(item->text().trimmed()));
        m_populating = false;
    }
    for (int col = 0; col < ColCount; ++col)
        if (QTableWidgetItem *i = m_fields->item(row, col)) {
            QColor c = palette().color(QPalette::Highlight);
            c.setAlpha(40);
            i->setBackground(c);
        }
}

void SATableStructureView::currentCellChanged(int currentRow, int, int previousRow, int)
{
    if (m_populating || m_saving) return;
    if (m_editingRow >= 0 && previousRow == m_editingRow && currentRow != m_editingRow) commitPendingEdit();
}

bool SATableStructureView::commitPendingEdit()
{
    if (m_editingRow < 0 || m_saving || m_type != SASchema::ObjectType::Table) return true;
    const int row = m_editingRow;
    const SASchema::Column column = columnFromRow(row);
    if (column.name.isEmpty() || column.type.isEmpty()) {
        m_document->reportError(tr("Invalid field"), tr("A field needs at least a name and a type."));
        return false;
    }
    if (!m_editingNewRow && row < m_columns.size()) {
        const SASchema::Column &original = m_columns.at(row);
        const QString nullString = SAPreferences::instance().stringFor(SAPreferences::NullValue);
        if (SASchema::columnDefinition(original, nullString, m_document->escaper()) == SASchema::columnDefinition(column, nullString, m_document->escaper())) {
            cancelEdit();
            return true;
        }
    }
    if (column.isAutoIncrement() && !SASQLTypes::typeAllowsAutoIncrement(column.type)) {
        m_document->reportError(tr("Invalid AUTO_INCREMENT"), tr("AUTO_INCREMENT is only allowed for integer and floating point types, not %1.").arg(column.type));
        return false;
    }
    const QString nullString = SAPreferences::instance().stringFor(SAPreferences::NullValue);
    QString sql;
    if (m_editingNewRow) {
        const QString after = row > 0 && row - 1 < m_columns.size() ? m_columns.at(row - 1).name : QString();
        sql = SASchema::addColumn(m_table, column, after, nullString, m_document->escaper());
        if (column.isAutoIncrement() && !column.isPrimary() && SASchema::primaryKeyColumns(m_columns).isEmpty()) sql += QStringLiteral(", ADD PRIMARY KEY (%1)").arg(SADatabaseSession::quoteIdentifier(column.name));
    } else {
        sql = SASchema::changeColumn(m_table, m_editingOriginalName, column, nullString, m_document->escaper());
    }
    m_saving = true;
    const bool wasNew = m_editingNewRow;
    m_document->session()->query(sql, [this, row, wasNew, column, sql](const SAResult &r) {
        m_saving = false;
        if (!r.ok) {
            const QString message = wasNew
                ? tr("An error occurred when trying to add the field “%1” via\n\n%2\n\nMySQL said: %3").arg(column.name, sql, r.errorMessage)
                : tr("An error occurred when trying to change the field “%1” via\n\n%2\n\nMySQL said: %3").arg(column.name, sql, r.errorMessage);
            const bool keep = SADialogs::confirm(this, wasNew ? tr("Error adding field") : tr("Error changing field"), message, tr("Edit row"), tr("Discard changes"));
            if (!keep) cancelEdit();
            else { m_fields->setCurrentCell(row, ColName); m_fields->setFocus(); }
            return;
        }
        m_editingRow = -1;
        m_editingNewRow = false;
        afterStructureChange();
    });
    return true;
}

void SATableStructureView::cancelEdit()
{
    if (m_editingRow < 0) return;
    const int row = m_editingRow;
    m_editingRow = -1;
    if (m_editingNewRow) {
        m_populating = true;
        ++m_generation;
        m_fields->removeRow(row);
        m_populating = false;
        m_editingNewRow = false;
    } else {
        populateColumns();
    }
}

void SATableStructureView::afterStructureChange()
{
    m_document->tableStructureChanged();
    reload();
}

void SATableStructureView::addField()
{
    if (m_type != SASchema::ObjectType::Table) return;
    if (m_editingRow >= 0 && !commitPendingEdit()) return;
    const int row = m_fields->rowCount();
    SASchema::Column c;
    c.name = tr("new_field");
    c.type = QStringLiteral("INT");
    c.nullable = SAPreferences::instance().boolFor(SAPreferences::NewFieldsAllowNulls);
    c.defaultIsNull = c.nullable;
    c.hasDefault = c.nullable;
    m_populating = true;
    m_fields->insertRow(row);
    setRowFromColumn(row, c);
    m_populating = false;
    m_editingRow = row;
    m_editingNewRow = true;
    m_editingOriginalName.clear();
    m_fields->setCurrentCell(row, ColName);
    m_fields->editItem(m_fields->item(row, ColName));
}

void SATableStructureView::removeField()
{
    if (m_type != SASchema::ObjectType::Table) return;
    const int row = m_fields->currentRow();
    if (row < 0) return;
    if (m_editingRow == row && m_editingNewRow) { cancelEdit(); return; }
    if (m_editingRow >= 0 && !commitPendingEdit()) return;
    if (row >= m_columns.size()) return;
    const QString name = m_columns.at(row).name;
    if (m_columns.size() == 1) { m_document->reportError(tr("Cannot remove field"), tr("A table needs at least one field. Delete the table instead.")); return; }
    if (!SADialogs::confirm(this, tr("Delete field “%1”?").arg(name), tr("Are you sure you want to delete the field “%1”? This action cannot be undone.").arg(name), tr("Delete"), QString(), true)) return;
    m_document->session()->query(SASchema::dropColumn(m_table, name), [this, name](const SAResult &r) {
        if (!r.ok) {
            if (r.errorNumber == 1553 || r.errorMessage.contains(QLatin1String("foreign key"), Qt::CaseInsensitive)) {
                m_document->reportError(tr("Cannot remove field"), tr("The field “%1” is part of a foreign key. Remove the relation first (Relations view).\n\nMySQL said: %2").arg(name, r.errorMessage));
            } else {
                m_document->reportError(tr("Error"), tr("Couldn't delete field “%1”.\n\nMySQL said: %2").arg(name, r.errorMessage));
            }
            return;
        }
        afterStructureChange();
    });
}

void SATableStructureView::addIndex()
{
    if (m_type != SASchema::ObjectType::Table || m_columns.isEmpty()) return;
    if (m_editingRow >= 0 && !commitPendingEdit()) return;
    const bool hasPrimary = !SASchema::primaryKeyColumns(m_columns).isEmpty();
    SAIndexDialog dialog(this, m_columns, hasPrimary, true, true);
    if (dialog.exec() != QDialog::Accepted) return;
    if (dialog.columns().isEmpty()) { m_document->reportError(tr("No columns"), tr("Please choose at least one column for the index.")); return; }
    const QString sql = SASchema::addIndex(m_table, dialog.indexType(), dialog.indexName(), dialog.columns(), dialog.subParts(), dialog.storageType());
    m_document->session()->query(sql, [this, sql](const SAResult &r) {
        if (!r.ok) { m_document->reportError(tr("Unable to add index"), tr("An error occurred while trying to add the index.\n\n%1\n\nMySQL said: %2").arg(sql, r.errorMessage)); return; }
        afterStructureChange();
    });
}

void SATableStructureView::removeIndex()
{
    if (m_type != SASchema::ObjectType::Table) return;
    const int row = m_indexes->currentRow();
    if (row < 0 || row >= m_indexList.size()) return;
    const QString name = m_indexList.at(row).name;
    if (!SADialogs::confirm(this, tr("Delete index “%1”?").arg(name), tr("Are you sure you want to delete the index “%1”? This action cannot be undone.").arg(name), tr("Delete"), QString(), true)) return;
    m_document->session()->query(SASchema::dropIndex(m_table, name), [this, name](const SAResult &r) {
        if (!r.ok) {
            m_document->reportError(tr("Error"), r.errorMessage.contains(QLatin1String("foreign key"), Qt::CaseInsensitive)
                ? tr("The index “%1” is needed by a foreign key constraint. Remove the relation first.\n\nMySQL said: %2").arg(name, r.errorMessage)
                : tr("Couldn't delete index “%1”.\n\nMySQL said: %2").arg(name, r.errorMessage));
            return;
        }
        afterStructureChange();
    });
}
