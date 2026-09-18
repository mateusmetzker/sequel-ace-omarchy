//
//  SATableRelationsView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATableRelationsView.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"
#include "SAIcons.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

SATableRelationsView::SATableRelationsView(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_message = new QLabel;
    m_message->setContentsMargins(8, 6, 8, 6);
    m_message->setVisible(false);
    layout->addWidget(m_message);
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Columns"), tr("Referenced table"), tr("Referenced columns"), tr("On update"), tr("On delete")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(6, 3, 6, 3);
    m_add = new QToolButton;
    m_add->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    m_add->setToolTip(tr("Add relation"));
    connect(m_add, &QToolButton::clicked, this, &SATableRelationsView::addRelation);
    m_remove = new QToolButton;
    m_remove->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_remove->setToolTip(tr("Remove relation"));
    connect(m_remove, &QToolButton::clicked, this, &SATableRelationsView::removeRelation);
    auto *refresh = new QToolButton;
    refresh->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    connect(refresh, &QToolButton::clicked, this, &SATableRelationsView::reload);
    for (QToolButton *b : {m_add, m_remove, refresh}) { b->setAutoRaise(true); buttons->addWidget(b); }
    buttons->addStretch();
    layout->addLayout(buttons);
}

void SATableRelationsView::clear()
{
    m_tableName.clear();
    m_keys.clear();
    m_table->setRowCount(0);
    m_message->setVisible(false);
}

void SATableRelationsView::loadTable(const QString &name, SASchema::ObjectType type)
{
    m_tableName = name;
    m_type = type;
    m_table->setRowCount(0);
    if (type != SASchema::ObjectType::Table) {
        m_message->setText(tr("Relations are only available for tables."));
        m_message->setVisible(true);
        m_add->setEnabled(false);
        m_remove->setEnabled(false);
        return;
    }
    const QString db = m_document->currentDatabase();
    m_document->session()->queryBatch({SASchema::foreignKeysFor(db, name, m_document->escaper()), SASchema::tableStatusLike(name, m_document->escaper()), SASchema::showFullColumns(name)},
        [this, name](const QVector<SAResult> &results) {
            if (name != m_tableName || results.size() < 3) return;
            m_keys = SASchema::parseForeignKeys(results[0]);
            const QString engine = results[1].ok && results[1].rowCount() ? results[1].stringAt(0, QStringLiteral("Engine")) : QString();
            m_isInnoDB = engine.compare(QLatin1String("InnoDB"), Qt::CaseInsensitive) == 0;
            m_columns = SASchema::parseColumns(results[2], m_document->session()->serverInfo().isMariaDB);
            m_message->setVisible(!m_isInnoDB);
            if (!m_isInnoDB) m_message->setText(tr("The table “%1” uses the %2 engine. Foreign key relations are only supported by InnoDB tables.").arg(name, engine.isEmpty() ? tr("unknown") : engine));
            m_add->setEnabled(m_isInnoDB);
            m_remove->setEnabled(m_isInnoDB);
            m_table->setRowCount(m_keys.size());
            for (int i = 0; i < m_keys.size(); ++i) {
                const SASchema::ForeignKey &k = m_keys.at(i);
                m_table->setItem(i, 0, new QTableWidgetItem(k.name));
                m_table->setItem(i, 1, new QTableWidgetItem(k.columns.join(QStringLiteral(", "))));
                m_table->setItem(i, 2, new QTableWidgetItem(k.referencedDatabase == m_document->currentDatabase() ? k.referencedTable : k.referencedDatabase + QLatin1Char('.') + k.referencedTable));
                m_table->setItem(i, 3, new QTableWidgetItem(k.referencedColumns.join(QStringLiteral(", "))));
                m_table->setItem(i, 4, new QTableWidgetItem(k.onUpdate));
                m_table->setItem(i, 5, new QTableWidgetItem(k.onDelete));
            }
            m_table->resizeColumnsToContents();
        }, SADatabaseSession::Silent);
}

void SATableRelationsView::reload()
{
    if (!m_tableName.isEmpty()) loadTable(m_tableName, m_type);
}

void SATableRelationsView::addRelation()
{
    if (!m_isInnoDB || m_columns.isEmpty()) return;
    const QString db = m_document->currentDatabase();
    m_document->session()->query(SASchema::innodbTablesInDatabase(db, m_document->escaper()), [this](const SAResult &r) {
        QStringList tables;
        for (int i = 0; i < r.rowCount(); ++i) tables << r.stringAt(i, 0);
        SARelationDialog dialog(this, m_columns, tables, [this](const QString &table, std::function<void(const QStringList &)> done) {
            m_document->session()->query(SASchema::showFullColumns(table), [done](const SAResult &c) {
                QStringList names;
                for (int i = 0; i < c.rowCount(); ++i) names << c.stringAt(i, QStringLiteral("Field"));
                done(names);
            }, SADatabaseSession::Silent);
        });
        if (dialog.exec() != QDialog::Accepted) return;
        if (dialog.columns().isEmpty() || dialog.referencedColumns().isEmpty()) {
            m_document->reportError(tr("Incomplete relation"), tr("Choose at least one local column and one referenced column."));
            return;
        }
        if (dialog.columns().size() != dialog.referencedColumns().size()) {
            m_document->reportError(tr("Incomplete relation"), tr("The number of local and referenced columns must match."));
            return;
        }
        const QString sql = SASchema::addForeignKey(m_tableName, dialog.constraintName(), dialog.columns(), QString(), dialog.referencedTable(),
                                                    dialog.referencedColumns(), dialog.onDelete(), dialog.onUpdate());
        m_document->session()->query(sql, [this, sql](const SAResult &res) {
            if (!res.ok) {
                QString detail;
                if (res.errorNumber == 1005 || res.errorNumber == 1215) detail = tr("\n\nForeign keys need an index on the referenced columns and matching column types.");
                m_document->reportError(tr("Error creating relation"), tr("The relation could not be created.\n\n%1\n\nMySQL said: %2%3").arg(sql, res.errorMessage, detail));
                return;
            }
            m_document->tableStructureChanged();
            reload();
        });
    }, SADatabaseSession::Silent);
}

void SATableRelationsView::removeRelation()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_keys.size()) return;
    const QString name = m_keys.at(row).name;
    if (!SADialogs::confirm(this, tr("Delete relation “%1”?").arg(name), tr("Are you sure you want to delete the relation “%1”? This action cannot be undone.").arg(name), tr("Delete"), QString(), true)) return;
    m_document->session()->query(SASchema::dropForeignKey(m_tableName, name), [this](const SAResult &r) {
        if (!r.ok) { m_document->reportError(tr("Error"), tr("Couldn't delete relation.\n\nMySQL said: %1").arg(r.errorMessage)); return; }
        m_document->tableStructureChanged();
        reload();
    });
}
