//
//  SATablesList.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SATablesList.h"
#include "SADatabaseDocument.h"
#include "SAIcons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QDebug>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int TypeRole = Qt::UserRole + 1;
constexpr int NameRole = Qt::UserRole + 2;
constexpr int HeaderRole = Qt::UserRole + 3;

SAIcons::Glyph glyphFor(SASchema::ObjectType type)
{
    switch (type) {
    case SASchema::ObjectType::View: return SAIcons::Glyph::View;
    case SASchema::ObjectType::Procedure: return SAIcons::Glyph::Procedure;
    case SASchema::ObjectType::Function: return SAIcons::Glyph::Function;
    case SASchema::ObjectType::Event: return SAIcons::Glyph::Event;
    default: return SAIcons::Glyph::Table;
    }
}
}

SATablesList::SATablesList(SADatabaseDocument *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    setObjectName(QStringLiteral("tablesListPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(4);

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &SATablesList::rebuild);
    layout->addWidget(m_filter);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setIconSize(QSize(16, 16));
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { if (!m_updating) Q_EMIT selectionChanged(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &SATablesList::showContextMenu);
    layout->addWidget(m_list, 1);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setContentsMargins(8, 4, 8, 4);
    m_info->setObjectName(QStringLiteral("tablesListInfo"));
    m_info->setVisible(false);
    // A long comment must not push the add/remove/refresh row below the
    // window; scroll the info panel instead of growing past this.
    m_info->setMaximumHeight(110);
    auto *infoScroll = new QScrollArea;
    infoScroll->setWidget(m_info);
    infoScroll->setWidgetResizable(true);
    infoScroll->setFrameShape(QFrame::NoFrame);
    infoScroll->setMaximumHeight(110);
    infoScroll->setVisible(false);
    infoScroll->setObjectName(QStringLiteral("tablesListInfoScroll"));
    m_infoScroll = infoScroll;
    layout->addWidget(infoScroll);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(4, 0, 4, 4);
    auto *add = new QToolButton;
    add->setIcon(SAIcons::icon(SAIcons::Glyph::Add));
    add->setToolTip(tr("Add table"));
    connect(add, &QToolButton::clicked, this, &SATablesList::addRequested);
    m_removeButton = new QToolButton;
    m_removeButton->setIcon(SAIcons::icon(SAIcons::Glyph::Remove));
    m_removeButton->setToolTip(tr("Delete selected"));
    connect(m_removeButton, &QToolButton::clicked, this, &SATablesList::removeRequested);
    auto *refresh = new QToolButton;
    refresh->setIcon(SAIcons::icon(SAIcons::Glyph::Refresh));
    refresh->setToolTip(tr("Refresh tables"));
    connect(refresh, &QToolButton::clicked, this, &SATablesList::refreshRequested);
    auto *gear = new QToolButton;
    gear->setIcon(SAIcons::icon(SAIcons::Glyph::Gear));
    gear->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(gear);
    menu->addAction(tr("Rename…"), this, &SATablesList::renameRequested);
    menu->addAction(tr("Duplicate…"), this, &SATablesList::duplicateRequested);
    menu->addAction(tr("Truncate…"), this, &SATablesList::truncateRequested);
    menu->addSeparator();
    menu->addAction(tr("Copy Create Syntax"), this, &SATablesList::copyCreateRequested);
    menu->addAction(tr("Show Create Syntax…"), this, &SATablesList::showCreateRequested);
    menu->addSeparator();
    menu->addAction(tr("Export…"), this, &SATablesList::exportRequested);
    gear->setMenu(menu);
    for (QToolButton *b : {add, m_removeButton, refresh, gear}) b->setAutoRaise(true);
    buttons->addWidget(add);
    buttons->addWidget(m_removeButton);
    buttons->addWidget(refresh);
    buttons->addStretch();
    buttons->addWidget(gear);
    layout->addLayout(buttons);
    setMinimumWidth(160);
}

QListWidgetItem *SATablesList::headerItem(const QString &text)
{
    auto *item = new QListWidgetItem(text);
    item->setFlags(Qt::NoItemFlags);
    QFont f = item->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.85);
    item->setFont(f);
    item->setForeground(palette().color(QPalette::PlaceholderText));
    item->setData(HeaderRole, true);
    return item;
}

void SATablesList::setEntries(const QVector<SASchema::ObjectEntry> &entries)
{
    m_entries = entries;
    rebuild();
}

void SATablesList::rebuild()
{
    const QString previous = selectedName();
    m_updating = true;
    m_list->clear();
    const QString filter = m_filter->text().trimmed();
    bool hasViews = false;
    QVector<SASchema::ObjectEntry> tables, routines;
    for (const SASchema::ObjectEntry &e : m_entries) {
        if (!filter.isEmpty() && !e.name.contains(filter, Qt::CaseInsensitive)) continue;
        if (e.type == SASchema::ObjectType::Table || e.type == SASchema::ObjectType::View) {
            tables << e;
            if (e.type == SASchema::ObjectType::View) hasViews = true;
        } else {
            routines << e;
        }
    }
    m_list->addItem(headerItem(hasViews ? tr("TABLES & VIEWS") : tr("TABLES")));
    auto addEntry = [this](const SASchema::ObjectEntry &e) {
        auto *item = new QListWidgetItem(SAIcons::icon(glyphFor(e.type)), e.name);
        item->setData(TypeRole, int(e.type));
        item->setData(NameRole, e.name);
        if (!e.comment.isEmpty()) item->setToolTip(e.comment);
        m_list->addItem(item);
    };
    for (const SASchema::ObjectEntry &e : tables) addEntry(e);
    if (!routines.isEmpty()) {
        m_list->addItem(headerItem(tr("PROCS & FUNCS")));
        for (const SASchema::ObjectEntry &e : routines) addEntry(e);
    }
    m_updating = false;
    if (!previous.isEmpty()) selectName(previous);
    m_removeButton->setEnabled(!selectedName().isEmpty());
}

void SATablesList::clear()
{
    m_entries.clear();
    m_updating = true;
    m_list->clear();
    m_updating = false;
    m_info->clear();
    m_infoScroll->setVisible(false);
}

void SATablesList::selectName(const QString &name)
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->data(NameRole).toString() == name) {
            if (!item->isSelected() || m_list->selectedItems().size() != 1) {
                m_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            }
            m_list->scrollToItem(item);
            m_removeButton->setEnabled(true);
            return;
        }
    }
    m_list->clearSelection();
    m_removeButton->setEnabled(false);
}

QString SATablesList::selectedName() const
{
    const QList<QListWidgetItem *> items = m_list->selectedItems();
    for (QListWidgetItem *item : items)
        if (!item->data(HeaderRole).toBool()) return item->data(NameRole).toString();
    return QString();
}

SASchema::ObjectType SATablesList::selectedType() const
{
    const QList<QListWidgetItem *> items = m_list->selectedItems();
    for (QListWidgetItem *item : items)
        if (!item->data(HeaderRole).toBool()) return static_cast<SASchema::ObjectType>(item->data(TypeRole).toInt());
    return SASchema::ObjectType::None;
}

QVector<SASchema::ObjectEntry> SATablesList::selectedEntries() const
{
    QVector<SASchema::ObjectEntry> entries;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (!item->isSelected() || item->data(HeaderRole).toBool()) continue;
        SASchema::ObjectEntry e;
        e.name = item->data(NameRole).toString();
        e.type = static_cast<SASchema::ObjectType>(item->data(TypeRole).toInt());
        entries << e;
    }
    return entries;
}

void SATablesList::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void SATablesList::setInfoHtml(const QString &html)
{
    m_info->setText(html);
    m_infoScroll->setVisible(!html.isEmpty());
}

void SATablesList::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (item && !item->isSelected() && !item->data(HeaderRole).toBool()) m_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    const QVector<SASchema::ObjectEntry> selected = selectedEntries();
    QMenu menu(this);
    menu.addAction(tr("Add Table…"), this, &SATablesList::addRequested);
    if (!selected.isEmpty()) {
        const bool single = selected.size() == 1;
        const bool tablesOnly = std::all_of(selected.cbegin(), selected.cend(), [](const SASchema::ObjectEntry &e) { return e.type == SASchema::ObjectType::Table; });
        menu.addSeparator();
        if (single) {
            menu.addAction(tr("Rename…"), this, &SATablesList::renameRequested);
            if (selected.first().type == SASchema::ObjectType::Table) menu.addAction(tr("Duplicate…"), this, &SATablesList::duplicateRequested);
        }
        if (tablesOnly) menu.addAction(tr("Truncate…"), this, &SATablesList::truncateRequested);
        menu.addAction(tr("Delete…"), this, &SATablesList::removeRequested);
        menu.addSeparator();
        if (single) {
            menu.addAction(tr("Copy Create Syntax"), this, &SATablesList::copyCreateRequested);
            menu.addAction(tr("Show Create Syntax…"), this, &SATablesList::showCreateRequested);
        }
        if (tablesOnly) {
            QMenu *maintenance = menu.addMenu(tr("Maintenance"));
            for (const char *cmd : {"Check", "Repair", "Analyze", "Optimize", "Flush", "Checksum"}) {
                const QString command = QLatin1String(cmd);
                maintenance->addAction(tr("%1 Table").arg(command), this, [this, command]() { Q_EMIT maintenanceRequested(command.toUpper()); });
            }
        }
        menu.addSeparator();
        menu.addAction(tr("Export…"), this, &SATablesList::exportRequested);
    }
    menu.addSeparator();
    menu.addAction(tr("Refresh"), this, &SATablesList::refreshRequested);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
