//
//  SAServerDialogs.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAServerDialogs.h"
#include "SADatabaseDocument.h"
#include "SADialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

// ---- variables ------------------------------------------------------------------

SAServerVariablesDialog::SAServerVariablesDialog(SADatabaseDocument *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Server Variables"));
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter variables"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &SAServerVariablesDialog::filter);
    m_table = new QTableWidget(0, 2);
    m_table->setHorizontalHeaderLabels({tr("Variable"), tr("Value")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, this, &SAServerVariablesDialog::reload);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
    resize(720, 560);
    reload();
}

void SAServerVariablesDialog::reload()
{
    m_document->session()->query(QStringLiteral("SHOW VARIABLES"), [this](const SAResult &r) {
        m_all.clear();
        for (int i = 0; i < r.rowCount(); ++i) m_all.append({r.stringAt(i, 0), r.stringAt(i, 1)});
        filter();
    });
}

void SAServerVariablesDialog::filter()
{
    const QString text = m_filter->text().trimmed();
    m_table->setRowCount(0);
    for (const auto &pair : m_all) {
        if (!text.isEmpty() && !pair.first.contains(text, Qt::CaseInsensitive) && !pair.second.contains(text, Qt::CaseInsensitive)) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(pair.first));
        m_table->setItem(row, 1, new QTableWidgetItem(pair.second));
    }
    m_table->resizeColumnToContents(0);
}

// ---- process list ----------------------------------------------------------------

SAProcessListDialog::SAProcessListDialog(SADatabaseDocument *document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setWindowTitle(tr("Server Processes"));
    m_table = new QTableWidget(0, 8);
    m_table->setHorizontalHeaderLabels({tr("Id"), tr("User"), tr("Host"), tr("Database"), tr("Command"), tr("Time"), tr("State"), tr("Info")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_showFull = new QCheckBox(tr("Show full process list"));
    m_autoRefresh = new QCheckBox(tr("Auto refresh every 10 s"));
    m_timer = new QTimer(this);
    m_timer->setInterval(10000);
    connect(m_timer, &QTimer::timeout, this, &SAProcessListDialog::reload);
    connect(m_autoRefresh, &QCheckBox::toggled, this, [this](bool on) { if (on) m_timer->start(); else m_timer->stop(); });
    connect(m_showFull, &QCheckBox::toggled, this, &SAProcessListDialog::reload);
    m_status = new QLabel;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, this, &SAProcessListDialog::reload);
    auto *killQuery = buttons->addButton(tr("Kill Query"), QDialogButtonBox::ActionRole);
    connect(killQuery, &QPushButton::clicked, this, [this]() { kill(false); });
    auto *killConnection = buttons->addButton(tr("Kill Connection"), QDialogButtonBox::ActionRole);
    connect(killConnection, &QPushButton::clicked, this, [this]() { kill(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *top = new QHBoxLayout;
    top->addWidget(m_showFull);
    top->addWidget(m_autoRefresh);
    top->addStretch();
    top->addWidget(m_status);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
    resize(960, 520);
    reload();
}

void SAProcessListDialog::reload()
{
    m_document->session()->query(m_showFull->isChecked() ? QStringLiteral("SHOW FULL PROCESSLIST") : QStringLiteral("SHOW PROCESSLIST"), [this](const SAResult &r) {
        if (!r.ok) { m_status->setText(r.errorMessage); return; }
        const QStringList wanted{QStringLiteral("Id"), QStringLiteral("User"), QStringLiteral("Host"), QStringLiteral("db"), QStringLiteral("Command"), QStringLiteral("Time"), QStringLiteral("State"), QStringLiteral("Info")};
        m_table->setRowCount(r.rowCount());
        for (int i = 0; i < r.rowCount(); ++i) {
            const QMap<QString, QString> row = r.rowAsMap(i);
            for (int c = 0; c < wanted.size(); ++c) {
                QString value = row.value(wanted.at(c));
                if (wanted.at(c) == QLatin1String("Id") && value.isEmpty()) value = row.value(QStringLiteral("ID"));
                if (wanted.at(c) == QLatin1String("db") && value.isEmpty()) value = row.value(QStringLiteral("Db"));
                auto *item = new QTableWidgetItem(value.simplified());
                if (wanted.at(c) == QLatin1String("Info")) item->setToolTip(value);
                m_table->setItem(i, c, item);
            }
        }
        m_table->resizeColumnsToContents();
        m_table->setColumnWidth(7, qMin(m_table->columnWidth(7), 400));
        m_status->setText(tr("%n process(es)", nullptr, r.rowCount()));
    }, SADatabaseSession::Silent);
}

void SAProcessListDialog::kill(bool connection)
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) return;
    const quint64 id = m_table->item(row, 0)->text().toULongLong();
    if (!SADialogs::confirm(this, connection ? tr("Kill connection %1?").arg(id) : tr("Kill query of connection %1?").arg(id),
                            connection ? tr("The connection will be closed and any running query aborted.") : tr("The currently running query of this connection will be aborted."),
                            tr("Kill"), QString(), true)) return;
    const bool tidb = m_document->session()->serverInfo().versionString.contains(QLatin1String("TiDB"), Qt::CaseInsensitive);
    m_document->session()->query(connection ? SASchema::killConnection(id, tidb) : SASchema::killQuery(id, tidb), [this](const SAResult &r) {
        if (!r.ok) m_document->reportError(tr("Error"), tr("Unable to kill.\n\nMySQL said: %1").arg(r.errorMessage));
        reload();
    });
}
