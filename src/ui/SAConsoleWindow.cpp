//
//  SAConsoleWindow.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAConsoleWindow.h"
#include "SAOmarchyTheme.h"
#include "SAIcons.h"
#include "SAPreferences.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>
#include <QScrollBar>
#include <algorithm>

namespace {
constexpr int IsErrorRole = Qt::UserRole + 1;
constexpr int IsSelectRole = Qt::UserRole + 2;

class ConsoleFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool showSelects = true;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex message = sourceModel()->index(row, 3, parent);
        if (!showSelects && sourceModel()->data(message, IsSelectRole).toBool() && !sourceModel()->data(message, IsErrorRole).toBool()) return false;
        const QRegularExpression re = filterRegularExpression();
        if (re.pattern().isEmpty()) return true;
        for (int c = 0; c < 4; ++c)
            if (sourceModel()->data(sourceModel()->index(row, c, parent)).toString().contains(re)) return true;
        return false;
    }
};
}

SAConsoleWindow *SAConsoleWindow::shared()
{
    static SAConsoleWindow *instance = new SAConsoleWindow;
    return instance;
}

SAConsoleWindow::SAConsoleWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Console"));
    setWindowIcon(SAIcons::icon(SAIcons::Glyph::Console));
    resize(900, 360);

    m_model = new QStandardItemModel(0, 4, this);
    m_model->setHorizontalHeaderLabels({tr("Time"), tr("Connection"), tr("Database"), tr("Message")});
    auto *proxy = new ConsoleFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 160);
    m_table->setColumnWidth(2, 140);
    auto *copy = new QAction(this);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QAction::triggered, this, &SAConsoleWindow::copySelection);
    m_table->addAction(copy);

    SAPreferences &prefs = SAPreferences::instance();
    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &SAConsoleWindow::applyFilters);
    top->addWidget(m_filter, 1);
    m_showSelects = new QCheckBox(tr("Show SELECT/SHOW statements"));
    m_showSelects->setChecked(prefs.boolFor(SAPreferences::ConsoleShowSelectsAndShows));
    m_showTimestamps = new QCheckBox(tr("Timestamps"));
    m_showTimestamps->setChecked(prefs.boolFor(SAPreferences::ConsoleShowTimestamps));
    m_showConnections = new QCheckBox(tr("Connections"));
    m_showConnections->setChecked(prefs.boolFor(SAPreferences::ConsoleShowConnections));
    m_showDatabases = new QCheckBox(tr("Databases"));
    m_showDatabases->setChecked(prefs.boolFor(SAPreferences::ConsoleShowDatabases));
    for (QCheckBox *box : {m_showSelects, m_showTimestamps, m_showConnections, m_showDatabases}) {
        connect(box, &QCheckBox::toggled, this, &SAConsoleWindow::applyFilters);
        top->addWidget(box);
    }

    auto *bottom = new QHBoxLayout;
    auto *clearButton = new QPushButton(tr("Clear"));
    connect(clearButton, &QPushButton::clicked, this, &SAConsoleWindow::clear);
    auto *saveButton = new QPushButton(tr("Save As…"));
    connect(saveButton, &QPushButton::clicked, this, &SAConsoleWindow::saveAs);
    bottom->addWidget(clearButton);
    bottom->addWidget(saveButton);
    bottom->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);
    applyFilters();
}

void SAConsoleWindow::applyFilters()
{
    static_cast<ConsoleFilter *>(m_proxy)->showSelects = m_showSelects->isChecked();
    m_proxy->setFilterFixedString(m_filter->text());
    m_proxy->invalidate();
    m_table->setColumnHidden(0, !m_showTimestamps->isChecked());
    m_table->setColumnHidden(1, !m_showConnections->isChecked());
    m_table->setColumnHidden(2, !m_showDatabases->isChecked());
    SAPreferences &prefs = SAPreferences::instance();
    prefs.set(SAPreferences::ConsoleShowSelectsAndShows, m_showSelects->isChecked());
    prefs.set(SAPreferences::ConsoleShowTimestamps, m_showTimestamps->isChecked());
    prefs.set(SAPreferences::ConsoleShowConnections, m_showConnections->isChecked());
    prefs.set(SAPreferences::ConsoleShowDatabases, m_showDatabases->isChecked());
}

void SAConsoleWindow::addMessage(const QString &connection, const QString &database, const QString &message, bool isError, double seconds)
{
    SAPreferences &prefs = SAPreferences::instance();
    if (!prefs.boolFor(SAPreferences::ConsoleEnableLogging) && !isError) return;
    if (isError && !prefs.boolFor(SAPreferences::ConsoleEnableErrorLogging)) return;
    static const QRegularExpression selectLike(QStringLiteral("^\\s*(select|show|describe|explain)\\b"), QRegularExpression::CaseInsensitiveOption);
    const bool scrollAtEnd = m_table->verticalScrollBar()->value() >= m_table->verticalScrollBar()->maximum() - 2;
    QList<QStandardItem *> row;
    row << new QStandardItem(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    row << new QStandardItem(connection);
    row << new QStandardItem(database);
    auto *text = new QStandardItem(message.simplified().left(2000));
    text->setToolTip(message.size() > 4000 ? message.left(4000) + QStringLiteral("…") : message);
    text->setData(isError, IsErrorRole);
    text->setData(selectLike.match(message).hasMatch(), IsSelectRole);
    row << text;
    if (isError) for (QStandardItem *item : row) item->setForeground(SAOmarchyTheme::current().red);
    if (seconds > 0) row.first()->setToolTip(tr("%1 ms").arg(QString::number(seconds * 1000.0, 'f', 2)));
    m_model->appendRow(row);
    while (m_model->rowCount() > 5000) m_model->removeRow(0);
    if (scrollAtEnd) m_table->scrollToBottom();
}

void SAConsoleWindow::clear()
{
    m_model->removeRows(0, m_model->rowCount());
}

void SAConsoleWindow::copySelection()
{
    QStringList lines;
    QSet<int> rows;
    for (const QModelIndex &index : m_table->selectionModel()->selectedIndexes()) rows.insert(index.row());
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end());
    for (int r : sorted) {
        QStringList parts;
        if (m_showTimestamps->isChecked()) parts << m_proxy->index(r, 0).data().toString();
        if (m_showConnections->isChecked()) parts << m_proxy->index(r, 1).data().toString();
        if (m_showDatabases->isChecked()) parts << m_proxy->index(r, 2).data().toString();
        const QString prefix = parts.isEmpty() ? QString() : QStringLiteral("/* %1 */ ").arg(parts.join(QLatin1Char(' ')));
        lines << prefix + m_proxy->index(r, 3).data(Qt::ToolTipRole).toString();
    }
    if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void SAConsoleWindow::saveAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Console"), QDir::homePath() + QStringLiteral("/sequel-ace-console.sql"), tr("SQL files (*.sql);;Text files (*.txt)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const QString line = QStringLiteral("/* %1 %2 %3 */ %4\n").arg(m_proxy->index(r, 0).data().toString(), m_proxy->index(r, 1).data().toString(),
                                                                       m_proxy->index(r, 2).data().toString(), m_proxy->index(r, 3).data(Qt::ToolTipRole).toString());
        file.write(line.toUtf8());
    }
}

void SAConsoleWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}
