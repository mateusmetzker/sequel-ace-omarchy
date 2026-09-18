//
//  SADialogs.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SADialogs.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace SADialogs {

void warning(QWidget *parent, const QString &title, const QString &text, const QString &detail)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    box.setText(title);
    box.setInformativeText(text);
    if (!detail.isEmpty()) box.setDetailedText(detail);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void information(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(title);
    box.setText(title);
    box.setInformativeText(text);
    box.exec();
}

bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &primaryButton,
             const QString &cancelButton, bool destructive, const QString &detail)
{
    QMessageBox box(parent);
    box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(title);
    box.setInformativeText(text);
    if (!detail.isEmpty()) box.setDetailedText(detail);
    QPushButton *primary = box.addButton(primaryButton, destructive ? QMessageBox::DestructiveRole : QMessageBox::AcceptRole);
    QPushButton *cancel = box.addButton(cancelButton.isEmpty() ? QObject::tr("Cancel") : cancelButton, QMessageBox::RejectRole);
    box.setDefaultButton(destructive ? cancel : primary);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == primary;
}

int errorContinuation(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(title);
    box.setText(title);
    box.setInformativeText(text);
    QPushButton *runAll = box.addButton(QObject::tr("Run All"), QMessageBox::AcceptRole);
    QPushButton *cont = box.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
    QPushButton *stop = box.addButton(QObject::tr("Stop"), QMessageBox::RejectRole);
    box.setDefaultButton(stop);
    box.exec();
    if (box.clickedButton() == runAll) return 2;
    if (box.clickedButton() == cont) return 1;
    return 0;
}

QString askText(QWidget *parent, const QString &title, const QString &label, const QString &initial, bool *ok)
{
    return QInputDialog::getText(parent, title, label, QLineEdit::Normal, initial, ok);
}

QString askPassword(QWidget *parent, const QString &title, const QString &label, bool *ok)
{
    return QInputDialog::getText(parent, title, label, QLineEdit::Password, QString(), ok);
}

} // namespace SADialogs

// ---- SADatabaseDialog ------------------------------------------------------------

SADatabaseDialog::SADatabaseDialog(QWidget *parent, const SADialogs::CharsetCollation &charsets, const QString &existingName,
                                   const QString &currentCharset, const QString &currentCollation)
    : QDialog(parent), m_data(charsets)
{
    setWindowTitle(existingName.isEmpty() ? tr("Add Database") : tr("Alter Database"));
    auto *form = new QFormLayout;
    m_name = new QLineEdit(existingName);
    m_name->setReadOnly(!existingName.isEmpty());
    m_name->setPlaceholderText(tr("Database name"));
    m_charset = new QComboBox;
    m_charset->addItem(tr("Default (server)"), QString());
    for (const QString &cs : charsets.charsets) m_charset->addItem(cs, cs);
    m_collation = new QComboBox;
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Encoding:"), m_charset);
    form->addRow(tr("Collation:"), m_collation);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(existingName.isEmpty() ? tr("Add") : tr("Alter"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_charset, &QComboBox::currentIndexChanged, this, &SADatabaseDialog::charsetChanged);
    connect(m_name, &QLineEdit::textChanged, this, [buttons](const QString &t) { buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty()); });
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (!currentCharset.isEmpty()) m_charset->setCurrentIndex(qMax(0, m_charset->findData(currentCharset)));
    charsetChanged();
    if (!currentCollation.isEmpty()) m_collation->setCurrentIndex(qMax(0, m_collation->findData(currentCollation)));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_name->text().trimmed().isEmpty());
    resize(420, sizeHint().height());
}

void SADatabaseDialog::charsetChanged()
{
    const QString cs = m_charset->currentData().toString();
    m_collation->clear();
    m_collation->addItem(tr("Default"), QString());
    for (const QString &c : m_data.collations.value(cs)) m_collation->addItem(c, c);
    m_collation->setEnabled(!cs.isEmpty());
}

QString SADatabaseDialog::databaseName() const { return m_name->text().trimmed(); }
QString SADatabaseDialog::charset() const { return m_charset->currentData().toString(); }
QString SADatabaseDialog::collation() const { return m_collation->currentData().toString(); }

// ---- SATableDialog --------------------------------------------------------------------

SATableDialog::SATableDialog(QWidget *parent, const QStringList &engines, const QString &defaultEngine,
                             const SADialogs::CharsetCollation &charsets, const QString &defaultCharset)
    : QDialog(parent), m_data(charsets)
{
    setWindowTitle(tr("Add Table"));
    auto *form = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Table name"));
    m_engine = new QComboBox;
    m_engine->addItem(tr("Default (%1)").arg(defaultEngine.isEmpty() ? tr("server") : defaultEngine), QString());
    for (const QString &e : engines) m_engine->addItem(e, e);
    m_charset = new QComboBox;
    m_charset->addItem(tr("Default (%1)").arg(defaultCharset.isEmpty() ? tr("database") : defaultCharset), QString());
    for (const QString &cs : charsets.charsets) m_charset->addItem(cs, cs);
    m_collation = new QComboBox;
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Engine:"), m_engine);
    form->addRow(tr("Encoding:"), m_charset);
    form->addRow(tr("Collation:"), m_collation);
    auto *note = new QLabel(tr("The table is created with an auto-incrementing `id` primary key; add fields in the Structure view."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_charset, &QComboBox::currentIndexChanged, this, &SATableDialog::charsetChanged);
    connect(m_name, &QLineEdit::textChanged, this, [buttons](const QString &t) { buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty()); });
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    charsetChanged();
    resize(440, sizeHint().height());
}

void SATableDialog::charsetChanged()
{
    const QString cs = m_charset->currentData().toString();
    m_collation->clear();
    m_collation->addItem(tr("Default"), QString());
    for (const QString &c : m_data.collations.value(cs)) m_collation->addItem(c, c);
    m_collation->setEnabled(!cs.isEmpty());
}

QString SATableDialog::tableName() const { return m_name->text().trimmed(); }
QString SATableDialog::engine() const { return m_engine->currentData().toString(); }
QString SATableDialog::charset() const { return m_charset->currentData().toString(); }
QString SATableDialog::collation() const { return m_collation->currentData().toString(); }

// ---- SAIndexDialog --------------------------------------------------------------------

SAIndexDialog::SAIndexDialog(QWidget *parent, const QVector<SASchema::Column> &columns, bool hasPrimaryKey, bool supportsFulltext, bool supportsSpatial)
    : QDialog(parent), m_allColumns(columns)
{
    setWindowTitle(tr("Add Index"));
    auto *form = new QFormLayout;
    m_type = new QComboBox;
    m_type->addItem(tr("INDEX"), QStringLiteral("INDEX"));
    m_type->addItem(tr("UNIQUE"), QStringLiteral("UNIQUE"));
    if (!hasPrimaryKey) m_type->addItem(tr("PRIMARY KEY"), QStringLiteral("PRIMARY KEY"));
    if (supportsFulltext) m_type->addItem(tr("FULLTEXT"), QStringLiteral("FULLTEXT"));
    if (supportsSpatial) m_type->addItem(tr("SPATIAL"), QStringLiteral("SPATIAL"));
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Optional index name"));
    m_columns = new QListWidget;
    m_columns->setSelectionMode(QAbstractItemView::NoSelection);
    for (const SASchema::Column &c : columns) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(c.name, c.fullType), m_columns);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, c.name);
        item->setData(Qt::UserRole + 1, c.isBlobOrText() || c.typeGroup == QLatin1String("string"));
    }
    m_storage = new QComboBox;
    m_storage->addItem(tr("Default"), QString());
    m_storage->addItem(QStringLiteral("BTREE"), QStringLiteral("BTREE"));
    m_storage->addItem(QStringLiteral("HASH"), QStringLiteral("HASH"));
    form->addRow(tr("Type:"), m_type);
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Columns:"), m_columns);
    form->addRow(tr("Storage:"), m_storage);
    auto *hint = new QLabel(tr("TEXT and BLOB columns need a key length; add it after the column name as name(10)."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_type, &QComboBox::currentIndexChanged, this, &SAIndexDialog::typeChanged);
    // Allow editing "name(len)" in place for sub parts.
    for (int i = 0; i < m_columns->count(); ++i) m_columns->item(i)->setFlags(m_columns->item(i)->flags() | Qt::ItemIsEditable);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    typeChanged();
    resize(460, 420);
}

void SAIndexDialog::typeChanged()
{
    const QString type = indexType();
    m_name->setEnabled(type != QLatin1String("PRIMARY KEY"));
    m_storage->setEnabled(type == QLatin1String("INDEX") || type == QLatin1String("UNIQUE") || type == QLatin1String("PRIMARY KEY"));
}

QString SAIndexDialog::indexType() const { return m_type->currentData().toString(); }
QString SAIndexDialog::indexName() const { return m_name->text().trimmed(); }

QStringList SAIndexDialog::columns() const
{
    QStringList names;
    for (int i = 0; i < m_columns->count(); ++i) {
        const QListWidgetItem *item = m_columns->item(i);
        if (item->checkState() != Qt::Checked) continue;
        names << item->data(Qt::UserRole).toString();
    }
    return names;
}

QStringList SAIndexDialog::subParts() const
{
    QStringList parts;
    for (int i = 0; i < m_columns->count(); ++i) {
        const QListWidgetItem *item = m_columns->item(i);
        if (item->checkState() != Qt::Checked) continue;
        // An edited label of the form "name(10)  (type)" carries the key length.
        const QString text = item->text();
        const QString name = item->data(Qt::UserRole).toString();
        QString part;
        const int open = text.indexOf(QLatin1Char('('), name.size());
        if (text.startsWith(name) && open == name.size()) {
            const int close = text.indexOf(QLatin1Char(')'), open);
            if (close > open) part = text.mid(open + 1, close - open - 1).trimmed();
        }
        parts << part;
    }
    return parts;
}

QString SAIndexDialog::storageType() const { return m_storage->currentData().toString(); }

// ---- SARelationDialog --------------------------------------------------------------------

SARelationDialog::SARelationDialog(QWidget *parent, const QVector<SASchema::Column> &columns, const QStringList &referenceTables,
                                   std::function<void(const QString &, std::function<void(const QStringList &)>)> columnsLoader)
    : QDialog(parent), m_columnsLoader(std::move(columnsLoader))
{
    setWindowTitle(tr("Add Relation"));
    auto *form = new QFormLayout;
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("Optional constraint name"));
    m_columns = new QListWidget;
    for (const SASchema::Column &c : columns) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(c.name, c.fullType), m_columns);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, c.name);
    }
    m_refTable = new QComboBox;
    m_refTable->addItems(referenceTables);
    m_refColumns = new QListWidget;
    m_onDelete = new QComboBox;
    m_onUpdate = new QComboBox;
    for (QComboBox *box : {m_onDelete, m_onUpdate}) {
        box->addItem(tr("Default (RESTRICT)"), QString());
        for (const char *action : {"RESTRICT", "CASCADE", "SET NULL", "NO ACTION"}) box->addItem(QLatin1String(action), QLatin1String(action));
    }
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Columns:"), m_columns);
    form->addRow(tr("References table:"), m_refTable);
    form->addRow(tr("References columns:"), m_refColumns);
    form->addRow(tr("On delete:"), m_onDelete);
    form->addRow(tr("On update:"), m_onUpdate);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_refTable, &QComboBox::currentIndexChanged, this, &SARelationDialog::referenceTableChanged);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    referenceTableChanged();
    resize(480, 520);
}

void SARelationDialog::referenceTableChanged()
{
    m_refColumns->clear();
    const QString table = m_refTable->currentText();
    if (table.isEmpty() || !m_columnsLoader) return;
    QPointer<QListWidget> list(m_refColumns);
    m_columnsLoader(table, [list](const QStringList &names) {
        if (!list) return;
        list->clear();
        for (const QString &n : names) {
            auto *item = new QListWidgetItem(n, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    });
}

QString SARelationDialog::constraintName() const { return m_name->text().trimmed(); }
QStringList SARelationDialog::columns() const
{
    QStringList names;
    for (int i = 0; i < m_columns->count(); ++i)
        if (m_columns->item(i)->checkState() == Qt::Checked) names << m_columns->item(i)->data(Qt::UserRole).toString();
    return names;
}
QString SARelationDialog::referencedTable() const { return m_refTable->currentText(); }
QStringList SARelationDialog::referencedColumns() const
{
    QStringList names;
    for (int i = 0; i < m_refColumns->count(); ++i)
        if (m_refColumns->item(i)->checkState() == Qt::Checked) names << m_refColumns->item(i)->text();
    return names;
}
QString SARelationDialog::onDelete() const { return m_onDelete->currentData().toString(); }
QString SARelationDialog::onUpdate() const { return m_onUpdate->currentData().toString(); }

// ---- SATriggerDialog --------------------------------------------------------------------

SATriggerDialog::SATriggerDialog(QWidget *parent, const QString &table, const SASchema::Trigger *existing)
    : QDialog(parent), m_table(table)
{
    setWindowTitle(existing ? tr("Edit Trigger") : tr("Add Trigger"));
    auto *form = new QFormLayout;
    m_name = new QLineEdit(existing ? existing->name : QString());
    m_timing = new QComboBox;
    m_timing->addItems({QStringLiteral("BEFORE"), QStringLiteral("AFTER")});
    m_event = new QComboBox;
    m_event->addItems({QStringLiteral("INSERT"), QStringLiteral("UPDATE"), QStringLiteral("DELETE")});
    m_statement = new QPlainTextEdit(existing ? existing->statement : QString());
    m_statement->setPlaceholderText(tr("SET NEW.updated_at = NOW()"));
    if (existing) {
        m_timing->setCurrentText(existing->timing.toUpper());
        m_event->setCurrentText(existing->event.toUpper());
    }
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Table:"), new QLabel(table));
    form->addRow(tr("Action time:"), m_timing);
    form->addRow(tr("Event:"), m_event);
    form->addRow(tr("Statement:"), m_statement);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(existing ? tr("Save") : tr("Add"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(560, 420);
}

SASchema::Trigger SATriggerDialog::trigger() const
{
    SASchema::Trigger t;
    t.name = m_name->text().trimmed();
    t.table = m_table;
    t.timing = m_timing->currentText();
    t.event = m_event->currentText();
    t.statement = m_statement->toPlainText().trimmed();
    return t;
}

// ---- SAGotoDatabaseDialog -------------------------------------------------------------------

SAGotoDatabaseDialog::SAGotoDatabaseDialog(QWidget *parent, const QStringList &databases, const QString &current)
    : QDialog(parent), m_all(databases)
{
    setWindowTitle(tr("Go to Database"));
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Type to filter…"));
    m_filter->setClearButtonEnabled(true);
    m_list = new QListWidget;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Go"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged, this, &SAGotoDatabaseDialog::filter);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_list);
    layout->addWidget(buttons);
    filter(QString());
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text() == current) m_list->setCurrentRow(i);
    resize(380, 460);
}

void SAGotoDatabaseDialog::filter(const QString &text)
{
    m_list->clear();
    for (const QString &db : m_all)
        if (text.isEmpty() || db.contains(text, Qt::CaseInsensitive)) m_list->addItem(db);
    if (m_list->count() && !m_list->currentItem()) m_list->setCurrentRow(0);
}

QString SAGotoDatabaseDialog::selectedDatabase() const
{
    return m_list->currentItem() ? m_list->currentItem()->text() : QString();
}

// ---- SABusyDialog ---------------------------------------------------------------------------

SABusyDialog::SABusyDialog(QWidget *parent, const QString &title, const QString &text, bool cancellable)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint)
{
    setWindowTitle(title);
    setModal(true);
    m_label = new QLabel(text);
    m_label->setWordWrap(true);
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_label);
    layout->addWidget(bar);
    if (cancellable) {
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::rejected, this, &SABusyDialog::reject);
        layout->addWidget(buttons);
    }
    setMinimumWidth(360);
}

void SABusyDialog::setText(const QString &text) { m_label->setText(text); }

void SABusyDialog::finish()
{
    m_finished = true;
    QDialog::done(QDialog::Accepted);   // honours WA_DeleteOnClose
}

void SABusyDialog::closeEvent(QCloseEvent *event)
{
    if (m_finished) {
        QDialog::closeEvent(event);
        return;
    }
    // Window-manager close counts as Cancel; keep the dialog up until the task reacts.
    event->ignore();
    reject();
}

void SABusyDialog::reject()
{
    if (m_finished || m_cancelled) return;
    m_cancelled = true;
    Q_EMIT cancelRequested();
}
