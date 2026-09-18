//
//  SATableTriggersView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATableTriggersView.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAIcons.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

SATableTriggersView::SATableTriggersView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_message = new QLabel;
    m_message->setContentsMargins(8, 6, 8, 6);
    m_message->setVisible(false);
    layout->addWidget(m_message);
    m_table = new QTableWidget(0, 7);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Event"), tr("Timing"), tr("Statement"), tr("Created"), tr("SQL mode"), tr("Definer")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editTrigger(); });
    layout->addWidget(m_table, 1);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(6, 3, 6, 3);
    m_add = new QToolButton;
    m_add->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    m_add->setToolTip(tr("Add trigger"));
    connect(m_add, &QToolButton::clicked, this, &SATableTriggersView::addTrigger);
    m_remove = new QToolButton;
    m_remove->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_remove->setToolTip(tr("Remove trigger"));
    connect(m_remove, &QToolButton::clicked, this, &SATableTriggersView::removeTrigger);
    auto *edit = new QToolButton;
    edit->setIcon(SAIcons::icon(SAIcons::Glyph::Edit));
    edit->setToolTip(tr("Edit trigger"));
    connect(edit, &QToolButton::clicked, this, &SATableTriggersView::editTrigger);
    auto *refresh = new QToolButton;
    refresh->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    connect(refresh, &QToolButton::clicked, this, &SATableTriggersView::reload);
    for (QToolButton *b : {m_add, m_remove, edit, refresh}) { b->setAutoRaise(true); buttons->addWidget(b); }
    buttons->addStretch();
    layout->addLayout(buttons);
}

void SATableTriggersView::clear()
{
    m_tableName.clear();
    m_triggers.clear();
    m_table->setRowCount(0);
    m_message->setVisible(false);
}

void SATableTriggersView::loadTable(const QString &name, SASchema::ObjectType type)
{
    m_tableName = name;
    m_type = type;
    m_table->setRowCount(0);
    const bool isTable = type == SASchema::ObjectType::Table;
    m_add->setEnabled(isTable);
    m_remove->setEnabled(isTable);
    m_message->setVisible(!isTable);
    if (!isTable) { m_message->setText(tr("Triggers are only available for tables.")); return; }
    m_document->session()->query(SASchema::triggersFor(m_document->currentDatabase(), name, m_document->escaper()), [this, name](const SAResult &r) {
        if (name != m_tableName) return;
        if (!r.ok) { m_document->reportError(tr("Error"), tr("The triggers could not be loaded.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        m_triggers = SASchema::parseTriggers(r);
        m_table->setRowCount(m_triggers.size());
        for (int i = 0; i < m_triggers.size(); ++i) {
            const SASchema::Trigger &t = m_triggers.at(i);
            m_table->setItem(i, 0, new QTableWidgetItem(t.name));
            m_table->setItem(i, 1, new QTableWidgetItem(t.event));
            m_table->setItem(i, 2, new QTableWidgetItem(t.timing));
            auto *statement = new QTableWidgetItem(t.statement.simplified());
            statement->setToolTip(t.statement);
            m_table->setItem(i, 3, statement);
            m_table->setItem(i, 4, new QTableWidgetItem(t.created));
            m_table->setItem(i, 5, new QTableWidgetItem(t.sqlMode));
            m_table->setItem(i, 6, new QTableWidgetItem(t.definer));
        }
        m_table->resizeColumnsToContents();
        m_table->setColumnWidth(3, qMin(m_table->columnWidth(3), 420));
    }, SADatabaseSession::Silent);
}

void SATableTriggersView::reload()
{
    if (!m_tableName.isEmpty()) loadTable(m_tableName, m_type);
}

void SATableTriggersView::addTrigger()
{
    if (m_type != SASchema::ObjectType::Table) return;
    SATriggerDialog dialog(this, m_tableName);
    if (dialog.exec() != QDialog::Accepted) return;
    const SASchema::Trigger t = dialog.trigger();
    if (t.name.isEmpty() || t.statement.isEmpty()) { m_document->reportError(tr("Incomplete trigger"), tr("A trigger needs a name and a statement.")); return; }
    const QString sql = SASchema::createTrigger(t.name, t.timing, t.event, t.table, t.statement);
    m_document->session()->query(sql, [this, sql](const SAResult &r) {
        if (!r.ok) { m_document->reportError(tr("Error creating trigger"), tr("The trigger could not be created.\n\n%1\n\nMySQL said: %2").arg(sql, r.errorMessage)); return; }
        reload();
    });
}

void SATableTriggersView::editTrigger()
{
    const int row = m_table->currentRow();
    if (m_type != SASchema::ObjectType::Table || row < 0 || row >= m_triggers.size()) return;
    const SASchema::Trigger existing = m_triggers.at(row);
    SATriggerDialog dialog(this, m_tableName, &existing);
    if (dialog.exec() != QDialog::Accepted) return;
    const SASchema::Trigger t = dialog.trigger();
    if (t.name.isEmpty() || t.statement.isEmpty()) return;
    // MySQL has no ALTER TRIGGER: drop and re-create, like the macOS app.
    const QStringList statements{SASchema::dropTrigger(m_document->currentDatabase(), existing.name), SASchema::createTrigger(t.name, t.timing, t.event, t.table, t.statement)};
    m_document->session()->queryBatch(statements, [this, statements](const QVector<SAResult> &results) {
        for (int i = 0; i < results.size(); ++i)
            if (!results[i].ok) { m_document->reportError(tr("Error editing trigger"), tr("%1\n\nMySQL said: %2").arg(statements.at(i), results[i].errorMessage)); break; }
        reload();
    }, SADatabaseSession::NoFlags, true);
}

void SATableTriggersView::removeTrigger()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_triggers.size()) return;
    const QString name = m_triggers.at(row).name;
    if (!SADialogs::confirm(this, tr("Delete trigger “%1”?").arg(name), tr("Are you sure you want to delete the trigger “%1”? This action cannot be undone.").arg(name), tr("Delete"), QString(), true)) return;
    m_document->session()->query(SASchema::dropTrigger(m_document->currentDatabase(), name), [this](const SAResult &r) {
        if (!r.ok) { m_document->reportError(tr("Error"), tr("Couldn't delete trigger.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        reload();
    });
}
