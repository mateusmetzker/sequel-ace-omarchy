//
//  SAResultTableView.cpp
//  Sequel Ace (Linux port)
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#include "SAResultTableView.h"
#include "SAResultModel.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionHeader>
#include <algorithm>

// ---- header -------------------------------------------------------------------

SAResultHeaderView::SAResultHeaderView(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setSectionsClickable(true);
    setHighlightSections(false);
    setStretchLastSection(false);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setSectionsMovable(false);
    setMinimumSectionSize(40);
    setDefaultSectionSize(140);
}

void SAResultHeaderView::setShowTypes(bool show)
{
    m_showTypes = show;
    updateGeometry();
    viewport()->update();
}

QSize SAResultHeaderView::sizeHint() const
{
    QSize size = QHeaderView::sizeHint();
    if (m_showTypes) size.setHeight(int(size.height() * 1.75));
    return size;
}

void SAResultHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    if (!rect.isValid() || !model()) return;
    painter->save();
    QStyleOptionHeader option;
    initStyleOption(&option);
    option.rect = rect;
    option.section = logicalIndex;
    option.text = QString();
    const int sortState = model()->headerData(logicalIndex, Qt::Horizontal, Qt::UserRole + 1).toInt();
    if (sortState >= 0) {
        option.sortIndicator = sortState == int(Qt::AscendingOrder) ? QStyleOptionHeader::SortDown : QStyleOptionHeader::SortUp;
    }
    style()->drawControl(QStyle::CE_Header, &option, painter, this);
    painter->restore();

    const QString name = model()->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
    const QString type = model()->headerData(logicalIndex, Qt::Horizontal, Qt::UserRole).toString();
    QRect textRect = rect.adjusted(6, 2, -(sortState >= 0 ? 18 : 6), -2);
    painter->save();
    QFont bold = font();
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(palette().color(QPalette::ButtonText));
    if (m_showTypes && !type.isEmpty()) {
        const int half = textRect.height() / 2;
        painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), half), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(name, Qt::ElideRight, textRect.width()));
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        painter->setFont(small);
        painter->setPen(palette().color(QPalette::Disabled, QPalette::ButtonText));
        painter->drawText(QRect(textRect.left(), textRect.top() + half, textRect.width(), textRect.height() - half), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(small).elidedText(type, Qt::ElideRight, textRect.width()));
    } else {
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(name, Qt::ElideRight, textRect.width()));
    }
    painter->restore();
}

// ---- view -----------------------------------------------------------------------

SAResultTableView::SAResultTableView(QWidget *parent)
    : QTableView(parent)
{
    m_header = new SAResultHeaderView(this);
    setHorizontalHeader(m_header);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setMinimumWidth(36);
    setShowGrid(true);
    setGridStyle(Qt::SolidLine);
    setCornerButtonEnabled(false);
    setSortingEnabled(false);   // server-side sorting handled by the owner
}

void SAResultTableView::setResultModel(SAResultModel *model)
{
    m_model = model;
    setModel(model);
}

void SAResultTableView::setShowColumnTypes(bool show) { m_header->setShowTypes(show); }

void SAResultTableView::setVerticalGridlines(bool show)
{
    setStyleSheet(show ? QString() : QStringLiteral("QTableView { gridline-color: transparent; }"));
}

QList<int> SAResultTableView::selectedRows() const
{
    QSet<int> rows;
    for (const QModelIndex &index : selectionModel()->selectedIndexes()) rows.insert(index.row());
    QList<int> list = rows.values();
    std::sort(list.begin(), list.end());
    return list;
}

QString SAResultTableView::selectionAsText(bool includeHeaders) const
{
    if (!m_model) return QString();
    const QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) return QString();
    QSet<int> rowSet, colSet;
    for (const QModelIndex &i : indexes) { rowSet.insert(i.row()); colSet.insert(i.column()); }
    QList<int> rows = rowSet.values(), cols = colSet.values();
    std::sort(rows.begin(), rows.end());
    std::sort(cols.begin(), cols.end());
    QStringList lines;
    if (includeHeaders) {
        QStringList names;
        for (int c : cols) names << m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        lines << names.join(QLatin1Char('\t'));
    }
    for (int r : rows) {
        QStringList cells;
        for (int c : cols) {
            const QModelIndex idx = m_model->index(r, c);
            if (!selectionModel()->isSelected(idx)) { cells << QString(); continue; }
            cells << m_model->data(idx, SAResultModel::FullTextRole).toString();
        }
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

void SAResultTableView::copySelection(bool includeHeaders)
{
    const QString text = selectionAsText(includeHeaders);
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}

void SAResultTableView::autosizeColumns()
{
    if (!m_model) return;
    const int maxWidth = 420;
    const QFontMetrics fm(font());
    for (int c = 0; c < m_model->columnCount(); ++c) {
        int width = fm.horizontalAdvance(m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString()) + 24;
        const int rows = qMin(m_model->rowCount(), 200);
        for (int r = 0; r < rows; ++r) {
            const QString text = m_model->data(m_model->index(r, c), Qt::DisplayRole).toString();
            width = qMax(width, fm.horizontalAdvance(text.left(80)) + 16);
            if (width >= maxWidth) break;
        }
        setColumnWidth(c, qMin(width, maxWidth));
    }
}

void SAResultTableView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelection(false);
        event->accept();
        return;
    }
    if (m_editingEnabled && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && state() != EditingState) {
        Q_EMIT deleteRowsRequested();
        event->accept();
        return;
    }
    if (m_editingEnabled && event->key() == Qt::Key_N && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        Q_EMIT setNullRequested();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && state() != EditingState && currentIndex().isValid()) {
        if (m_editingEnabled && !(event->modifiers() & Qt::ShiftModifier)) {
            edit(currentIndex());
        } else {
            Q_EMIT editInSheetRequested(currentIndex());
        }
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}

void SAResultTableView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid() && m_model) {
        const QString group = m_model->data(index, SAResultModel::TypeGroupRole).toString();
        const bool binary = m_model->data(index, SAResultModel::IsBinaryRole).toBool();
        const QString text = m_model->data(index, SAResultModel::FullTextRole).toString();
        // Long or multi-line text, blobs and read-only grids open the field editor.
        if (!m_editingEnabled || binary || group == QLatin1String("textdata") || group == QLatin1String("blobdata")
            || group == QLatin1String("geometry") || text.contains(QLatin1Char('\n')) || text.size() > 255) {
            Q_EMIT editInSheetRequested(index);
            event->accept();
            return;
        }
    }
    QTableView::mouseDoubleClickEvent(event);
}

void SAResultTableView::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid() && !selectionModel()->isSelected(index)) setCurrentIndex(index);
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("Copy"), this, [this]() { copySelection(false); });
    copy->setShortcut(QKeySequence::Copy);
    menu.addAction(tr("Copy with Column Names"), this, [this]() { copySelection(true); });
    menu.addAction(tr("Copy as SQL INSERT"), this, [this]() { Q_EMIT copyAsInsertRequested(false); });
    menu.addAction(tr("Copy as SQL INSERT (no auto_inc)"), this, [this]() { Q_EMIT copyAsInsertRequested(true); });
    menu.addAction(tr("Export Result to File…"), this, [this]() { Q_EMIT exportRequested(); });
    menu.addSeparator();
    if (index.isValid()) {
        menu.addAction(tr("Open in Editor…"), this, [this, index]() { Q_EMIT editInSheetRequested(index); });
        menu.addAction(tr("Filter by this value"), this, [this, index]() { Q_EMIT columnFilterRequested(index); });
    }
    if (m_editingEnabled) {
        menu.addSeparator();
        menu.addAction(tr("Set to NULL"), this, [this]() { Q_EMIT setNullRequested(); });
        menu.addAction(tr("Add Row"), this, [this]() { Q_EMIT addRowRequested(); });
        menu.addAction(tr("Duplicate Row"), this, [this]() { Q_EMIT duplicateRowRequested(); });
        menu.addAction(tr("Delete Selected Rows"), this, [this]() { Q_EMIT deleteRowsRequested(); });
    }
    menu.addSeparator();
    menu.addAction(tr("Reload"), this, [this]() { Q_EMIT reloadRequested(); });
    menu.exec(event->globalPos());
}
